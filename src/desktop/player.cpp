#include "player.h"

#include "ui.h"

#include <mfapi.h>
#include <mfmediaengine.h>
#include <d3d11.h>

#include <atomic>
#include <string>

#include <windowsx.h>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "d3d11")

namespace rlui {

namespace {

template <class T>
struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** put() { return &p; }
    T* get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// The Media Engine reports readiness and errors through this callback.
class EngineNotify : public IMFMediaEngineNotify {
public:
    explicit EngineNotify(std::atomic<bool>* ready) : ready_(ready) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IMFMediaEngineNotify) {
            *out = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return n;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override {
        if (event == MF_MEDIA_ENGINE_EVENT_CANPLAY) ready_->store(true);
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) ready_->store(false);
        return S_OK;
    }

private:
    std::atomic<bool>* ready_;
    LONG refs_ = 1;
};

struct MfInit {
    bool ok = false;
    MfInit() { ok = SUCCEEDED(MFStartup(MF_VERSION)); }
    ~MfInit() { if (ok) MFShutdown(); }
};

} // namespace

struct VideoPlayer::Impl {
    MfInit mf;
    Com<IMFMediaEngine> engine;
    Com<ID3D11Device> device;
    HWND host = nullptr;
    std::atomic<bool> ready{false};
    bool playing = false;
};

VideoPlayer::VideoPlayer() : impl_(std::make_unique<Impl>()) {}
VideoPlayer::~VideoPlayer() { shutdown(); }

bool VideoPlayer::attach(HWND target) {
    if (!impl_->mf.ok || !target) return false;
    impl_->host = target;

    // A D3D11 device with video support lets the engine present into the HWND.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, impl_->device.put(), nullptr, nullptr)))
        return false;
    Com<ID3D10Multithread> mt;
    if (SUCCEEDED(impl_->device->QueryInterface(IID_PPV_ARGS(mt.put())))) mt->SetMultithreadProtected(TRUE);

    Com<IMFDXGIDeviceManager> manager;
    UINT token = 0;
    if (FAILED(MFCreateDXGIDeviceManager(&token, manager.put()))) return false;
    manager->ResetDevice(impl_->device.get(), token);

    Com<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(attrs.put(), 4))) return false;
    auto* notify = new EngineNotify(&impl_->ready);
    attrs->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
    attrs->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, manager.get());
    attrs->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND, (UINT64)target);
    attrs->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);

    Com<IMFMediaEngineClassFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.put())))) {
        notify->Release();
        return false;
    }
    HRESULT hr = factory->CreateInstance(0, attrs.get(), impl_->engine.put());
    notify->Release();
    return SUCCEEDED(hr);
}

bool VideoPlayer::load(const std::wstring& path) {
    if (!impl_->engine) return false;
    impl_->ready.store(false);
    impl_->playing = false;
    BSTR url = SysAllocString(path.c_str());
    HRESULT hr = impl_->engine->SetSource(url);
    SysFreeString(url);
    return SUCCEEDED(hr);
}

void VideoPlayer::play() {
    if (!impl_->engine) return;
    impl_->engine->Play();
    impl_->playing = true;
}

void VideoPlayer::pause() {
    if (!impl_->engine) return;
    impl_->engine->Pause();
    impl_->playing = false;
}

bool VideoPlayer::playing() const { return impl_->playing; }

// Stop means "back to the beginning, paused", which is what a stop button does
// in every player a user has seen. Shutting the engine down would make the
// control dead, so it stays alive.
void VideoPlayer::stop() {
    if (!impl_->engine) return;
    impl_->engine->Pause();
    impl_->engine->SetCurrentTime(0);
    impl_->playing = false;
}

void VideoPlayer::seek(double sec) {
    if (!impl_->engine) return;
    if (sec < 0) sec = 0;
    double d = durationSec();
    if (d > 0 && sec > d - 0.05) sec = d - 0.05;
    impl_->engine->SetCurrentTime(sec);
}

bool VideoPlayer::ready() const { return impl_->ready.load(); }

void VideoPlayer::shutdown() {
    if (!impl_->engine) return;
    impl_->engine->Pause();
    impl_->engine->Shutdown();
    impl_->playing = false;
}

void VideoPlayer::resize(int width, int height) {
    if (!impl_->engine) return;
    // UpdateVideoStream lives on the Ex interface, not on IMFMediaEngine.
    Com<IMFMediaEngineEx> ex;
    if (FAILED(impl_->engine->QueryInterface(IID_PPV_ARGS(ex.put())))) return;
    RECT rc{0, 0, width, height};
    MFVideoNormalizedRect src{0, 0, 1, 1};
    ex->UpdateVideoStream(&src, &rc, nullptr);
}

