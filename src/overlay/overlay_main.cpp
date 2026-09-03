// RiftLoop.Overlay: external transparent window (PRD 9.9, 14.2).
// - header strip is interactive: drag to move, [+] expand, [x] hide
// - the body is click-through so the game receives every click (RF-OVR-001)
// - renders only when data or visibility changes (event-driven, PRD 15.4)
// - draws localized item names and cached Data Dragon icons
// - shows at most 3 purchase decisions with one-line conditions (RF-OVR-003)
#include "core/config.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/imagecache.h"
#include "core/ipc.h"
#include "core/lcu_history.h"
#include "core/serial.h"
#include "core/util.h"

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "windowscodecs")

using namespace rl;
using nlohmann::json;

namespace {

constexpr int kHotkeyExpand = 1;
constexpr int kHotkeyMove = 2;
constexpr int kWidth = 360;
constexpr int kHeader = 28;
constexpr int kHeightCollapsed = 230;
constexpr int kHeightExpanded = 470;
constexpr int kIcon = 18;
constexpr UINT WM_APP_REFRESH = WM_APP + 5;

struct Row {
    std::wstring text;
    float size = 13.0f;
    bool bold = false;
    int itemIcon = 0;                // item id to draw an icon for, 0 = none
    D2D1::ColorF color = D2D1::ColorF(0.9f, 0.9f, 0.9f);
};

struct OverlayState {
    HWND hwnd = nullptr;
    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dwrite = nullptr;
    IWICImagingFactory* wic = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    std::map<int, ID2D1Bitmap*> iconCache;   // itemId -> bitmap (rt lifetime)

    Ddragon dd;
    bool ddOk = false;

    std::mutex mutex;
    ItemPlan plan;
    std::wstring header = L"RiftLoop";
    bool hasPlan = false;
    bool visible = false;
    bool expanded = false;
    bool userHidden = false;         // [x] pressed: stay hidden this game

    std::unique_ptr<ipc::Client> client;
};

OverlayState* g = nullptr;

std::wstring itemName(int id) {
    if (g->ddOk)
        if (const ItemInfo* it = g->dd.item(id)) return util::widen(it->name);
    return L"Item " + std::to_wstring(id);
}

void clearIconCache() {
    for (auto& [id, bmp] : g->iconCache)
        if (bmp) bmp->Release();
    g->iconCache.clear();
}

ID2D1Bitmap* iconFor(int itemId) {
    auto it = g->iconCache.find(itemId);
    if (it != g->iconCache.end()) return it->second;
    ID2D1Bitmap* result = nullptr;
    auto path = img::cached("item", std::to_string(itemId));
    if (!path.empty() && g->wic && g->rt) {
        IWICBitmapDecoder* dec = nullptr;
        if (SUCCEEDED(g->wic->CreateDecoderFromFilename(path.wstring().c_str(), nullptr,
                                                        GENERIC_READ,
                                                        WICDecodeMetadataCacheOnLoad, &dec))) {
            IWICBitmapFrameDecode* frame = nullptr;
            if (SUCCEEDED(dec->GetFrame(0, &frame))) {
                IWICFormatConverter* conv = nullptr;
                if (SUCCEEDED(g->wic->CreateFormatConverter(&conv))) {
                    if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                                   WICBitmapDitherTypeNone, nullptr, 0,
                                                   WICBitmapPaletteTypeCustom)))
                        g->rt->CreateBitmapFromWicBitmap(conv, nullptr, &result);
                    conv->Release();
                }
                frame->Release();
            }
            dec->Release();
        }
    }
    g->iconCache[itemId] = result;   // cache misses too: no retry loop in game
    return result;
}

