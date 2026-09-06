// RiftLoop.Capture: opt-in window recording, beta (PRD 9.10, 14.2).
// Windows.Graphics.Capture + Media Foundation H.264 (hardware when available).
// - records only one window (default: the League game window)
// - 30 FPS target, quality follows RF-REC-002 (~2.5 Mbps)
// - exits cleanly when the window closes or --seconds elapses
// - output: %LOCALAPPDATA%\RiftLoop\clips\<timestamp>.mp4
//
// Known beta limits (documented, RF-REC-004): no clip cutting yet, no
// resolution downscale (records at window size), no in-game audio.
#include "core/config.h"
#include "core/lcu.h"
#include "core/util.h"

#include <nlohmann/json.hpp>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <ctime>

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {

constexpr UINT32 kFps = 30;
constexpr UINT32 kBitrate = 2'500'000;   // RF-REC-002
constexpr int64_t kMaxBytes = 2LL * 1024 * 1024 * 1024;   // 2 GB cap (RF-REC-003)
// The game clock origin arrives seconds after the recording starts. Ask every
// two seconds, and stop after five minutes: the GameStart event never shows up
// that late, and a dead API must not cost one request every two seconds.
constexpr int64_t kOffsetRetryMs = 2'000;
constexpr int64_t kOffsetGiveUpMs = 5 * 60 * 1000;

struct CaptureApp {
    com_ptr<ID3D11Device> d3d;
    com_ptr<ID3D11DeviceContext> ctx;
    IDirect3DDevice winrtDevice{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};

    com_ptr<IMFSinkWriter> writer;
    DWORD streamIndex = 0;
    UINT32 width = 0, height = 0;
    std::atomic<bool> running{true};
    int64_t firstTimestamp = 0;
    int64_t lastWritten = 0;
    int64_t bytesEstimate = 0;
    HWND target = nullptr;
    std::wstring outPath;
    // Alignment data of the sidecar. The offset starts unknown and the watchdog
    // loop fills it in later. See writeSidecar.
    int64_t startedAtMs = 0;
    double startGameTimeSec = -1;
    double gameStartOffsetSec = -1;
};

CaptureApp* g = nullptr;

HWND findWindowByTitleSubstring(const std::wstring& needle) {
    struct Ctx { const std::wstring* needle; HWND found; } ctx{&needle, nullptr};
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        if (!IsWindowVisible(hwnd)) return TRUE;
        wchar_t title[256];
        GetWindowTextW(hwnd, title, 256);
        std::wstring t = title;
        if (!t.empty() && t.find(*c->needle) != std::wstring::npos) {
            c->found = hwnd;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.found;
}

// Writes the sidecar that clipmaker reads to align a cut. The file is written
// twice: once at the start, and once more when the game clock origin arrives.
// The second write must keep the first write's values, so both come from `g`.
void writeSidecar() {
    nlohmann::json meta;
    meta["start_game_time_sec"] = g->startGameTimeSec;
    meta["game_start_offset_sec"] = g->gameStartOffsetSec;
    meta["started_at_ms"] = g->startedAtMs;
    meta["fps"] = kFps;
    std::wstring sidecar = g->outPath + L".json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, sidecar.c_str(), L"wb") != 0 || !f) {
        std::printf("aviso: no se pudo escribir el sidecar de alineacion\n");
        return;
    }
    std::string text = meta.dump(2);
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

bool initSinkWriter() {
    std::wstring dir = (rl::util::dataDir() / "clips").wstring();
    CreateDirectoryW(dir.c_str(), nullptr);
    if (g->outPath.empty()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t name[64];
        swprintf_s(name, L"%04d%02d%02d_%02d%02d%02d.mp4", st.wYear, st.wMonth, st.wDay, st.wHour,
                   st.wMinute, st.wSecond);
        g->outPath = dir + L"\\" + name;
    }

    // Sidecar with the alignment data. Without the game clock at the moment
    // recording started, a cut cannot land on the right second (RF-REC-003).
    {
        g->startGameTimeSec = rl::liveGameTimeSec();
        // Origin of the match clock inside the API clock. The Riot timeline
        // counts from 0:00, the API from when the process started; the gap is
        // the loading screen plus the fountain wait. Measured on the recording
        // of LA2_1622009391: 57 s, read from two frames 600 s apart.
        //
        // This read almost always fails here. Capture starts the moment the
        // Agent sees InGame, and the API clock reads about 0.03 s then, before
        // the GameStart event exists. The watchdog loop asks again.
        g->gameStartOffsetSec = rl::liveGameStartOffsetSec();
        g->startedAtMs = (int64_t)_time64(nullptr) * 1000;
        writeSidecar();
        if (g->startGameTimeSec < 0)
            std::printf("aviso: sin reloj de partida; los cortes automaticos no podran "
                        "alinearse\n");
    }

    com_ptr<IMFAttributes> attrs;
    MFCreateAttributes(attrs.put(), 2);
    // Prefer the hardware encoder (RF-REC-002).
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);

    if (FAILED(MFCreateSinkWriterFromURL(g->outPath.c_str(), nullptr, attrs.get(),
                                         g->writer.put())))
        return false;

    com_ptr<IMFMediaType> out;
    MFCreateMediaType(out.put());
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, kBitrate);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out.get(), MF_MT_FRAME_SIZE, g->width, g->height);
    MFSetAttributeRatio(out.get(), MF_MT_FRAME_RATE, kFps, 1);
    MFSetAttributeRatio(out.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(g->writer->AddStream(out.get(), &g->streamIndex))) return false;

    com_ptr<IMFMediaType> in;
    MFCreateMediaType(in.put());
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(in.get(), MF_MT_FRAME_SIZE, g->width, g->height);
    MFSetAttributeRatio(in.get(), MF_MT_FRAME_RATE, kFps, 1);
    if (FAILED(g->writer->SetInputMediaType(g->streamIndex, in.get(), nullptr))) return false;

    return SUCCEEDED(g->writer->BeginWriting());
}

