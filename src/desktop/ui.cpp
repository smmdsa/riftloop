#include "ui.h"
#include "core/imagecache.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <windowsx.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#pragma comment(lib, "dwmapi")
#pragma comment(lib, "uxtheme")
#pragma comment(lib, "msimg32")
#pragma comment(lib, "windowscodecs")

namespace rlui {

// ------------------------------------------------------------------- theme

namespace theme {

HBRUSH bgBrush() {
    static HBRUSH b = CreateSolidBrush(kBg);
    return b;
}
HBRUSH sidebarBrush() {
    static HBRUSH b = CreateSolidBrush(kSidebar);
    return b;
}
HBRUSH cardBrush() {
    static HBRUSH b = CreateSolidBrush(kCard);
    return b;
}

static HFONT makeFont(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, L"Segoe UI");
}
HFONT h1()     { static HFONT f = makeFont(24, FW_SEMIBOLD); return f; }
HFONT h2()     { static HFONT f = makeFont(17, FW_SEMIBOLD); return f; }
HFONT body()   { static HFONT f = makeFont(15, FW_NORMAL);  return f; }
HFONT small_() { static HFONT f = makeFont(13, FW_NORMAL);  return f; }
HFONT tiny()   { static HFONT f = makeFont(12, FW_NORMAL);  return f; }

} // namespace theme

void applyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
}

void applyDarkTheme(HWND control) {
    SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
}

// -------------------------------------------------------------- icon store

namespace icons {

namespace {

struct Pending { std::string kind, id, url; };

std::mutex mtx;
std::map<std::string, HBITMAP> cache;    // "kind_id" -> bitmap (or nullptr = failed)
std::deque<Pending> queue;
std::condition_variable cv;
std::thread worker;
std::atomic<bool> running{false};
HWND notifyHwnd = nullptr;
IWICImagingFactory* wic = nullptr;

HBITMAP loadPng(const std::wstring& path) {
    if (!wic) return nullptr;
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnLoad, &dec)))
        return nullptr;
    HBITMAP out = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    if (SUCCEEDED(dec->GetFrame(0, &frame))) {
        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(wic->CreateFormatConverter(&conv))) {
            if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0,
                                           WICBitmapPaletteTypeCustom))) {
                UINT w = 0, h = 0;
                conv->GetSize(&w, &h);
                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof bi.bmiHeader;
                bi.bmiHeader.biWidth = (LONG)w;
                bi.bmiHeader.biHeight = -(LONG)h;    // top-down
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                void* bits = nullptr;
                HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (bmp && bits &&
                    SUCCEEDED(conv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits)))
                    out = bmp;
                else if (bmp)
                    DeleteObject(bmp);
            }
            conv->Release();
        }
        frame->Release();
    }
    dec->Release();
    return out;
}

void workerLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (running) {
        Pending p;
        {
            std::unique_lock lk(mtx);
            cv.wait(lk, [] { return !running || !queue.empty(); });
            if (!running) break;
            p = queue.front();
            queue.pop_front();
        }
        rl::img::ensure(p.kind, p.id, p.url);    // downloads to the disk cache
        bool queueEmpty;
        {
            std::lock_guard lk(mtx);
            cache.erase(p.kind + "_" + p.id);    // reload from disk on next get
            queueEmpty = queue.empty();
        }
        if (queueEmpty && notifyHwnd) PostMessageW(notifyHwnd, WM_APP_ICONS, 0, 0);
    }
    CoUninitialize();
}

} // namespace

void init(HWND notifyWindow) {
    notifyHwnd = notifyWindow;
    if (!wic)
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&wic));
    running = true;
    worker = std::thread(workerLoop);
}

void shutdown() {
    running = false;
    cv.notify_all();
    if (worker.joinable()) worker.join();
}