std::vector<Row> buildRows() {
    std::vector<Row> rows;
    std::lock_guard lk(g->mutex);
    if (!g->hasPlan) {
        rows.push_back({L"Sin plan de compra para esta partida.", 13.0f, false});
        rows.push_back({L"Se genera al bloquear campeon en champ select.", 12.0f, false, 0,
                        D2D1::ColorF(0.6f, 0.6f, 0.6f)});
        return rows;
    }
    const ItemPlan& p = g->plan;
    if (!p.core.empty()) {
        rows.push_back({L"Nucleo:", 13.0f, true});
        for (int id : p.core) rows.push_back({itemName(id), 13.0f, false, id});
    }
    int shown = 0;
    for (auto& b : p.branches) {
        if (++shown > 3) break;      // hard cap (RF-OVR-003)
        rows.push_back({util::widen(b.label) + L":", 13.0f, true});
        for (size_t i = 0; i < b.items.size() && i < 2; ++i)
            rows.push_back({itemName(b.items[i]), 13.0f, false, b.items[i]});
        rows.push_back({util::widen(b.condition), 11.0f, false, 0,
                        D2D1::ColorF(0.65f, 0.65f, 0.65f)});
    }
    if (g->expanded) {
        if (!p.boots.empty()) {
            rows.push_back({L"Botas:", 13.0f, true});
            for (auto& b : p.boots) {
                if (!b.items.empty())
                    rows.push_back({itemName(b.items[0]), 13.0f, false, b.items[0]});
                rows.push_back({util::widen(b.condition), 11.0f, false, 0,
                                D2D1::ColorF(0.65f, 0.65f, 0.65f)});
            }
        }
        rows.push_back({L"Confianza: " + util::widen(g->plan.confidence), 11.0f, false, 0,
                        D2D1::ColorF(0.6f, 0.6f, 0.6f)});
    }
    return rows;
}

void drawText(const std::wstring& text, float x, float y, float w, float size, bool bold,
              const D2D1::ColorF& color, ID2D1SolidColorBrush* brush) {
    IDWriteTextFormat* fmt = nullptr;
    g->dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                                bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                L"es-ES", &fmt);
    if (!fmt) return;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    brush->SetColor(color);
    g->rt->DrawTextW(text.c_str(), (UINT32)text.size(), fmt,
                     D2D1::RectF(x, y, x + w, y + size + 8), brush);
    fmt->Release();
}

void render() {
    if (!g->rt) return;
    g->rt->BeginDraw();
    g->rt->Clear(D2D1::ColorF(0.07f, 0.08f, 0.10f, 1.0f));

    ID2D1SolidColorBrush* brush = nullptr;
    g->rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush);
    if (brush) {
        // Header strip (interactive zone).
        brush->SetColor(D2D1::ColorF(0.12f, 0.14f, 0.20f));
        g->rt->FillRectangle(D2D1::RectF(0, 0, (float)kWidth, (float)kHeader), brush);
        drawText(g->header, 10.0f, 5.0f, kWidth - 70.0f, 14.0f, true,
                 D2D1::ColorF(0.55f, 0.78f, 1.0f), brush);
        drawText(g->expanded ? L"–" : L"+", kWidth - 46.0f, 3.0f, 20.0f, 16.0f, true,
                 D2D1::ColorF(0.8f, 0.8f, 0.8f), brush);
        drawText(L"x", kWidth - 24.0f, 3.0f, 20.0f, 15.0f, true,
                 D2D1::ColorF(0.8f, 0.5f, 0.5f), brush);

        float y = kHeader + 8.0f;
        for (auto& row : buildRows()) {
            float x = 12.0f;
            if (row.itemIcon) {
                if (ID2D1Bitmap* bmp = iconFor(row.itemIcon)) {
                    g->rt->DrawBitmap(bmp, D2D1::RectF(x, y, x + kIcon, y + kIcon));
                }
                x += kIcon + 6.0f;
            }
            drawText(row.text, x, y, kWidth - x - 10.0f, row.size, row.bold, row.color, brush);
            y += row.size + (row.itemIcon ? 9.0f : 7.0f);
        }
        drawText(L"Ctrl+Shift+O expandir | arrastra la barra para mover", 10.0f,
                 (g->expanded ? kHeightExpanded : kHeightCollapsed) - 20.0f, kWidth - 20.0f,
                 10.0f, false, D2D1::ColorF(0.45f, 0.45f, 0.45f), brush);
        brush->Release();
    }
    if (g->rt->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) {
        clearIconCache();
        g->rt->Release();
        g->rt = nullptr;
    }
}

void applyVisibility() {
    int h = g->expanded ? kHeightExpanded : kHeightCollapsed;
    bool show = g->visible && !g->userHidden;
    SetWindowPos(g->hwnd, HWND_TOPMOST, 0, 0, kWidth, h,
                 SWP_NOMOVE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) | SWP_NOACTIVATE);
    if (g->rt) g->rt->Resize(D2D1::SizeU(kWidth, h));
    if (show) render();
}

