// RiftLoop.Overlay: external transparent window (PRD 9.9, 14.2).
// - click-through by default; Ctrl+Shift+M toggles move mode
// - Ctrl+Shift+O toggles expanded view
// - renders only when data or visibility changes (event-driven, PRD 15.4)
// - shows at most 3 purchase decisions with one-line conditions (RF-OVR-003)
#include "core/config.h"
#include "core/db.h"
#include "core/ddragon.h"
#include "core/ipc.h"
#include "core/serial.h"
#include "core/util.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

using namespace rl;
using nlohmann::json;

namespace {

constexpr int kHotkeyExpand = 1;
constexpr int kHotkeyMove = 2;
constexpr int kWidth = 340;
constexpr int kHeightCollapsed = 190;
constexpr int kHeightExpanded = 420;
constexpr UINT WM_APP_REFRESH = WM_APP + 5;

struct Line { std::wstring text; float size; bool bold; D2D1::ColorF color = D2D1::ColorF(0.9f, 0.9f, 0.9f); };

struct OverlayState {
    HWND hwnd = nullptr;
    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;

    Ddragon dd;
    bool ddOk = false;

    std::mutex mutex;
    ItemPlan plan;
    std::wstring header = L"RiftLoop";
    bool hasPlan = false;
    bool visible = false;
    bool expanded = false;
    bool moveMode = false;

    std::unique_ptr<ipc::Client> client;
};

OverlayState* g = nullptr;

std::wstring itemName(int id) {
    if (g->ddOk)
        if (const ItemInfo* it = g->dd.item(id)) return util::widen(it->name);
    return L"Item " + std::to_wstring(id);
}

std::vector<Line> buildLines() {
    std::vector<Line> lines;
    std::lock_guard lk(g->mutex);
    lines.push_back({g->header, 15.0f, true, D2D1::ColorF(0.55f, 0.78f, 1.0f)});
    if (!g->hasPlan) {
        lines.push_back({L"Sin plan de compra para esta partida.", 13.0f, false});
        lines.push_back({L"Se genera al bloquear campeon en champ select.", 12.0f, false,
                         D2D1::ColorF(0.6f, 0.6f, 0.6f)});
        return lines;
    }
    const ItemPlan& p = g->plan;
    if (!p.core.empty()) {
        lines.push_back({L"Nucleo:", 13.0f, true});
        for (int id : p.core)
            lines.push_back({L"  " + itemName(id), 13.0f, false});
    }
    int shown = 0;
    for (auto& b : p.branches) {
        if (++shown > 3) break;      // hard cap (RF-OVR-003)
        lines.push_back({util::widen(b.label) + L":", 13.0f, true});
        std::wstring names;
        for (size_t i = 0; i < b.items.size() && i < 2; ++i)
            names += (i ? L" / " : L"  ") + itemName(b.items[i]);
        lines.push_back({names, 13.0f, false});
        lines.push_back({L"  " + util::widen(b.condition), 11.5f, false,
                         D2D1::ColorF(0.65f, 0.65f, 0.65f)});
    }
    if (g->expanded) {
        if (!p.boots.empty()) {
            lines.push_back({L"Botas:", 13.0f, true});
            for (auto& b : p.boots) {
                std::wstring names;
                for (size_t i = 0; i < b.items.size() && i < 1; ++i) names += itemName(b.items[i]);
                lines.push_back({L"  " + names, 13.0f, false});
                lines.push_back({L"  " + util::widen(b.condition), 11.5f, false,
                                 D2D1::ColorF(0.65f, 0.65f, 0.65f)});
            }
        }
        lines.push_back({L"Confianza: " + util::widen(p.confidence), 11.5f, false,
                         D2D1::ColorF(0.6f, 0.6f, 0.6f)});
    } else {
        lines.push_back({L"Ctrl+Shift+O: expandir  Ctrl+Shift+M: mover", 10.5f, false,
                         D2D1::ColorF(0.5f, 0.5f, 0.5f)});
    }
    return lines;
}

void render() {
    if (!g->rt) return;
    g->rt->BeginDraw();
    g->rt->Clear(D2D1::ColorF(0.07f, 0.08f, 0.10f, 1.0f));

    ID2D1SolidColorBrush* brush = nullptr;
    g->rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush);
    if (brush) {
        float y = 10.0f;
        for (auto& line : buildLines()) {
            IDWriteTextFormat* fmt = nullptr;
            g->dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                                        line.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                                  : DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                        line.size, L"es-ES", &fmt);
            if (fmt) {
                brush->SetColor(line.color);
                D2D1_RECT_F rc = D2D1::RectF(12.0f, y, (float)kWidth - 12.0f, y + line.size + 8);
                g->rt->DrawTextW(line.text.c_str(), (UINT32)line.text.size(), fmt, rc, brush);
                fmt->Release();
            }
            y += line.size + 7;
        }
        brush->Release();
    }
    g->rt->EndDraw();
}

void applyVisibility() {
    int h = g->expanded ? kHeightExpanded : kHeightCollapsed;
    SetWindowPos(g->hwnd, HWND_TOPMOST, 0, 0, kWidth, h,
                 SWP_NOMOVE | (g->visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) | SWP_NOACTIVATE);
    if (g->rt) g->rt->Resize(D2D1::SizeU(kWidth, h));
    if (g->visible) render();
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
            }
            PostMessageW(g->hwnd, WM_APP_REFRESH, 0, 0);
        } else if (type == "plan") {
            std::lock_guard lk(g->mutex);
            const json& d = j.at("data").at("options");
            g->plan = d.at("items").get<ItemPlan>();
            g->header = util::widen(d.value("champion", "") + " - " + d.value("role", ""));
            g->hasPlan = true;
            PostMessageW(g->hwnd, WM_APP_REFRESH, 0, 0);
        }
    } catch (...) {}
}

void setClickThrough(bool enabled) {
    LONG ex = GetWindowLongW(g->hwnd, GWL_EXSTYLE);
    if (enabled) ex |= WS_EX_TRANSPARENT;
    else ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongW(g->hwnd, GWL_EXSTYLE, ex);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_REFRESH:
            applyVisibility();
            return 0;
        case WM_HOTKEY:
            if (wp == kHotkeyExpand) {
                g->expanded = !g->expanded;
                applyVisibility();
            } else if (wp == kHotkeyMove) {
                g->moveMode = !g->moveMode;
                setClickThrough(!g->moveMode);
            }
            return 0;
        case WM_NCHITTEST:
            if (g->moveMode) return HTCAPTION;   // drag anywhere in move mode
            break;
        case WM_EXITSIZEMOVE: {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            Config cfg = Config::load();
            cfg.overlayX = rc.left;
            cfg.overlayY = rc.top;
            cfg.save();                          // remember position (RF-OVR-002)
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
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
    state.ddOk = state.dd.load(false);           // cache only: fast start

    // Last plan survives an overlay restart mid-game.
    try {
        Db db;
        std::string last = db.lastRecommendation("pregame_plan");
        if (!last.empty()) {
            json c = json::parse(last);
            state.plan = c.at("options").at("items").get<ItemPlan>();
            state.header = util::widen(c["options"].value("champion", "") + " - " +
                                       c["options"].value("role", ""));
            state.hasPlan = true;
        }
    } catch (...) {}

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
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
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
    if (state.rt) state.rt->Release();
    if (state.dwrite) state.dwrite->Release();
    if (state.d2d) state.d2d->Release();
    if (mutex) CloseHandle(mutex);
    return 0;
}
