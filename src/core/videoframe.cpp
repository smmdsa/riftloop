#include "core/videoframe.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wincodec.h>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "windowscodecs")

namespace rl {

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

struct MfSession {
    bool ok = false;
    MfSession() { ok = SUCCEEDED(MFStartup(MF_VERSION)); }
    ~MfSession() { if (ok) MFShutdown(); }
};

constexpr int64_t kHns = 10'000'000;

// Nearest-neighbour scale into a top-down 32bpp DIB. A thumbnail does not earn
// a better filter, and this keeps the module free of extra dependencies.
HBITMAP makeThumb(const BYTE* src, LONG srcStride, UINT32 srcW, UINT32 srcH, int dstW, int dstH) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = dstW;
    bi.bmiHeader.biHeight = -dstH;          // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) return nullptr;

    auto* out = (BYTE*)bits;
    for (int y = 0; y < dstH; ++y) {
        UINT32 sy = (UINT32)((int64_t)y * srcH / dstH);
        if (sy >= srcH) sy = srcH - 1;
        const BYTE* srcRow = src + (int64_t)sy * srcStride;
        BYTE* dstRow = out + (int64_t)y * dstW * 4;
        for (int x = 0; x < dstW; ++x) {
            UINT32 sx = (UINT32)((int64_t)x * srcW / dstW);
            if (sx >= srcW) sx = srcW - 1;
            const BYTE* px = srcRow + (int64_t)sx * 4;
            dstRow[x * 4 + 0] = px[0];
            dstRow[x * 4 + 1] = px[1];
            dstRow[x * 4 + 2] = px[2];
            dstRow[x * 4 + 3] = 255;
        }
    }
    return bmp;
}

} // namespace

HBITMAP grabFrame(const std::wstring& path, double atSec, int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
    MfSession mf;
    if (!mf.ok) return nullptr;

    Com<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(attrs.put(), 2))) return nullptr;
    // Advanced processing lets the reader scale for us. Doing the scaling here
    // means the output pitch is ours to predict; fighting the source pitch
    // produced a skewed image, because a decoded frame is padded to the
    // hardware's alignment, not to width * 4.
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);

    Com<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attrs.get(), reader.put())))
        return nullptr;
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    Com<IMFMediaType> rgb;
    if (FAILED(MFCreateMediaType(rgb.put()))) return nullptr;
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    // Ask for the thumbnail size directly.
    MFSetAttributeSize(rgb.get(), MF_MT_FRAME_SIZE, (UINT32)width, (UINT32)height);
    rgb->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    rgb->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                                           rgb.get())))
        return nullptr;

    Com<IMFMediaType> current;
    UINT32 w = 0, h = 0;
    if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, current.put())) ||
        FAILED(MFGetAttributeSize(current.get(), MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0)
        return nullptr;

    if (atSec > 0) {
        PROPVARIANT pos;
        InitPropVariantFromInt64((int64_t)(atSec * kHns), &pos);
        reader->SetCurrentPosition(GUID_NULL, pos);
        PropVariantClear(&pos);
    }

    // Row pitch. Never assume w*4: Media Foundation aligns rows, and RGB32 can
    // arrive bottom-up with a negative stride. Mixing Lock and Lock2D on the
    // same buffer produced a skewed image, so pick one path and stay on it.
    LONG defaultStride = 0;
    if (FAILED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&defaultStride)) ||
        defaultStride == 0) {
        LONG computed = 0;
        if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &computed)))
            defaultStride = computed;
        else
            defaultStride = (LONG)w * 4;
    }

    HBITMAP result = nullptr;
    for (int tries = 0; tries < 30 && !result; ++tries) {
        DWORD flags = 0;
        Com<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags,
                                      nullptr, sample.put())))
            break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;               // decoder still warming up

        Com<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(buffer.put()))) continue;

        Com<IMF2DBuffer> two;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(two.put())))) {
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            if (SUCCEEDED(two->Lock2D(&scan0, &pitch))) {
                if (pitch < 0) {
                    // Bottom-up: scan0 points at the last row.
                    result = makeThumb(scan0 + (int64_t)(h - 1) * pitch, -pitch, w, h, width,
                                       height);
                } else {
                    result = makeThumb(scan0, pitch, w, h, width, height);
                }
                two->Unlock2D();
            }
            continue;
        }

        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) continue;
        if (defaultStride < 0) {
            result = makeThumb(data + (int64_t)(h - 1) * defaultStride, -defaultStride, w, h,
                               width, height);
        } else {
            result = makeThumb(data, defaultStride, w, h, width, height);
        }
        buffer->Unlock();
    }
    return result;
}

bool saveBitmapPng(HBITMAP bmp, const std::wstring& path) {
    if (!bmp) return false;
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(factory.put()))))
        return false;
    Com<IWICBitmap> wic;
    if (FAILED(factory->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, wic.put())))
        return false;
    Com<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
        return false;
    Com<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache)))
        return false;
    Com<IWICBitmapFrameEncode> frame;
    if (FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) || FAILED(frame->Initialize(nullptr)))
        return false;
    if (FAILED(frame->WriteSource(wic.get(), nullptr)) || FAILED(frame->Commit())) return false;
    return SUCCEEDED(encoder->Commit());
}

} // namespace rl
