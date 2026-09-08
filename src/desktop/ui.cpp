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

// loadPng lives in the anonymous namespace above, so nothing outside icons can
// reach it. localIcon needs exactly that and nothing else.
HBITMAP loadFromFile(const std::wstring& path) {
    return loadPng(path);
}

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

HBITMAP localIcon(const std::string& name) {
    // Loaded once and kept: assets\icons holds twenty files that never change
    // while the program runs, and a miss is remembered as a miss.
    static std::map<std::string, HBITMAP> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t cut = dir.find_last_of(L'\\');
    dir = cut == std::wstring::npos ? L"." : dir.substr(0, cut);

    std::wstring wide(name.begin(), name.end());
    HBITMAP bmp = icons::loadFromFile(dir + L"\\assets\\icons\\" + wide + L".png");
    cache[name] = bmp;
    return bmp;
}

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
    // Hover state for the explanation tip. -1 = the pointer is on no row.
    int  hoverIndex = -1;
    int  hoverChip = -1;
    bool trackingLeave = false;
    // Left edge of the place column, per row, measured while painting. The
    // place is right-aligned after a title of any width, so one shared value
    // would give every row the widest row's zone.
    std::map<int, int> placeLeft;
};

// Column geometry of a PlayerRow, in offsets from the row origin. The painter
// and the tip hit test both read these, so what the pointer is over is always
// what the eye is over.
namespace playerRow {
constexpr int kPortrait = 0,  kPortraitW = 30;
constexpr int kRole     = 30, kRoleW     = 20;
constexpr int kName     = 50, kNameW     = 178;
// The rank emblem sits between the name and the numbers, in its own column.
constexpr int kTier     = 232, kTierW    = 34;
constexpr int kKda      = 274, kKdaW     = 90;
constexpr int kFarm     = 368, kFarmW    = 168;
constexpr int kChips    = 542, kChipW    = 24;
constexpr int kGapW     = 10;

// Which chip index sits under localX, or -1.
int chipAt(const RVItem& it, int localX) {
    int cx = kChips;
    for (size_t i = 0; i < it.chips.size(); ++i) {
        if ((int)i == it.chipGap && i > 0) cx += kGapW;
        if (localX >= cx && localX < cx + kChipW) return (int)i;
        cx += kChipW;
    }
    return -1;
}
} // namespace playerRow

