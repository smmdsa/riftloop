#include "heatview.h"
#include "ui.h"

#include <algorithm>

using namespace rl;

namespace rlui {

namespace {

// Which slice of the game the cloud shows. Lane phase and the rest of the game
// are different problems, so they are never mixed by accident (TASK-0020).
enum class Window { All, Early, Late };

struct HeatState {
    HeatViewData data;
    bool show[3] = { true, true, true };   // Death, Kill, Assist
    Window window = Window::All;
};

// Counts one layer under the current time window. The legend, the panel and
// the painter all ask this, so they can never disagree.
int countLayer(const HeatState& st, int layer);

HeatState* stateOf(HWND h) {
    return (HeatState*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

COLORREF colorOf(HeatKind k) {
    switch (k) {
        case HeatKind::Death:  return theme::kDanger;
        case HeatKind::Kill:   return theme::kGood;
        default:               return theme::kAccent;
    }
}

const wchar_t* labelOf(HeatKind k) {
    switch (k) {
        case HeatKind::Death:  return L"Muertes";
        case HeatKind::Kill:   return L"Asesinatos";
        default:               return L"Asistencias";
    }
}

// One toggle of the legend bar, and one of the three time buttons.
struct Chip {
    RECT rc{};
    int  layer = -1;        // 0..2 for a layer, -1 for a time button
    Window window = Window::All;
};

constexpr int kBarH = 34;
constexpr int kPanelW = 210;    // the summary column, right of the map

struct Layout {
    RECT map{};
    RECT panel{};
    int  side = 0;
    int  blob = 15;             // odd, so the blob has a centre pixel
    std::vector<Chip> chips;
};

Layout layoutOf(HWND hwnd, const HeatState& st, HDC measureDc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    Layout out;
    // The chip widths must not move when a count changes, or a click would
    // land on a different chip than the one the eye aimed at.
    (void)st;

    int x = 8;
    HGDIOBJ oldFont = SelectObject(measureDc, theme::small_());
    for (int i = 0; i < 3; ++i) {
        std::wstring text = std::wstring(labelOf((HeatKind)i)) + L" " + std::to_wstring(999);
        SIZE sz{};
        GetTextExtentPoint32W(measureDc, text.c_str(), (int)text.size(), &sz);
        Chip c;
        c.layer = i;
        c.rc = { x, 6, x + sz.cx + 28, 6 + 22 };
        out.chips.push_back(c);
        x = c.rc.right + 6;
    }
    x += 10;
    const wchar_t* times[3] = { L"Toda la partida", L"Antes del 14", L"Despues del 14" };
    for (int i = 0; i < 3; ++i) {
        SIZE sz{};
        GetTextExtentPoint32W(measureDc, times[i], (int)wcslen(times[i]), &sz);
        Chip c;
        c.window = (Window)i;
        c.rc = { x, 6, x + sz.cx + 16, 6 + 22 };
        out.chips.push_back(c);
        x = c.rc.right + 6;
    }
    SelectObject(measureDc, oldFont);

    // The map is square and takes the height it can get. What is left of the
    // width becomes the summary column, so no space sits empty.
    int availH = rc.bottom - kBarH - 8;
    int availW = rc.right - kPanelW - 24;
    out.side = std::max(0, std::min(availW, availH));
    out.map = { 8, kBarH, 8 + out.side, kBarH + out.side };
    out.panel = { out.map.right + 16, kBarH, rc.right - 8, rc.bottom };
    // A blob that does not grow with the map turns a cloud into noise.
    out.blob = std::max(11, out.side / 32) | 1;
    return out;
}

int countLayer(const HeatState& st, int layer) {
    return (int)std::count_if(st.data.points.begin(), st.data.points.end(),
                              [&](const HeatPoint& p) {
                                  if ((int)p.kind != layer) return false;
                                  if (st.window == Window::Early) return p.tsMs < kLanePhaseEndMs;
                                  if (st.window == Window::Late)  return p.tsMs >= kLanePhaseEndMs;
                                  return true;
                              });
}

bool inRect(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

// A soft round blob, premultiplied, so overlapping points add up into heat.
HBITMAP makeBlob(COLORREF c, int size) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) return nullptr;

    auto* px = (uint32_t*)bits;
    const double r = size / 2.0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            double dx = x - r + 0.5, dy = y - r + 0.5;
            double d = std::sqrt(dx * dx + dy * dy) / r;
            double a = d >= 1.0 ? 0.0 : (1.0 - d) * (1.0 - d);
            auto alpha = (uint32_t)(a * 255.0);
            // Premultiplied BGRA, which is what AlphaBlend expects.
            uint32_t b = (uint32_t)GetBValue(c) * alpha / 255;
            uint32_t g = (uint32_t)GetGValue(c) * alpha / 255;
            uint32_t rr = (uint32_t)GetRValue(c) * alpha / 255;
            px[y * size + x] = (alpha << 24) | (rr << 16) | (g << 8) | b;
        }
    }
    return bmp;
}

void line(HDC dc, HPEN pen, int x1, int y1, int x2, int y2) {
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
}

// Drawn when Data Dragon has not given us the minimap yet. Three lanes, the
// river and the two nexuses: enough to read a cloud, never an empty square.
void drawBoard(HDC dc, const RECT& m, int side) {
    HBRUSH bg = CreateSolidBrush(theme::kBg);
    FillRect(dc, &m, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(theme::kBorder);
    FrameRect(dc, &m, border);
    DeleteObject(border);

    auto px = [&](double f) { return m.left + (int)(f * side); };
    auto py = [&](double f) { return m.top + (int)(f * side); };

    HPEN lane = CreatePen(PS_SOLID, std::max(2, side / 90), theme::kBorder);
    // Top lane, then bottom lane: two right angles along the outer edges.
    line(dc, lane, px(0.12), py(0.86), px(0.12), py(0.13));
    line(dc, lane, px(0.12), py(0.13), px(0.86), py(0.13));
    line(dc, lane, px(0.14), py(0.88), px(0.87), py(0.88));
    line(dc, lane, px(0.87), py(0.88), px(0.87), py(0.15));
    // Mid lane, corner to corner.
    line(dc, lane, px(0.13), py(0.87), px(0.87), py(0.13));
    DeleteObject(lane);

    HPEN river = CreatePen(PS_SOLID, std::max(3, side / 60), RGB(38, 62, 92));
    line(dc, river, px(0.06), py(0.34), px(0.66), py(0.94));
    DeleteObject(river);

    auto nexus = [&](double fx, double fy, COLORREF c) {
        int r = std::max(4, side / 40);
        HBRUSH b = CreateSolidBrush(c);
        RECT n{ px(fx) - r, py(fy) - r, px(fx) + r, py(fy) + r };
        FillRect(dc, &n, b);
        DeleteObject(b);
    };
    nexus(0.10, 0.90, RGB(58, 110, 165));    // blue base, bottom left
    nexus(0.90, 0.10, RGB(150, 62, 62));     // red base, top right
}

void paint(HWND hwnd, HeatState& st) {
    PAINTSTRUCT ps;
    HDC winDc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(winDc);
    HBITMAP back = CreateCompatibleBitmap(winDc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, back);
    FillRect(dc, &rc, theme::cardBrush());
    SetBkMode(dc, TRANSPARENT);

    Layout lay = layoutOf(hwnd, st, dc);

    // --- the legend bar: three layers, then the three time windows ----------
    SelectObject(dc, theme::small_());
    for (size_t i = 0; i < lay.chips.size(); ++i) {
        const Chip& c = lay.chips[i];
        bool on = c.layer >= 0 ? st.show[c.layer] : st.window == c.window;
        HBRUSH fill = CreateSolidBrush(on ? theme::kCardHi : theme::kCard);
        FillRect(dc, &c.rc, fill);
        DeleteObject(fill);
        HBRUSH edge = CreateSolidBrush(theme::kBorder);
        FrameRect(dc, &c.rc, edge);
        DeleteObject(edge);

        RECT text = c.rc;
        if (c.layer >= 0) {
            // The dot says which colour this layer wears on the map.
            RECT dot{ c.rc.left + 8, (c.rc.top + c.rc.bottom) / 2 - 4,
                      c.rc.left + 16, (c.rc.top + c.rc.bottom) / 2 + 4 };
            HBRUSH d = CreateSolidBrush(colorOf((HeatKind)c.layer));
            FillRect(dc, &dot, d);
            DeleteObject(d);
            text.left += 22;

            std::wstring s = std::wstring(labelOf((HeatKind)c.layer)) + L" " +
                             std::to_wstring(countLayer(st, c.layer));
            SetTextColor(dc, on ? theme::kText : theme::kDim);
            DrawTextW(dc, s.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        } else {
            const wchar_t* times[3] = { L"Toda la partida", L"Antes del 14", L"Despues del 14" };
            SetTextColor(dc, on ? theme::kAccent : theme::kDim);
            DrawTextW(dc, times[(int)c.window], -1, &text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
    }

    // --- the map ------------------------------------------------------------
    if (!st.data.note.empty()) {
        RECT box = { rc.left + 24, kBarH + 24, rc.right - 24, rc.bottom - 24 };
        SetTextColor(dc, theme::kDim);
        SelectObject(dc, theme::body());
        DrawTextW(dc, st.data.note.c_str(), -1, &box, DT_WORDBREAK | DT_CENTER);
    } else if (lay.side > 32) {
        HBITMAP mapBmp = st.data.mapUrl.empty()
                       ? nullptr
                       : icons::get("map", "11", st.data.mapUrl);
        if (mapBmp) drawBitmap(dc, mapBmp, lay.map.left, lay.map.top, lay.side);
        else        drawBoard(dc, lay.map, lay.side);

        // The timeline origin is the bottom-left corner of the Rift, so the Y
        // axis flips here. Without the flip the whole cloud lands upside down.
        // The blobs are cached per colour and per size: the window resizes far
        // less often than it repaints.
        static HBITMAP blobs[3] = { nullptr, nullptr, nullptr };
        static int blobSize = 0;
        if (blobSize != lay.blob) {
            for (auto& b : blobs) { if (b) DeleteObject(b); b = nullptr; }
            blobSize = lay.blob;
        }
        HDC blobDc = CreateCompatibleDC(dc);
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 190, AC_SRC_ALPHA };
        for (int layer = 0; layer < 3; ++layer) {
            if (!st.show[layer]) continue;
            if (!blobs[layer]) blobs[layer] = makeBlob(colorOf((HeatKind)layer), lay.blob);
            if (!blobs[layer]) continue;
            HGDIOBJ oldBlob = SelectObject(blobDc, blobs[layer]);
            for (const auto& p : st.data.points) {
                if ((int)p.kind != layer) continue;
                if (st.window == Window::Early && p.tsMs >= kLanePhaseEndMs) continue;
                if (st.window == Window::Late && p.tsMs < kLanePhaseEndMs) continue;
                CanvasPoint cp = projectToCanvas(p.x, p.y, lay.side);
                int cx = lay.map.left + cp.x;
                int cy = lay.map.top + cp.y;
                AlphaBlend(dc, cx - lay.blob / 2, cy - lay.blob / 2, lay.blob, lay.blob,
                           blobDc, 0, 0, lay.blob, lay.blob, bf);
            }
            SelectObject(blobDc, oldBlob);
        }
        DeleteDC(blobDc);

        // The summary column. A cloud without its sample size says nothing
        // (PRD 3.3), so the match count leads and the split follows.
        int py = lay.panel.top + 4;
        RECT line = { lay.panel.left, py, lay.panel.right, py + 26 };
        SetTextColor(dc, theme::kText);
        SelectObject(dc, theme::h2());
        std::wstring head = std::to_wstring(st.data.matches) + L" partidas";
        DrawTextW(dc, head.c_str(), -1, &line, DT_SINGLELINE | DT_LEFT);
        py += 30;

        SelectObject(dc, theme::small_());
        SetTextColor(dc, theme::kDim);
        RECT sub = { lay.panel.left, py, lay.panel.right, py + 20 };
        DrawTextW(dc, L"Reparto por fase de partida", -1, &sub, DT_SINGLELINE | DT_LEFT);
        py += 28;

        for (int layer = 0; layer < 3; ++layer) {
            RECT dot{ lay.panel.left, py + 6, lay.panel.left + 8, py + 14 };
            HBRUSH d = CreateSolidBrush(colorOf((HeatKind)layer));
            FillRect(dc, &dot, d);
            DeleteObject(d);

            int early = 0, late = 0;
            for (const auto& p : st.data.points) {
                if ((int)p.kind != layer) continue;
                (p.tsMs < kLanePhaseEndMs ? early : late)++;
            }
            RECT name = { lay.panel.left + 16, py, lay.panel.right, py + 20 };
            SetTextColor(dc, st.show[layer] ? theme::kText : theme::kDim);
            DrawTextW(dc, labelOf((HeatKind)layer), -1, &name, DT_SINGLELINE | DT_LEFT);
            RECT split = { lay.panel.left + 16, py + 18, lay.panel.right, py + 38 };
            std::wstring s = std::to_wstring(early) + L" antes del 14  ·  " +
                             std::to_wstring(late) + L" despues";
            SetTextColor(dc, theme::kDim);
            SelectObject(dc, theme::tiny());
            DrawTextW(dc, s.c_str(), -1, &split, DT_SINGLELINE | DT_LEFT);
            SelectObject(dc, theme::small_());
            py += 46;
        }

        RECT foot = { lay.panel.left, py + 10, lay.panel.right, py + 90 };
        SetTextColor(dc, theme::kDim);
        SelectObject(dc, theme::tiny());
        DrawTextW(dc,
                  L"Un punto por evento, con la posicion que guarda la propia partida. "
                  L"Nada se descarga: sale de lo que ya esta en tu disco.",
                  -1, &foot, DT_WORDBREAK);
    }

    BitBlt(winDc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(back);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK HeatProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = stateOf(hwnd);
    switch (msg) {
        case WM_CREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)new HeatState());
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
            HDC dc = GetDC(hwnd);
            Layout lay = layoutOf(hwnd, *st, dc);
            ReleaseDC(hwnd, dc);
            for (const Chip& c : lay.chips) {
                if (!inRect(c.rc, p)) continue;
                if (c.layer >= 0) st->show[c.layer] = !st->show[c.layer];
                else              st->window = c.window;
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            }
            return 0;
        }
        case WM_SETCURSOR: {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            if (st && p.y < kBarH) {
                HDC dc = GetDC(hwnd);
                Layout lay = layoutOf(hwnd, *st, dc);
                ReleaseDC(hwnd, dc);
                for (const Chip& c : lay.chips) {
                    if (inRect(c.rc, p)) {
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

void registerHeatView(HINSTANCE inst) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = HeatProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"RiftLoopHeatmap";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
}

void hmSet(HWND view, HeatViewData data) {
    auto* st = stateOf(view);
    if (!st) return;
    st->data = std::move(data);      // the toggles keep their state on purpose
    InvalidateRect(view, nullptr, FALSE);
}

} // namespace rlui
