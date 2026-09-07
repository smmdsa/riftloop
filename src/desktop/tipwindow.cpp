// The explanation popup. It exists because a number with no origin is not
// evidence (PRD 3.3): the row shows the place, this window shows where the
// place came from.
#include "ui.h"

#include <algorithm>

namespace rlui {

namespace {

constexpr int kPadX = 14;
constexpr int kPadY = 12;
constexpr int kLineH = 22;
constexpr int kTitleH = 26;
constexpr int kSubH = 20;
constexpr int kBarW = 54;
constexpr int kMaxW = 460;

struct TipData {
    std::wstring title, subtitle, footer;
    std::vector<TipLine> lines;
    int width = 320;
    int height = 100;
    int footerH = 0;
};

HWND g_tip = nullptr;

TipData* dataOf(HWND h) {
    return (TipData*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

// Measures the box the content needs. The value column is right-aligned, so
// the width has to hold the widest label and the widest value at once.
void measure(HDC dc, TipData& d) {
    int labelW = 0, valueW = 0;
    HGDIOBJ old = SelectObject(dc, theme::small_());
    for (const auto& l : d.lines) {
        SIZE a{}, b{};
        GetTextExtentPoint32W(dc, l.label.c_str(), (int)l.label.size(), &a);
        GetTextExtentPoint32W(dc, l.value.c_str(), (int)l.value.size(), &b);
        labelW = std::max<int>(labelW, a.cx);
        valueW = std::max<int>(valueW, b.cx);
    }
    SelectObject(dc, theme::h2());
    SIZE t{};
    GetTextExtentPoint32W(dc, d.title.c_str(), (int)d.title.size(), &t);

    int body = labelW + 18 + valueW + (d.lines.empty() ? 0 : kBarW + 12);
    d.width = std::min(kMaxW, std::max({ 300, body + kPadX * 2, (int)t.cx + kPadX * 2 }));

    d.footerH = 0;
    if (!d.footer.empty()) {
        SelectObject(dc, theme::tiny());
        RECT fr{ 0, 0, d.width - kPadX * 2, 0 };
        DrawTextW(dc, d.footer.c_str(), -1, &fr, DT_CALCRECT | DT_WORDBREAK);
        d.footerH = fr.bottom + 10;
    }
    SelectObject(dc, old);

    d.height = kPadY * 2 + kTitleH + (d.subtitle.empty() ? 0 : kSubH) +
               (int)d.lines.size() * kLineH + d.footerH;
}

void paint(HWND hwnd, TipData& d) {
    PAINTSTRUCT ps;
    HDC winDc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(winDc);
    HBITMAP back = CreateCompatibleBitmap(winDc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, back);

    HBRUSH bg = CreateSolidBrush(theme::kCardHi);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);
    HBRUSH edge = CreateSolidBrush(theme::kAccent);
    FrameRect(dc, &rc, edge);
    DeleteObject(edge);
    SetBkMode(dc, TRANSPARENT);

    int y = kPadY;
    SelectObject(dc, theme::h2());
    SetTextColor(dc, theme::kAccent);
    RECT tr{ kPadX, y, rc.right - kPadX, y + kTitleH };
    DrawTextW(dc, d.title.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    y += kTitleH;

    if (!d.subtitle.empty()) {
        SelectObject(dc, theme::small_());
        SetTextColor(dc, theme::kDim);
        RECT sr{ kPadX, y, rc.right - kPadX, y + kSubH };
        DrawTextW(dc, d.subtitle.c_str(), -1, &sr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        y += kSubH;
    }

    SelectObject(dc, theme::small_());
    for (const auto& l : d.lines) {
        RECT lr{ kPadX, y, rc.right - kPadX, y + kLineH };
        SetTextColor(dc, theme::kText);
        DrawTextW(dc, l.label.c_str(), -1, &lr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        int rightEdge = rc.right - kPadX;
        if (l.bar >= 0) {
            // The bar says how much of this line's weight the player took.
            RECT track{ rightEdge - kBarW, y + kLineH / 2 - 3, rightEdge, y + kLineH / 2 + 3 };
            HBRUSH tb = CreateSolidBrush(theme::kBg);
            FillRect(dc, &track, tb);
            DeleteObject(tb);
            int fill = std::min(100, std::max(0, l.bar)) * kBarW / 100;
            if (fill > 0) {
                RECT bar{ track.left, track.top, track.left + fill, track.bottom };
                HBRUSH fb = CreateSolidBrush(theme::kAccent);
                FillRect(dc, &bar, fb);
                DeleteObject(fb);
            }
            rightEdge = track.left - 12;
        }
        SetTextColor(dc, theme::kDim);
        RECT vr{ kPadX, y, rightEdge, y + kLineH };
        DrawTextW(dc, l.value.c_str(), -1, &vr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        y += kLineH;
    }

    if (!d.footer.empty()) {
        SelectObject(dc, theme::tiny());
        SetTextColor(dc, theme::kDim);
        RECT fr{ kPadX, y + 6, rc.right - kPadX, rc.bottom - 4 };
        DrawTextW(dc, d.footer.c_str(), -1, &fr, DT_WORDBREAK);
    }

    BitBlt(winDc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(back);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK TipProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = dataOf(hwnd);
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)new TipData());
            return 0;
        case WM_DESTROY:
            delete d;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (g_tip == hwnd) g_tip = nullptr;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (d) paint(hwnd, *d);
            return 0;
        // The tip must never take the pointer: the row under it has to keep
        // receiving the moves that keep the tip alive.
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void registerTipWindow(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = TipProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"RiftLoopTip";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
}

void tipShow(HWND owner, POINT screenPt, const std::wstring& title,
             const std::wstring& subtitle, const std::vector<TipLine>& lines,
             const std::wstring& footer) {
    if (title.empty()) {
        tipHide();
        return;
    }
    if (!g_tip) {
        g_tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                L"RiftLoopTip", L"", WS_POPUP, 0, 0, 10, 10, owner, nullptr,
                                (HINSTANCE)GetWindowLongPtrW(owner, GWLP_HINSTANCE), nullptr);
        if (!g_tip) return;
    }
    auto* d = dataOf(g_tip);
    if (!d) return;
    d->title = title;
    d->subtitle = subtitle;
    d->lines = lines;
    d->footer = footer;

    HDC dc = GetDC(g_tip);
    measure(dc, *d);
    ReleaseDC(g_tip, dc);

    // Keep the whole box on the monitor the pointer is on.
    int x = screenPt.x + 18, y = screenPt.y + 18;
    HMONITOR mon = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(MONITORINFO) };
    if (GetMonitorInfoW(mon, &mi)) {
        if (x + d->width > mi.rcWork.right) x = screenPt.x - d->width - 18;
        if (y + d->height > mi.rcWork.bottom) y = mi.rcWork.bottom - d->height - 8;
        if (x < mi.rcWork.left) x = mi.rcWork.left + 8;
        if (y < mi.rcWork.top) y = mi.rcWork.top + 8;
    }
    SetWindowPos(g_tip, HWND_TOPMOST, x, y, d->width, d->height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_tip, nullptr, TRUE);
}

void tipHide() {
    if (g_tip) ShowWindow(g_tip, SW_HIDE);
}

} // namespace rlui