// The five positions, drawn by hand: a corner for top and bottom, the diagonal
// for mid, leaves for the jungle, a pair for the duo lane. This is the
// fallback. localIcon draws the real game icon when assets\icons is there, and
// a stripped install still says which lane is which.
void drawRoleMark(HDC dc, const std::string& role, int x, int y, int size, COLORREF c) {
    if (role.empty()) return;
    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HBRUSH br = CreateSolidBrush(c);
    int a = x + 2, b = y + 2, e = x + size - 2, f = y + size - 2;

    if (role == "TOP") {
        MoveToEx(dc, a, f, nullptr); LineTo(dc, a, b); LineTo(dc, e, b);
    } else if (role == "BOTTOM") {
        MoveToEx(dc, a, f, nullptr); LineTo(dc, e, f); LineTo(dc, e, b);
    } else if (role == "MIDDLE") {
        MoveToEx(dc, a, f, nullptr); LineTo(dc, e, b);
    } else if (role == "JUNGLE") {
        // Three strokes fanning out, like the camps between the lanes.
        int mx = (a + e) / 2;
        MoveToEx(dc, mx, f, nullptr); LineTo(dc, mx, b + 2);
        MoveToEx(dc, mx, y + size / 2, nullptr); LineTo(dc, a, b + 2);
        MoveToEx(dc, mx, y + size / 2, nullptr); LineTo(dc, e, b + 2);
    } else if (role == "UTILITY") {
        RECT r1{a, y + size / 2 - 1, a + 5, y + size / 2 + 4};
        RECT r2{e - 5, y + size / 2 - 4, e, y + size / 2 + 1};
        FillRect(dc, &r1, br);
        FillRect(dc, &r2, br);
        MoveToEx(dc, a + 5, y + size / 2 + 1, nullptr); LineTo(dc, e - 5, y + size / 2 - 1);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    DeleteObject(br);
}

// Left margin of every row. The painter starts here and the tip hit test
// measures from here, so the two can never drift apart.
constexpr int kRowOriginX = 16;

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

// The tip of the column under the pointer. Returns an empty tip when that
// column has nothing to say. chip carries the chip index so the caller can
// tell one hover from another without comparing whole tips.
// localX is measured from the row origin, which is where the painter starts
// drawing: kRowOriginX plus the indent of that item. Passing a window x with a
// constant subtracted shifts every zone of an indented row.
const RVTip* tipUnder(const RVData* d, int row, const RVItem& it, int localX, int& chip) {
    chip = -1;
    if (it.kind != RVKind::PlayerRow) return nullptr;
    auto place = d->placeLeft.find(row);
    if (place != d->placeLeft.end() && localX >= place->second) return &it.tipPlace;

    int c = playerRow::chipAt(it, localX);
    if (c >= 0 && c < (int)it.chipTips.size()) {
        chip = c;
        return &it.chipTips[c];
    }
    if (localX >= playerRow::kTier && localX < playerRow::kTier + playerRow::kTierW)
        return &it.tipTier;
    if (localX >= playerRow::kFarm && localX < playerRow::kFarm + playerRow::kFarmW)
        return &it.tipFarm;
    if (localX >= playerRow::kKda && localX < playerRow::kKda + playerRow::kKdaW)
        return &it.tipKda;
    if (localX >= playerRow::kPortrait && localX < playerRow::kName + playerRow::kNameW)
        return &it.tipName;
    return nullptr;
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

    // The place zones are measured while painting, so they belong to this
    // frame only. Keeping the old ones lets a row inherit the zone of whatever
    // sat at its index before: after a shorter list replaces a longer one, a
    // stale entry makes tipUnder answer with an empty tipPlace and swallow
    // every other column of that row.
    d->placeLeft.clear();

    int y = 14 - d->scroll;
    // The index travels with the item: the place zone of the tip is measured
    // here, per row, and the hit test looks it up by the same index.
    int rowIndex = -1;
    for (auto& it : d->items) {
        ++rowIndex;
        int h = measureItem(dc, it, W);
        if (y + h >= 0 && y <= H) {
            int x = kRowOriginX + it.indent;
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

                    // The role, between the portrait and the name.
                    if (!it.role.empty()) {
                        HBITMAP roleBmp = localIcon("lane-" + it.role);
                        if (roleBmp)
                            drawBitmap(dc, roleBmp, x + playerRow::kRole, y + 6, 18);
                        else
                            drawRoleMark(dc, it.role, x + playerRow::kRole, y + 8, 16,
                                         theme::kDim);
                    }

                    // The header row names the columns with the game's own
                    // marks: sword for K/D/A, minion for cs, coin for gold.
                    if (it.color == theme::kBorder) {
                        struct { const char* icon; int at; } marks[] = {
                            {"stat-kda",   playerRow::kKda - 22},
                            {"stat-cs",    playerRow::kFarm - 22},
                        };
                        for (auto& m : marks)
                            if (HBITMAP b = localIcon(m.icon))
                                drawBitmap(dc, b, x + m.at, y + 8, 16);
                    }

                    SelectObject(dc, theme::small_());
                    SetTextColor(dc, theme::kText);
                    RECT nameRc{x + playerRow::kName, y + 2,
                                x + playerRow::kName + playerRow::kNameW, y + 32};
                    DrawTextW(dc, it.text.c_str(), -1, &nameRc,
                              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    // The rank badge. Its own column, always filled: an empty
                    // slot reads as a bug, the unranked emblem reads as an
                    // answer.
                    if (it.kind == RVKind::PlayerRow && it.color != theme::kBorder) {
                        std::string emblem = it.tier.empty() ? "UNRANKED" : it.tier;
                        if (HBITMAP badge = localIcon("tier-" + emblem)) {
                            int bx = x + playerRow::kTier, by = y + 2;
                            drawBitmap(dc, badge, bx, by, 30);
                            // A rank nobody has asked for yet is not the same
                            // answer as "this player is unranked". The dimmed
                            // badge says "unknown"; the solid one says "none".
                            if (!it.tierRead) {
                                HDC veil = CreateCompatibleDC(dc);
                                HBITMAP vb = CreateCompatibleBitmap(dc, 30, 30);
                                HGDIOBJ ov = SelectObject(veil, vb);
                                RECT vr{0, 0, 30, 30};
                                HBRUSH vbr = CreateSolidBrush(theme::kCard);
                                FillRect(veil, &vr, vbr);
                                DeleteObject(vbr);
                                BLENDFUNCTION bf{AC_SRC_OVER, 0, 165, 0};
                                AlphaBlend(dc, bx, by, 30, 30, veil, 0, 0, 30, 30, bf);
                                SelectObject(veil, ov);
                                DeleteObject(vb);
                                DeleteDC(veil);
                            }
                        }
                    }

                    SetTextColor(dc, theme::kDim);
                    RECT kdaRc{x + playerRow::kKda, y + 2,
                               x + playerRow::kKda + playerRow::kKdaW, y + 32};
                    DrawTextW(dc, it.kda.c_str(), -1, &kdaRc,
                              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                    RECT farmRc{x + playerRow::kFarm, y + 2,
                                x + playerRow::kFarm + playerRow::kFarmW, y + 32};
                    DrawTextW(dc, it.farm.c_str(), -1, &farmRc,
                              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    int cx = x + playerRow::kChips;
                    for (size_t ci = 0; ci < it.chips.size(); ++ci) {
                        if ((int)ci == it.chipGap && ci > 0) cx += playerRow::kGapW;
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
                        cx += playerRow::kChipW;
                    }

                    // The place and the title, right-aligned. A percentage
                    // alone did not read at a glance; a position does.
                    int rightEdge = W - 16;
                    if (!it.right.empty()) {
                        SelectObject(dc, theme::small_());
                        SetTextColor(dc, theme::kDim);
                        SIZE sz;
                        GetTextExtentPoint32W(dc, it.right.c_str(), (int)it.right.size(), &sz);
                        TextOutW(dc, rightEdge - sz.cx, y + 9, it.right.c_str(),
                                 (int)it.right.size());
                        rightEdge -= sz.cx + 14;
                    }
                    if (!it.title2.empty()) {
                        SelectObject(dc, theme::small_());
                        SIZE sz;
                        GetTextExtentPoint32W(dc, it.title2.c_str(), (int)it.title2.size(), &sz);
                        RECT pill{rightEdge - sz.cx - 14, y + 6, rightEdge, y + 28};
                        HBRUSH pb = CreateSolidBrush(theme::kBg);
                        FillRect(dc, &pill, pb);
                        DeleteObject(pb);
                        SetTextColor(dc, it.color ? it.color : theme::kAccent);
                        DrawTextW(dc, it.title2.c_str(), -1, &pill,
                                  DT_SINGLELINE | DT_VCENTER | DT_CENTER);
                        rightEdge = pill.left - 12;
                    }
                    if (!it.place.empty()) {
                        SelectObject(dc, theme::body());
                        SIZE sz;
                        GetTextExtentPoint32W(dc, it.place.c_str(), (int)it.place.size(), &sz);
                        SetTextColor(dc, theme::kText);
                        TextOutW(dc, rightEdge - sz.cx, y + 6, it.place.c_str(),
                                 (int)it.place.size());
                        // The podium tip belongs to this box and nowhere else.
                        d->placeLeft[rowIndex] = rightEdge - sz.cx - 6;
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
        case WM_MOUSEMOVE: {
            if (!d) return 0;
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int idx = rvHitTest(hwnd, d, p.y);
            // Ask for WM_MOUSELEAVE once per entry, so the tip dies when the
            // pointer leaves the view instead of hanging over the desktop.
            if (!d->trackingLeave) {
                TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                d->trackingLeave = true;
            }
            int chip = -1;
            const RVTip* tip = nullptr;
            if (idx >= 0 && idx < (int)d->items.size()) {
                const RVItem& hovered = d->items[idx];
                tip = tipUnder(d, idx, hovered, p.x - (kRowOriginX + hovered.indent), chip);
            }
            if (idx == d->hoverIndex && chip == d->hoverChip) return 0;
            d->hoverIndex = idx;
            d->hoverChip = chip;
            if (!tip || tip->title.empty()) {
                tipHide();
                return 0;
            }
            POINT screen = p;
            ClientToScreen(hwnd, &screen);
            tipShow(hwnd, screen, *tip);
            return 0;
        }
        case WM_MOUSELEAVE: {
            if (d) {
                d->trackingLeave = false;
                d->hoverIndex = -1;
                d->hoverChip = -1;
            }
            tipHide();
            return 0;
        }
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
    tipHide();
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
