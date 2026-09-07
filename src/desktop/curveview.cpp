#include "curveview.h"
#include "ui.h"

#include <algorithm>
#include <string>

using namespace rl;

namespace rlui {

namespace {

constexpr int kBarH = 32;
constexpr int kPadL = 56;      // room for the Y labels
constexpr int kPadR = 16;
constexpr int kPadT = 12;
constexpr int kPadB = 28;      // room for the X labels

struct CurveState {
    MatchCurves data;
    int series = 0;            // Gold, Cs, Xp
    RECT tabs[kSeriesCount]{};
};

CurveState* stateOf(HWND h) {
    return (CurveState*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

std::wstring w(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

// Thousands as "12.3k". A gold axis in raw digits is unreadable at this size.
std::wstring shortNumber(int v) {
    if (v < 1000) return std::to_wstring(v);
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1fk", v / 1000.0);
    return buf;
}

void polyline(HDC dc, const std::vector<POINT>& pts, COLORREF color, int width) {
    if (pts.size() < 2) return;
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old = SelectObject(dc, pen);
    Polyline(dc, pts.data(), (int)pts.size());
    SelectObject(dc, old);
    DeleteObject(pen);
}

struct Plot {
    RECT rc{};
    int  maxMinute = 1;
    int  maxValue = 1;
    POINT at(int minute, int value) const {
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        return { rc.left + (int)((int64_t)minute * w / maxMinute),
                 rc.bottom - (int)((int64_t)value * h / maxValue) };
    }
};

std::vector<POINT> toScreen(const Plot& p, const Curve& c) {
    std::vector<POINT> out;
    out.reserve(c.points.size());
    for (const auto& pt : c.points) out.push_back(p.at(pt.minute, pt.value));
    return out;
}

void paint(HWND hwnd, CurveState& st) {
    PAINTSTRUCT ps;
    HDC winDc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(winDc);
    HBITMAP back = CreateCompatibleBitmap(winDc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, back);
    FillRect(dc, &rc, theme::cardBrush());
    SetBkMode(dc, TRANSPARENT);

    // --- the three tabs ------------------------------------------------------
    SelectObject(dc, theme::small_());
    int x = 8;
    for (int i = 0; i < kSeriesCount; ++i) {
        std::wstring name = w(seriesName((Series)i));
        SIZE sz{};
        GetTextExtentPoint32W(dc, name.c_str(), (int)name.size(), &sz);
        RECT tab = { x, 5, x + sz.cx + 24, 5 + 22 };
        st.tabs[i] = tab;
        HBRUSH fill = CreateSolidBrush(st.series == i ? theme::kCardHi : theme::kCard);
        FillRect(dc, &tab, fill);
        DeleteObject(fill);
        HBRUSH edge = CreateSolidBrush(theme::kBorder);
        FrameRect(dc, &tab, edge);
        DeleteObject(edge);
        SetTextColor(dc, st.series == i ? theme::kAccent : theme::kDim);
        DrawTextW(dc, name.c_str(), -1, &tab, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        x = tab.right + 6;
    }

    if (!st.data.ok) {
        RECT box = { 16, kBarH + 16, rc.right - 16, rc.bottom - 16 };
        SetTextColor(dc, theme::kDim);
        SelectObject(dc, theme::body());
        DrawTextW(dc, w(st.data.note).c_str(), -1, &box, DT_WORDBREAK);
        BitBlt(winDc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(back);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return;
    }

    const Curve& mine = st.data.user[st.series];
    const Curve& band = st.data.band[st.series].median;
    const Curve& lane = st.data.lane[st.series];

    Plot plot;
    plot.rc = { kPadL, kBarH + kPadT, rc.right - kPadR, rc.bottom - kPadB };
    // The X axis ends where this match ended. The median of the role runs over
    // longer games too, and letting it stretch the axis squashes the minutes
    // the reader came here for.
    plot.maxMinute = std::max(1, mine.points.empty() ? 1 : mine.points.back().minute);
    auto clipped = [&](const Curve& c) {
        Curve out;
        for (const auto& pt : c.points)
            if (pt.minute <= plot.maxMinute) out.points.push_back(pt);
        return out;
    };
    Curve bandCut = clipped(band);
    Curve laneCut = clipped(lane);
    plot.maxValue = std::max({ 1, mine.maxValue(), bandCut.maxValue(), laneCut.maxValue() });
    // Round the top up so the highest point does not touch the frame.
    plot.maxValue = plot.maxValue + plot.maxValue / 10;

    if (plot.rc.right <= plot.rc.left || plot.rc.bottom <= plot.rc.top) {
        BitBlt(winDc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(back);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return;
    }

    // --- grid and axis labels -----------------------------------------------
    HPEN grid = CreatePen(PS_SOLID, 1, theme::kBorder);
    HGDIOBJ oldPen = SelectObject(dc, grid);
    SelectObject(dc, theme::tiny());
    SetTextColor(dc, theme::kDim);
    for (int i = 0; i <= 4; ++i) {
        int y = plot.rc.bottom - (plot.rc.bottom - plot.rc.top) * i / 4;
        MoveToEx(dc, plot.rc.left, y, nullptr);
        LineTo(dc, plot.rc.right, y);
        std::wstring lab = shortNumber(plot.maxValue * i / 4);
        RECT t = { 4, y - 8, plot.rc.left - 6, y + 8 };
        DrawTextW(dc, lab.c_str(), -1, &t, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    }
    for (int m = 0; m <= plot.maxMinute; m += 5) {
        POINT p = plot.at(m, 0);
        MoveToEx(dc, p.x, plot.rc.top, nullptr);
        LineTo(dc, p.x, plot.rc.bottom);
        std::wstring lab = std::to_wstring(m);
        RECT t = { p.x - 16, plot.rc.bottom + 4, p.x + 16, plot.rc.bottom + 20 };
        DrawTextW(dc, lab.c_str(), -1, &t, DT_SINGLELINE | DT_CENTER);
    }
    SelectObject(dc, oldPen);
    DeleteObject(grid);

    // --- the evidence marks, under the curves so they never hide them --------
    if (!st.data.marks.empty()) {
        HPEN mark = CreatePen(PS_DOT, 1, theme::kWarn);
        HGDIOBJ old = SelectObject(dc, mark);
        for (int64_t ms : st.data.marks) {
            int minute = (int)(ms / 60000);
            if (minute > plot.maxMinute) continue;
            POINT p = plot.at(minute, 0);
            MoveToEx(dc, p.x, plot.rc.top, nullptr);
            LineTo(dc, p.x, plot.rc.bottom);
        }
        SelectObject(dc, old);
        DeleteObject(mark);
    }

    // --- the band, then the opponent, then the user -------------------------
    // The user's line is drawn last and thickest: it is the one being read.
    polyline(dc, toScreen(plot, bandCut), theme::kDim, 2);
    polyline(dc, toScreen(plot, laneCut), theme::kWarn, 1);
    polyline(dc, toScreen(plot, mine), theme::kAccent, 3);

    // --- the legend ----------------------------------------------------------
    SelectObject(dc, theme::tiny());
    int lx = plot.rc.left + 4;
    int ly = plot.rc.top + 2;
    auto legend = [&](COLORREF c, const std::wstring& text) {
        RECT sw{ lx, ly + 5, lx + 14, ly + 7 };
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &sw, b);
        DeleteObject(b);
        SIZE sz{};
        GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
        RECT t = { lx + 20, ly, lx + 20 + sz.cx, ly + 16 };
        SetTextColor(dc, theme::kDim);
        DrawTextW(dc, text.c_str(), -1, &t, DT_SINGLELINE | DT_LEFT);
        lx = t.right + 16;
    };
    legend(theme::kAccent, w(st.data.champion) + L" (tu)");
    if (st.data.band[st.series].samples >= 1)
        legend(theme::kDim, L"Tu mediana en " + w(st.data.role) + L" · " +
                            std::to_wstring(st.data.band[st.series].samples) + L" partidas");
    if (!st.data.laneChampion.empty())
        legend(theme::kWarn, w(st.data.laneChampion) + L" (rival de linea)");

    // The band can be missing on purpose. Say why, right where it would be.
    if (!st.data.note.empty()) {
        RECT n = { plot.rc.left + 4, plot.rc.top + 20, plot.rc.right - 4, plot.rc.top + 52 };
        SetTextColor(dc, theme::kWarn);
        DrawTextW(dc, w(st.data.note).c_str(), -1, &n, DT_WORDBREAK);
    }

    BitBlt(winDc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(back);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK CurveProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = stateOf(hwnd);
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)new CurveState());
            return 0;
        case WM_DESTROY:
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (st) paint(hwnd, *st);
            return 0;
        case WM_SIZE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            if (!st) return 0;
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            for (int i = 0; i < kSeriesCount; ++i) {
                const RECT& t = st->tabs[i];
                if (p.x >= t.left && p.x < t.right && p.y >= t.top && p.y < t.bottom) {
                    st->series = i;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
            }
            return 0;
        }
        case WM_SETCURSOR: {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            if (st && p.y < kBarH) {
                for (const RECT& t : st->tabs) {
                    if (p.x >= t.left && p.x < t.right && p.y >= t.top && p.y < t.bottom) {
                        SetCursor(LoadCursorW(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void registerCurveView(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = CurveProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"RiftLoopCurves";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
}

void cvSet(HWND view, MatchCurves curves) {
    auto* st = stateOf(view);
    if (!st) return;
    st->data = std::move(curves);
    InvalidateRect(view, nullptr, FALSE);
}

} // namespace rlui