double VideoPlayer::positionSec() const {
    return impl_->engine ? impl_->engine->GetCurrentTime() : 0;
}

double VideoPlayer::durationSec() const {
    if (!impl_->engine) return 0;
    double d = impl_->engine->GetDuration();
    return (d != d) ? 0 : d;          // NaN before metadata arrives
}

namespace {

// What the host window needs to go full screen and come back exactly where it
// was. Kept on the window itself so the player stays a drop-in child control.
constexpr int kBarH = 44;
constexpr int kBtn = 34;
constexpr UINT kBarTimer = 1;

struct HostState {
    VideoPlayer* player = nullptr;
    HWND         parent = nullptr;
    HWND         bar = nullptr;      // control bar, child of the host
    RECT         rect{};             // position inside the parent
    LONG_PTR     style = 0;
    LONG_PTR     exStyle = 0;
    bool         full = false;
    bool         dragging = false;   // scrubbing the seek bar
};

HostState* stateOf(HWND wnd) {
    return wnd ? (HostState*)GetWindowLongPtrW(wnd, GWLP_USERDATA) : nullptr;
}

// Places the control bar. It is a sibling of the video window, never a child:
// the media engine presents through a DXGI swapchain that covers any child of
// the host, so a bar drawn inside would be invisible.
void placeBar(HWND wnd, HostState* st) {
    if (!st || !st->bar) return;
    if (st->full) {
        // Full screen: a topmost popup pinned to the bottom of the monitor.
        MONITORINFO mi{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(st->bar, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW(st->bar, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_NOACTIVATE);
        SetParent(st->bar, nullptr);
        SetWindowPos(st->bar, HWND_TOPMOST, mi.rcMonitor.left,
                     mi.rcMonitor.bottom - kBarH, mi.rcMonitor.right - mi.rcMonitor.left, kBarH,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        return;
    }
    // Windowed: right under the video, inside the same parent.
    RECT r;
    GetWindowRect(wnd, &r);
    HWND parent = GetParent(wnd);
    MapWindowPoints(nullptr, parent, (POINT*)&r, 2);
    SetWindowLongPtrW(st->bar, GWL_STYLE, WS_CHILD | WS_VISIBLE);
    SetWindowLongPtrW(st->bar, GWL_EXSTYLE, 0);
    SetParent(st->bar, parent);
    SetWindowPos(st->bar, HWND_TOP, r.left, r.bottom, r.right - r.left, kBarH,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void applySize(HWND wnd, HostState* st) {
    RECT rc;
    GetClientRect(wnd, &rc);
    if (st && st->player) st->player->resize(rc.right, rc.bottom);
    placeBar(wnd, st);
    InvalidateRect(wnd, nullptr, FALSE);
}

// ---------------------------------------------------------- control bar
//
// The media engine presents straight into the host HWND, so anything painted
// there ends up behind the video. The bar is therefore a real child window,
// which the compositor puts on top.


// Hit zones, left to right. The seek bar takes whatever is left in the middle.
enum class BarHit { None, PlayPause, Stop, Seek, Full };

struct BarLayout {
    RECT play{}, stop{}, seek{}, full{};
};

BarLayout barLayout(int width) {
    BarLayout b;
    int y = (kBarH - kBtn) / 2;
    b.play = {8, y, 8 + kBtn, y + kBtn};
    b.stop = {8 + kBtn + 4, y, 8 + kBtn * 2 + 4, y + kBtn};
    b.full = {width - 8 - kBtn, y, width - 8, y + kBtn};
    // Time text sits between the buttons and the seek bar.
    int seekLeft = b.stop.right + 96;
    int seekRight = b.full.left - 12;
    if (seekRight < seekLeft + 40) seekRight = seekLeft + 40;
    b.seek = {seekLeft, kBarH / 2 - 3, seekRight, kBarH / 2 + 3};
    return b;
}

bool inRect(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

std::wstring clockText(double sec) {
    if (sec < 0 || sec != sec) sec = 0;
    int total = (int)sec;
    wchar_t buf[32];
    swprintf_s(buf, L"%d:%02d", total / 60, total % 60);
    return buf;
}

void drawPlayIcon(HDC dc, const RECT& r, bool playing, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    if (playing) {
        RECT a{cx - 7, cy - 9, cx - 2, cy + 9};
        RECT b{cx + 2, cy - 9, cx + 7, cy + 9};
        FillRect(dc, &a, br);
        FillRect(dc, &b, br);
    } else {
        POINT tri[3] = {{cx - 6, cy - 9}, {cx - 6, cy + 9}, {cx + 9, cy}};
        HGDIOBJ oldB = SelectObject(dc, br);
        HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, tri, 3);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
    }
    DeleteObject(br);
}

void drawStopIcon(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    RECT sq{cx - 7, cy - 7, cx + 7, cy + 7};
    FillRect(dc, &sq, br);
    DeleteObject(br);
}

// Four corner brackets pointing out (enter) or in (leave), the shape every
// player uses for full screen.
void drawFullIcon(HDC dc, const RECT& r, bool isFull, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const int o = 8, i = 3;
    const int xs[4] = {-1, 1, -1, 1};
    const int ys[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; ++k) {
        int ox = cx + xs[k] * o, oy = cy + ys[k] * o;
        int ix = cx + xs[k] * i, iy = cy + ys[k] * i;
        if (isFull) {
            MoveToEx(dc, ox, iy, nullptr);
            LineTo(dc, ix, iy);
            LineTo(dc, ix, oy);
        } else {
            MoveToEx(dc, ix, oy, nullptr);
            LineTo(dc, ox, oy);
            LineTo(dc, ox, iy);
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK BarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

LRESULT CALLBACK PlayerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HostState* st = stateOf(hwnd);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDBLCLK:
            togglePlayerFullscreen(hwnd);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE && st && st->full) {
                togglePlayerFullscreen(hwnd);
                return 0;
            }
            if (wp == VK_F11) {
                togglePlayerFullscreen(hwnd);
                return 0;
            }
            if (wp == VK_SPACE && st && st->player) {
                if (st->player->playing()) st->player->pause();
                else st->player->play();
                return 0;
            }
            break;
        case WM_SIZE:
            applySize(hwnd, st);
            return 0;
        case WM_PAINT: {
            // The engine presents on its own; paint the letterbox behind it.
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH br = CreateSolidBrush(RGB(10, 12, 16));
            FillRect(dc, &rc, br);
            DeleteObject(br);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

namespace {

LRESULT CALLBACK BarProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND host = (HWND)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    HostState* st = stateOf(host);
    RECT rc;
    GetClientRect(hwnd, &rc);
    BarLayout lay = barLayout(rc.right);

    auto seekTo = [&](int x) {
        if (!st || !st->player) return;
        double dur = st->player->durationSec();
        if (dur <= 0) return;
        double f = (double)(x - lay.seek.left) / (double)(lay.seek.right - lay.seek.left);
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        st->player->seek(f * dur);
        InvalidateRect(hwnd, nullptr, FALSE);
    };

    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, kBarTimer, 250, nullptr);   // >= 250 ms (PRD 15.4)
            return 0;
        case WM_TIMER:
            if (st && st->player) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (!st || !st->player) return 0;
            if (inRect(lay.play, p)) {
                if (st->player->playing()) st->player->pause();
                else st->player->play();
            } else if (inRect(lay.stop, p)) {
                st->player->stop();
            } else if (inRect(lay.full, p)) {
                togglePlayerFullscreen(host);
            } else if (p.y >= lay.seek.top - 8 && p.y <= lay.seek.bottom + 8 &&
                       p.x >= lay.seek.left - 4 && p.x <= lay.seek.right + 4) {
                st->dragging = true;
                SetCapture(hwnd);
                seekTo(p.x);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (st && st->dragging) seekTo(GET_X_LPARAM(lp));
            return 0;
        case WM_LBUTTONUP:
            if (st && st->dragging) {
                st->dragging = false;
                ReleaseCapture();
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            return 0;                 // never let a double click reach the host
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC winDc = BeginPaint(hwnd, &ps);
            HDC dc = CreateCompatibleDC(winDc);
            HBITMAP back = CreateCompatibleBitmap(winDc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(dc, back);

            HBRUSH bg = CreateSolidBrush(RGB(14, 17, 24));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);

            const COLORREF fg = RGB(228, 232, 240);
            const COLORREF dim = RGB(120, 130, 150);
            const COLORREF accent = RGB(76, 194, 255);
            bool playing = st && st->player && st->player->playing();
            drawPlayIcon(dc, lay.play, playing, fg);
            drawStopIcon(dc, lay.stop, fg);
            drawFullIcon(dc, lay.full, st && st->full, fg);

            double pos = st && st->player ? st->player->positionSec() : 0;
            double dur = st && st->player ? st->player->durationSec() : 0;

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, dim);
            SelectObject(dc, theme::small_());
            std::wstring t = clockText(pos) + L" / " + clockText(dur);
            RECT tr{lay.stop.right + 10, 0, lay.seek.left - 6, kBarH};
            DrawTextW(dc, t.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            HBRUSH track = CreateSolidBrush(RGB(52, 60, 78));
            FillRect(dc, &lay.seek, track);
            DeleteObject(track);
            if (dur > 0) {
                double f = pos / dur;
                if (f < 0) f = 0;
                if (f > 1) f = 1;
                RECT done = lay.seek;
                done.right = lay.seek.left + (LONG)((lay.seek.right - lay.seek.left) * f);
                HBRUSH fill = CreateSolidBrush(accent);
                FillRect(dc, &done, fill);
                // Knob, so the bar reads as draggable.
                RECT knob{done.right - 5, lay.seek.top - 5, done.right + 5, lay.seek.bottom + 5};
                FillRect(dc, &knob, fill);
                DeleteObject(fill);
            }

            BitBlt(winDc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldBmp);
            DeleteObject(back);
            DeleteDC(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, kBarTimer);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void bindPlayerHost(HWND playerWnd, VideoPlayer* player) {
    if (!playerWnd) return;
    HostState* st = stateOf(playerWnd);
    if (!st) {
        st = new HostState();
        SetWindowLongPtrW(playerWnd, GWLP_USERDATA, (LONG_PTR)st);
    }
    st->player = player;
    if (!st->bar) {
        st->bar = CreateWindowW(L"RiftLoopPlayerBar", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, kBarH,
                                GetParent(playerWnd), nullptr, nullptr, nullptr);
        SetWindowLongPtrW(st->bar, GWLP_USERDATA, (LONG_PTR)playerWnd);
    }
    applySize(playerWnd, st);
}

void showPlayerBar(HWND playerWnd, bool visible) {
    HostState* st = stateOf(playerWnd);
    if (!st || !st->bar) return;
    ShowWindow(st->bar, visible ? SW_SHOW : SW_HIDE);
}

bool playerIsFullscreen(HWND playerWnd) {
    HostState* st = stateOf(playerWnd);
    return st && st->full;
}

void togglePlayerFullscreen(HWND playerWnd) {
    HostState* st = stateOf(playerWnd);
    if (!st) return;

    if (!st->full) {
        // Remember where it lives, then detach it into a borderless popup on
        // the monitor it is on. The media engine keeps presenting into the same
        // HWND, so nothing has to be reloaded.
        st->parent = GetParent(playerWnd);
        GetWindowRect(playerWnd, &st->rect);
        MapWindowPoints(nullptr, st->parent, (POINT*)&st->rect, 2);
        st->style = GetWindowLongPtrW(playerWnd, GWL_STYLE);
        st->exStyle = GetWindowLongPtrW(playerWnd, GWL_EXSTYLE);

        MONITORINFO mi{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(playerWnd, MONITOR_DEFAULTTONEAREST), &mi);

        SetParent(playerWnd, nullptr);
        SetWindowLongPtrW(playerWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW(playerWnd, GWL_EXSTYLE, WS_EX_TOPMOST);
        SetWindowPos(playerWnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        st->full = true;
        SetFocus(playerWnd);          // so Esc and space reach the window
    } else {
        SetWindowLongPtrW(playerWnd, GWL_STYLE, st->style);
        SetWindowLongPtrW(playerWnd, GWL_EXSTYLE, st->exStyle);
        SetParent(playerWnd, st->parent);
        SetWindowPos(playerWnd, HWND_TOP, st->rect.left, st->rect.top,
                     st->rect.right - st->rect.left, st->rect.bottom - st->rect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        st->full = false;
        SetFocus(st->parent);
    }
    applySize(playerWnd, st);
}

void registerPlayerView(HINSTANCE inst) {
    WNDCLASSW wc{};
    // Without CS_DBLCLKS the window never sees WM_LBUTTONDBLCLK.
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = PlayerProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"RiftLoopPlayer";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    WNDCLASSW bar{};
    bar.style = CS_DBLCLKS;
    bar.lpfnWndProc = BarProc;
    bar.hInstance = inst;
    bar.lpszClassName = L"RiftLoopPlayerBar";
    bar.hCursor = LoadCursorW(nullptr, IDC_HAND);
    RegisterClassW(&bar);
}

} // namespace rlui