void onIpcMessage(const std::string& text) {
    try {
        json j = json::parse(text);
        std::string type = j.value("type", "");
        if (type == "state") {
            std::string s = j.value("state", "");
            bool show = s == "InGame";
            {
                std::lock_guard lk(g->mutex);
                if (g->visible == show) return;
                g->visible = show;
                if (!show) g->userHidden = false;     // reset the [x] between games
            }
            PostMessageW(g->hwnd, WM_APP_REFRESH, 0, 0);
        } else if (type == "plan") {
            std::lock_guard lk(g->mutex);
            const json& d = j.at("data").at("options");
            g->plan = d.at("items").get<ItemPlan>();
            std::string champ = d.value("champion", "");
            if (g->ddOk)
                if (const ChampInfo* c = g->dd.champion(champ)) champ = c->name;
            g->header = util::widen(champ + " - " + d.value("role", ""));
            g->hasPlan = true;
            g->userHidden = false;
            PostMessageW(g->hwnd, WM_APP_REFRESH, 0, 0);
        }
    } catch (...) {}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_REFRESH:
            applyVisibility();
            return 0;
        case WM_HOTKEY:
            if (wp == kHotkeyExpand || wp == kHotkeyMove) {
                if (wp == kHotkeyExpand) g->expanded = !g->expanded;
                applyVisibility();
            }
            return 0;
        case WM_NCHITTEST: {
            // Header: interactive. Body: clicks pass through to the game.
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y <= kHeader) {
                if (pt.x >= kWidth - 50) return HTCLIENT;    // [+] / [x] buttons
                return HTCAPTION;                            // drag anywhere else
            }
            return HTTRANSPARENT;
        }
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (y <= kHeader) {
                if (x >= kWidth - 50 && x < kWidth - 26) {
                    g->expanded = !g->expanded;
                    applyVisibility();
                } else if (x >= kWidth - 26) {
                    g->userHidden = true;    // hide for the rest of this game
                    applyVisibility();
                }
            }
            return 0;
        }
        case WM_EXITSIZEMOVE: {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            Config cfg = Config::load();
            cfg.overlayX = rc.left;
            cfg.overlayY = rc.top;
            cfg.save();                      // remember position (RF-OVR-002)
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (!g->rt && g->d2d) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                g->d2d->CreateHwndRenderTarget(
                    D2D1::RenderTargetProperties(),
                    D2D1::HwndRenderTargetProperties(hwnd,
                        D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
                    &g->rt);
            }
            render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"RiftLoop.Overlay.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    OverlayState state;
    g = &state;

    // Static data from cache in the client's language; no network at start.
    try {
        Db db;
        Config cfg = Config::load();
        std::string locale = resolveDataLocale(db, nullptr, cfg.dataLocale);
        state.ddOk = state.dd.load(false, locale);

        std::string last = db.lastRecommendation("pregame_plan");
        if (!last.empty()) {
            json c = json::parse(last);
            state.plan = c.at("options").at("items").get<ItemPlan>();
            std::string champ = c["options"].value("champion", "");
            if (state.ddOk)
                if (const ChampInfo* ci = state.dd.champion(champ)) champ = ci->name;
            state.header = util::widen(champ + " - " + c["options"].value("role", ""));
            state.hasPlan = true;
        }
    } catch (...) {}

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&state.wic));

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RiftLoopOverlayWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    Config cfg = Config::load();
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int x = cfg.overlayX >= 0 ? cfg.overlayX : screenW - kWidth - 24;
    int y = cfg.overlayY >= 0 ? cfg.overlayY : 220;

    state.hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"RiftLoop Overlay", WS_POPUP,
        x, y, kWidth, kHeightCollapsed, nullptr, nullptr, hInst, nullptr);
    SetLayeredWindowAttributes(state.hwnd, 0, 235, LWA_ALPHA);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &state.d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        (IUnknown**)&state.dwrite);
    if (state.d2d) {
        state.d2d->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(state.hwnd, D2D1::SizeU(kWidth, kHeightCollapsed)),
            &state.rt);
    }

    RegisterHotKey(state.hwnd, kHotkeyExpand, MOD_CONTROL | MOD_SHIFT, 'O');
    RegisterHotKey(state.hwnd, kHotkeyMove, MOD_CONTROL | MOD_SHIFT, 'M');

    state.client = std::make_unique<ipc::Client>(onIpcMessage);
    state.client->start();

    // The overlay starts hidden; the Agent's state broadcast reveals it.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnregisterHotKey(state.hwnd, kHotkeyExpand);
    UnregisterHotKey(state.hwnd, kHotkeyMove);
    clearIconCache();
    if (state.rt) state.rt->Release();
    if (state.wic) state.wic->Release();
    if (state.dwrite) state.dwrite->Release();
    if (state.d2d) state.d2d->Release();
    if (mutex) CloseHandle(mutex);
    return 0;
}