void writeFrame(ID3D11Texture2D* frameTex, int64_t timestamp100ns) {
    // Throttle to ~30 FPS.
    if (g->firstTimestamp == 0) g->firstTimestamp = timestamp100ns;
    int64_t rel = timestamp100ns - g->firstTimestamp;
    if (g->lastWritten != 0 && rel - g->lastWritten < 10'000'000 / kFps) return;

    D3D11_TEXTURE2D_DESC desc;
    frameTex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    com_ptr<ID3D11Texture2D> staging;
    if (FAILED(g->d3d->CreateTexture2D(&desc, nullptr, staging.put()))) return;
    g->ctx->CopyResource(staging.get(), frameTex);

    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(g->ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map))) return;

    const UINT32 rowBytes = g->width * 4;
    com_ptr<IMFMediaBuffer> buffer;
    if (SUCCEEDED(MFCreateMemoryBuffer(rowBytes * g->height, buffer.put()))) {
        BYTE* dst = nullptr;
        DWORD maxLen = 0;
        if (SUCCEEDED(buffer->Lock(&dst, &maxLen, nullptr))) {
            // RGB32 default layout is bottom-up: copy rows reversed.
            auto* src = (const BYTE*)map.pData;
            for (UINT32 y = 0; y < g->height; ++y)
                memcpy(dst + (size_t)(g->height - 1 - y) * rowBytes, src + (size_t)y * map.RowPitch,
                       rowBytes);
            buffer->Unlock();
            buffer->SetCurrentLength(rowBytes * g->height);

            com_ptr<IMFSample> sample;
            if (SUCCEEDED(MFCreateSample(sample.put()))) {
                sample->AddBuffer(buffer.get());
                sample->SetSampleTime(rel);
                sample->SetSampleDuration(10'000'000 / kFps);
                if (SUCCEEDED(g->writer->WriteSample(g->streamIndex, sample.get()))) {
                    g->lastWritten = rel;
                    g->bytesEstimate += kBitrate / 8 / kFps;
                    if (g->bytesEstimate > kMaxBytes) g->running = false;   // quota (RF-REC-003)
                }
            }
        }
    }
    g->ctx->Unmap(staging.get(), 0);
}

} // namespace