HBITMAP peek(const std::string& kind, const std::string& id) {
    std::string key = kind + "_" + id;
    {
        std::lock_guard lk(mtx);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    auto path = rl::img::cached(kind, id);
    if (path.empty()) return nullptr;
    HBITMAP bmp = loadPng(path.wstring());
    std::lock_guard lk(mtx);
    cache[key] = bmp;
    return bmp;
}

HBITMAP get(const std::string& kind, const std::string& id, const std::string& url) {
    if (HBITMAP bmp = peek(kind, id)) return bmp;
    if (url.empty()) return nullptr;
    auto path = rl::img::cached(kind, id);
    if (path.empty()) {
        std::lock_guard lk(mtx);
        for (auto& p : queue)
            if (p.kind == kind && p.id == id) return nullptr;    // already queued
        queue.push_back({kind, id, url});
        cv.notify_one();
    }
    return nullptr;
}

} // namespace icons

void drawBitmap(HDC dc, HBITMAP bmp, int x, int y, int size) {
    if (!bmp) return;
    BITMAP info{};
    GetObjectW(bmp, sizeof info, &info);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, x, y, size, size, mem, 0, 0, info.bmWidth, info.bmHeight, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

// ------------------------------------------------------------- report view

namespace {

struct RVData {
    std::vector<RVItem> items;
    int scroll = 0;
    int contentHeight = 0;
};

int itemHeight(const RVItem& it) {
    switch (it.kind) {
        case RVKind::Title:   return 44;
        case RVKind::Section: return 38;
        case RVKind::Text:    return 24;
        case RVKind::Dim:     return 21;
        case RVKind::IconRow: return 32;
        case RVKind::Badge:   return 30;
        case RVKind::Spacer:  return 12;
        case RVKind::ClipCard: return 84;
        case RVKind::TeamStrip: return it.compact ? 50 : 78;
        case RVKind::PlayerRow: return 34;
    }
    return 22;
}

// Wrapped text needs extra height; measure with the target font.
int measureItem(HDC dc, const RVItem& it, int width) {
    if (it.kind != RVKind::Text && it.kind != RVKind::Dim) return itemHeight(it);
    HFONT font = it.kind == RVKind::Text ? theme::body() : theme::small_();
    HGDIOBJ old = SelectObject(dc, font);
    RECT rc{0, 0, width - 24 - it.indent, 0};
    DrawTextW(dc, it.text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK);
    SelectObject(dc, old);
    int lineH = it.kind == RVKind::Text ? 24 : 21;
    return (rc.bottom > lineH - 4) ? rc.bottom + 6 : lineH;
}

void rvPaint(HWND hwnd, RVData* d) {
    PAINTSTRUCT ps;
    HDC winDc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC dc = CreateCompatibleDC(winDc);
    HBITMAP back = CreateCompatibleBitmap(winDc, W, H);
    HGDIOBJ oldBmp = SelectObject(dc, back);

    HBRUSH cardBr = theme::cardBrush();
    FillRect(dc, &rc, cardBr);
    SetBkMode(dc, TRANSPARENT);

    int y = 14 - d->scroll;
    for (auto& it : d->items) {
        int h = measureItem(dc, it, W);
        if (y + h >= 0 && y <= H) {
            int x = 16 + it.indent;
            switch (it.kind) {
                case RVKind::Title: {
                    int tx = x;
                    if (!it.iconKind.empty()) {
                        HBITMAP bmp = icons::get(it.iconKind, it.iconId, it.iconUrl);
                        if (bmp) drawBitmap(dc, bmp, x, y + 2, 36);
                        else {
                            RECT ph{x, y + 2, x + 36, y + 38};
                            FillRect(dc, &ph, theme::bgBrush());
                        }
                        tx += 46;
                    }
                    SelectObject(dc, theme::h1());
                    SetTextColor(dc, theme::kText);
                    TextOutW(dc, tx, y + 5, it.text.c_str(), (int)it.text.size());
                    break;
                }
                case RVKind::Section: {
                    SelectObject(dc, theme::h2());
                    SetTextColor(dc, it.color ? it.color : theme::kAccent);
                    TextOutW(dc, x, y + 12, it.text.c_str(), (int)it.text.size());
                    HPEN pen = CreatePen(PS_SOLID, 1, theme::kBorder);
                    HGDIOBJ oldPen = SelectObject(dc, pen);
                    MoveToEx(dc, x, y + 34, nullptr);
                    LineTo(dc, W - 16, y + 34);
                    SelectObject(dc, oldPen);
                    DeleteObject(pen);
                    break;
                }
                case RVKind::Text:
                case RVKind::Dim: {
                    SelectObject(dc, it.kind == RVKind::Text ? theme::body() : theme::small_());
                    SetTextColor(dc, it.color ? it.color
                                              : (it.kind == RVKind::Text ? theme::kText
                                                                         : theme::kDim));
                    RECT trc{x, y + 2, W - 16, y + h};
                    DrawTextW(dc, it.text.c_str(), -1, &trc, DT_WORDBREAK);
                    break;
                }
                case RVKind::IconRow: {
                    HBITMAP bmp = it.iconKind.empty()
                                      ? nullptr
                                      : icons::get(it.iconKind, it.iconId, it.iconUrl);
                    if (bmp) {
                        drawBitmap(dc, bmp, x, y + 2, 26);
                    } else if (!it.iconKind.empty()) {
                        RECT ph{x, y + 2, x + 26, y + 28};
                        HBRUSH phb = CreateSolidBrush(theme::kCardHi);
                        FillRect(dc, &ph, phb);
                        DeleteObject(phb);
                    }
                    SelectObject(dc, theme::body());
                    SetTextColor(dc, it.color ? it.color : theme::kText);
                    TextOutW(dc, x + (it.iconKind.empty() ? 0 : 34), y + 6, it.text.c_str(),
                             (int)it.text.size());
                    if (!it.right.empty()) {
                        SelectObject(dc, theme::small_());
                        SetTextColor(dc, theme::kDim);
                        SIZE sz;
                        GetTextExtentPoint32W(dc, it.right.c_str(), (int)it.right.size(), &sz);
                        TextOutW(dc, W - 20 - sz.cx, y + 8, it.right.c_str(),
                                 (int)it.right.size());
                    }
                    break;
                }
                case RVKind::PlayerRow: {
                    // A scoreboard line: portrait, name and role, KDA, farm,
                    // then the chips. Every column starts at a fixed offset so
                    // ten rows read as a table and not as ten sentences.
                    HBITMAP bmp = it.iconKind.empty()
                                      ? nullptr
                                      : icons::get(it.iconKind, it.iconId, it.iconUrl);
                    if (bmp) drawBitmap(dc, bmp, x, y + 3, 26);
                    else if (!it.iconKind.empty()) {
                        RECT ph{x, y + 3, x + 26, y + 29};
                        HBRUSH phb = CreateSolidBrush(theme::kCardHi);
                        FillRect(dc, &ph, phb);
                        DeleteObject(phb);
                    }
                    // The colour band on the left says which side played it.
                    if (it.color) {
                        RECT band{x - 8, y + 3, x - 5, y + 29};
                        HBRUSH cb = CreateSolidBrush(it.color);
                        FillRect(dc, &band, cb);
                        DeleteObject(cb);
                    }

                    SelectObject(dc, theme::small_());
                    SetTextColor(dc, theme::kText);
                    RECT nameRc{x + 34, y + 2, x + 34 + 174, y + 32};
                    DrawTextW(dc, it.text.c_str(), -1, &nameRc,
                              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    SetTextColor(dc, theme::kDim);
                    RECT kdaRc{x + 214, y + 2, x + 214 + 90, y + 32};
                    DrawTextW(dc, it.kda.c_str(), -1, &kdaRc,
                              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                    RECT farmRc{x + 306, y + 2, x + 306 + 170, y + 32};
                    DrawTextW(dc, it.farm.c_str(), -1, &farmRc,
                              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    int cx = x + 484;
                    for (size_t ci = 0; ci < it.chips.size(); ++ci) {
                        if ((int)ci == it.chipGap && ci > 0) cx += 10;
                        const RVChip& chip = it.chips[ci];
                        RECT slot{cx, y + 5, cx + 22, y + 27};
                        HBITMAP cb = chip.id.empty()
                                         ? nullptr
                                         : icons::get(chip.kind, chip.id, chip.url);
                        if (cb) {
                            drawBitmap(dc, cb, cx, y + 5, 22);
                        } else {
                            HBRUSH e = CreateSolidBrush(theme::kBg);
                            FillRect(dc, &slot, e);
                            DeleteObject(e);
                        }
                        cx += 24;
                    }

                    if (!it.right.empty()) {
                        SelectObject(dc, theme::small_());
                        SetTextColor(dc, theme::kDim);
                        SIZE sz;
                        GetTextExtentPoint32W(dc, it.right.c_str(), (int)it.right.size(), &sz);
                        TextOutW(dc, W - 20 - sz.cx, y + 9, it.right.c_str(),
                                 (int)it.right.size());
                    }
                    break;
                }
                case RVKind::ClipCard: {
                    // Card: thumbnail, title, subtitle. Spaced so a playlist
                    // reads as a list of moments, not as a dense table.
                    RECT card{x, y, W - 16, y + h - 10};
                    HBRUSH bg = CreateSolidBrush(it.selected ? theme::kCardHi : theme::kBg);
                    FillRect(dc, &card, bg);
                    DeleteObject(bg);
                    if (it.selected) {
                        RECT edge{card.left, card.top, card.left + 3, card.bottom};
                        HBRUSH acc = CreateSolidBrush(theme::kAccent);
                        FillRect(dc, &edge, acc);
                        DeleteObject(acc);
                    }
                    int tw = 96, th = 54;
                    if (it.thumb) {
                        HDC mem = CreateCompatibleDC(dc);
                        HGDIOBJ oldB = SelectObject(mem, it.thumb);
                        BITMAP bm{};
                        GetObject(it.thumb, sizeof bm, &bm);
                        SetStretchBltMode(dc, HALFTONE);
                        StretchBlt(dc, card.left + 10, card.top + 6, tw, th, mem, 0, 0, bm.bmWidth,
                                   bm.bmHeight, SRCCOPY);
                        SelectObject(mem, oldB);
                        DeleteDC(mem);
                    } else {
                        RECT ph{card.left + 10, card.top + 6, card.left + 10 + tw,
                                card.top + 6 + th};
                        HBRUSH b2 = CreateSolidBrush(theme::kCard);
                        FillRect(dc, &ph, b2);
                        DeleteObject(b2);
                    }
                    int textX = card.left + 10 + tw + 12;
                    SelectObject(dc, theme::body());
                    SetTextColor(dc, theme::kText);
                    RECT tr{textX, card.top + 8, card.right - 10, card.top + 34};
                    DrawTextW(dc, it.text.c_str(), -1, &tr, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE);
                    SelectObject(dc, theme::small_());
                    SetTextColor(dc, theme::kDim);
                    // Two lines at most, with an ellipsis: a card that spills
                    // its text over the next one reads as broken.
                    RECT sr{textX, card.top + 32, card.right - 10, card.bottom - 6};
                    DrawTextW(dc, it.subtitle.c_str(), -1, &sr,
                              DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
                    break;
                }
                case RVKind::TeamStrip: {
                    // Two rows of portraits facing each other, with the row name
                    // in the middle. A seat with no champion keeps its box, so
                    // the row never shifts when a late pick arrives.
                    // Picks decide the game; bans are context. Size says so.
                    const int kPortrait = it.compact ? 40 : 52;
                    const int kGap = it.compact ? 6 : 8, kTop = it.compact ? 2 : 4;
                    auto drawSeats = [&](const std::vector<RVSeat>& seats, int originX,
                                         COLORREF accent, bool rightToLeft) {
                        for (size_t i = 0; i < seats.size(); ++i) {
                            const RVSeat& s = seats[i];
                            int step = (int)i * (kPortrait + kGap);
                            int sx = rightToLeft ? originX - step - kPortrait : originX + step;
                            int sy = y + kTop;
                            RECT box{sx, sy, sx + kPortrait, sy + kPortrait};

                            HBITMAP bmp = s.champion.empty()
                                              ? nullptr
                                              : icons::get("champ", s.champion, s.iconUrl);
                            if (bmp) {
                                drawBitmap(dc, bmp, sx, sy, kPortrait);
                                // A ban is the same portrait, spent: dimmed and
                                // crossed. It must not read as a pick.
                                if (s.banned) {
                                    HDC mem = CreateCompatibleDC(dc);
                                    HBITMAP veil = CreateCompatibleBitmap(dc, kPortrait,
                                                                          kPortrait);
                                    HGDIOBJ ob = SelectObject(mem, veil);
                                    RECT vr{0, 0, kPortrait, kPortrait};
                                    FillRect(mem, &vr, theme::cardBrush());
                                    // A ban must stay recognisable: the veil says "spent",
                                    // it does not hide who it is.
                                    BLENDFUNCTION bf{AC_SRC_OVER, 0, 55, 0};
                                    AlphaBlend(dc, sx, sy, kPortrait, kPortrait, mem, 0, 0,
                                               kPortrait, kPortrait, bf);
                                    SelectObject(mem, ob);
                                    DeleteObject(veil);
                                    DeleteDC(mem);
                                    HPEN sl = CreatePen(PS_SOLID, 2, theme::kDanger);
                                    HGDIOBJ op = SelectObject(dc, sl);
                                    MoveToEx(dc, sx + 2, sy + kPortrait - 2, nullptr);
                                    LineTo(dc, sx + kPortrait - 2, sy + 2);
                                    SelectObject(dc, op);
                                    DeleteObject(sl);
                                }
                            } else {
                                // Reserved, still open. A dotted box holds the
                                // place so nothing jumps when the pick lands.
                                HPEN pen = CreatePen(PS_DOT, 1, theme::kBorder);
                                HGDIOBJ op = SelectObject(dc, pen);
                                HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
                                Rectangle(dc, box.left, box.top, box.right, box.bottom);
                                SelectObject(dc, ob);
                                SelectObject(dc, op);
                                DeleteObject(pen);
                            }

                            // Team colour frames every filled seat. The user's
                            // own champion gets a thicker frame in kGood.
                            if (!s.champion.empty()) {
                                COLORREF edge = s.isUser ? theme::kGood : accent;
                                int width = s.isUser ? 3 : 1;
                                HPEN pen = CreatePen(s.hover ? PS_DOT : PS_SOLID, width, edge);
                                HGDIOBJ op = SelectObject(dc, pen);
                                HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
                                Rectangle(dc, box.left, box.top, box.right, box.bottom);
                                SelectObject(dc, ob);
                                SelectObject(dc, op);
                                DeleteObject(pen);
                            }

                            if (!s.label.empty()) {
                                SelectObject(dc, theme::tiny());
                                SetTextColor(dc, theme::kDim);
                                RECT lr{sx - 4, sy + kPortrait + 1, sx + kPortrait + 4,
                                        sy + kPortrait + 16};
                                DrawTextW(dc, s.label.c_str(), -1, &lr,
                                          DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                            }
                        }
                    };
                    drawSeats(it.leftSeats, x, it.leftColor ? it.leftColor : theme::kAccent, false);
                    drawSeats(it.rightSeats, W - 16, it.rightColor ? it.rightColor : theme::kDanger,
                              true);
                    if (!it.text.empty()) {
                        SelectObject(dc, theme::tiny());
                        SetTextColor(dc, theme::kDim);
                        RECT mr{x, y + kTop + 14, W - 16, y + kTop + 32};
                        DrawTextW(dc, it.text.c_str(), -1, &mr,
                                  DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    }
                    break;
                }
                case RVKind::Badge: {
                    SelectObject(dc, theme::small_());
                    SIZE sz;
                    GetTextExtentPoint32W(dc, it.text.c_str(), (int)it.text.size(), &sz);
                    COLORREF col = it.color ? it.color : theme::kAccent;
                    RECT pill{x, y + 4, x + sz.cx + 20, y + 26};
                    HBRUSH pb = CreateSolidBrush(theme::kCardHi);
                    FillRect(dc, &pill, pb);
                    DeleteObject(pb);
                    SetTextColor(dc, col);
                    TextOutW(dc, x + 10, y + 6, it.text.c_str(), (int)it.text.size());
                    break;
                }
                case RVKind::Spacer:
                    break;
            }
        }
        y += h;
    }
    d->contentHeight = y + d->scroll + 10;

    BitBlt(winDc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(back);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

void rvUpdateScrollbar(HWND hwnd, RVData* d) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMin = 0;
    si.nMax = d->contentHeight > 0 ? d->contentHeight : 1;
    si.nPage = rc.bottom;
    si.nPos = d->scroll;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

// Item under a client-area point, or -1. Uses the same walk as the painter so
// the hit area always matches what the user sees.
int rvHitTest(HWND hwnd, RVData* d, int py) {
    HDC dc = GetDC(hwnd);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HGDIOBJ oldFont = GetCurrentObject(dc, OBJ_FONT);
    int y = 14 - d->scroll;
    int hit = -1;
    for (size_t i = 0; i < d->items.size(); ++i) {
        int h = measureItem(dc, d->items[i], rc.right);
        if (py >= y && py < y + h) { hit = (int)i; break; }
        y += h;
    }
    SelectObject(dc, oldFont);
    ReleaseDC(hwnd, dc);
    return hit;
}

LRESULT CALLBACK ReportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = (RVData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE:
            d = new RVData();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
            return 0;
        case WM_PAINT:
            if (d) {
                rvPaint(hwnd, d);
                rvUpdateScrollbar(hwnd, d);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (d) {
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                RECT rc;
                GetClientRect(hwnd, &rc);
                int maxScroll = d->contentHeight - rc.bottom;
                if (maxScroll < 0) maxScroll = 0;
                d->scroll -= delta / 2;
                if (d->scroll < 0) d->scroll = 0;
                if (d->scroll > maxScroll) d->scroll = maxScroll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_VSCROLL:
            if (d) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int maxScroll = d->contentHeight - rc.bottom;
                if (maxScroll < 0) maxScroll = 0;
                int line = 30;
                switch (LOWORD(wp)) {
                    case SB_LINEUP: d->scroll -= line; break;
                    case SB_LINEDOWN: d->scroll += line; break;
                    case SB_PAGEUP: d->scroll -= rc.bottom; break;
                    case SB_PAGEDOWN: d->scroll += rc.bottom; break;
                    case SB_THUMBTRACK:
                    case SB_THUMBPOSITION: d->scroll = HIWORD(wp); break;
                }
                if (d->scroll < 0) d->scroll = 0;
                if (d->scroll > maxScroll) d->scroll = maxScroll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (d) {
                int idx = rvHitTest(hwnd, d, GET_Y_LPARAM(lp));
                if (idx >= 0 && !d->items[idx].action.empty())
                    PostMessageW(GetParent(hwnd), WM_RV_ACTION, (WPARAM)idx, (LPARAM)hwnd);
            }
            return 0;
        case WM_SETCURSOR: {
            // Hand cursor over a clickable row: the only affordance a flat
            // report view can give.
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (d) {
                int idx = rvHitTest(hwnd, d, pt.y);
                if (idx >= 0 && !d->items[idx].action.empty()) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            delete d;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void registerReportView(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ReportProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"RiftLoopReport";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
}

void rvSet(HWND view, std::vector<RVItem> items) {
    if (!view) return;
    auto* d = (RVData*)GetWindowLongPtrW(view, GWLP_USERDATA);
    if (!d) return;
    d->items = std::move(items);
    d->scroll = 0;
    InvalidateRect(view, nullptr, FALSE);
}

void rvHighlight(HWND view, int index) {
    if (!view) return;
    auto* d = (RVData*)GetWindowLongPtrW(view, GWLP_USERDATA);
    if (!d) return;
    for (size_t i = 0; i < d->items.size(); ++i) d->items[i].selected = ((int)i == index);
    InvalidateRect(view, nullptr, FALSE);
}

std::string rvActionAt(HWND view, int index) {
    if (!view) return {};
    auto* d = (RVData*)GetWindowLongPtrW(view, GWLP_USERDATA);
    if (!d || index < 0 || index >= (int)d->items.size()) return {};
    return d->items[index].action;
}

} // namespace rlui