// An MP4 without its index is unreadable, so a recording that dies without
// finalizing is lost footage. This catches console close, logoff and shutdown
// and gives the writer a chance to close the file (RF-REC-004: degrade, never
// lose what was already recorded).
BOOL WINAPI onConsoleSignal(DWORD) {
    if (g) {
        g->running = false;
        // The main loop finalizes; give it a moment before the OS kills us.
        for (int i = 0; i < 40 && g->writer; ++i) Sleep(50);
    }
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(onConsoleSignal, TRUE);
    // Recording must never starve the game (PRD 15.3).
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    std::wstring windowTitle = L"League of Legends (TM) Client";   // the game window
    int seconds = 0;                 // 0 = until the window closes
    std::wstring outPath;            // empty = timestamped name
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--window" && i + 1 < argc) windowTitle = argv[++i];
        if (a == L"--seconds" && i + 1 < argc) seconds = _wtoi(argv[++i]);
        // --out names the file. Evidence clips need a stable name to link them
        // back to the finding that produced them.
        if (a == L"--out" && i + 1 < argc) outPath = argv[++i];
    }

    init_apartment(apartment_type::multi_threaded);
    if (FAILED(MFStartup(MF_VERSION))) {
        std::printf("error: MFStartup fallo\n");
        return 1;
    }
    if (!GraphicsCaptureSession::IsSupported()) {
        std::printf("error: Windows.Graphics.Capture no esta disponible en este sistema\n");
        return 1;
    }

    CaptureApp app;
    g = &app;
    app.outPath = outPath;

    app.target = findWindowByTitleSubstring(windowTitle);
    if (!app.target) {
        std::printf("error: no se encontro la ventana '%s'\n",
                    rl::util::narrow(windowTitle).c_str());
        return 1;
    }

    // D3D11 device shared with WinRT.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                 D3D11_SDK_VERSION, app.d3d.put(), nullptr, app.ctx.put()))) {
        std::printf("error: sin dispositivo D3D11\n");
        return 1;
    }
    com_ptr<IDXGIDevice> dxgi = app.d3d.as<IDXGIDevice>();
    com_ptr<::IInspectable> inspectable;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()))) {
        std::printf("error: CreateDirect3D11DeviceFromDXGIDevice fallo\n");
        return 1;
    }
    app.winrtDevice = inspectable.as<IDirect3DDevice>();

    // Capture item for the window.
    auto interop = get_activation_factory<GraphicsCaptureItem>().as<IGraphicsCaptureItemInterop>();
    if (FAILED(interop->CreateForWindow(app.target, guid_of<GraphicsCaptureItem>(),
                                        put_abi(app.item)))) {
        std::printf("error: CreateForWindow fallo (¿ventana protegida?)\n");
        return 1;
    }

    auto size = app.item.Size();
    app.width = (UINT32)(size.Width & ~1);       // encoder wants even dimensions
    app.height = (UINT32)(size.Height & ~1);
    if (app.width < 64 || app.height < 64) {
        std::printf("error: ventana demasiado pequeña\n");
        return 1;
    }
    if (!initSinkWriter()) {
        std::printf("error: no se pudo iniciar el encoder H.264\n");
        return 1;
    }

    app.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        app.winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
    app.session = app.pool.CreateCaptureSession(app.item);
    app.item.Closed([](auto&&, auto&&) { g->running = false; });
    app.pool.FrameArrived([](Direct3D11CaptureFramePool const& sender, auto&&) {
        auto frame = sender.TryGetNextFrame();
        if (!frame || !g->running) return;
        auto surface = frame.Surface();
        auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        com_ptr<ID3D11Texture2D> tex;
        if (SUCCEEDED(access->GetInterface(guid_of<ID3D11Texture2D>(), tex.put_void())))
            writeFrame(tex.get(), frame.SystemRelativeTime().count());
    });
    app.session.IsCursorCaptureEnabled(false);
    app.session.StartCapture();

    std::printf("grabando '%s' -> %s\n", rl::util::narrow(windowTitle).c_str(),
                rl::util::narrow(app.outPath).c_str());

    int64_t start = GetTickCount64();
    int64_t lastOffsetTry = 0;
    while (app.running) {
        Sleep(250);
        if (!IsWindow(app.target)) app.running = false;               // window closed
        if (seconds > 0 && GetTickCount64() - start > (int64_t)seconds * 1000)
            app.running = false;
        // The GameStart event does not exist yet when the recording starts, so
        // the read in initSinkWriter returns -1 and clipmaker refuses to cut.
        // Ask again until the event appears, then write the sidecar again.
        // Frames arrive on the frame pool threads, so a slow read here costs no
        // frame. It only delays the two checks above (RF-REC-003).
        const int64_t now = (int64_t)GetTickCount64();
        if (app.gameStartOffsetSec < 0 && now - start < kOffsetGiveUpMs &&
            now - lastOffsetTry >= kOffsetRetryMs) {
            lastOffsetTry = now;
            double off = rl::liveGameStartOffsetSec();
            if (off >= 0) {
                app.gameStartOffsetSec = off;
                writeSidecar();
                std::printf("origen del reloj de partida: %.3f s\n", off);
            }
        }
    }
    if (app.gameStartOffsetSec < 0)
        std::printf("aviso: el evento GameStart nunca llego; esta grabacion no se podra "
                    "cortar\n");

    app.session.Close();
    app.pool.Close();
    if (app.writer) {
        app.writer->Finalize();
        app.writer = nullptr;        // tells the signal handler the file is closed
    }
    MFShutdown();
    std::printf("grabacion finalizada: %s\n", rl::util::narrow(app.outPath).c_str());
    return 0;
}
