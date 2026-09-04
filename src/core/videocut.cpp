#include "core/videocut.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "propsys")

namespace rl {

namespace {

// Minimal COM pointer: this module is the only user and it needs no more.
template <class T>
struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** put() { return &p; }
    T* get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    void reset() { if (p) { p->Release(); p = nullptr; } }
};

struct MfSession {
    bool ok = false;
    MfSession() { ok = SUCCEEDED(MFStartup(MF_VERSION)); }
    ~MfSession() { if (ok) MFShutdown(); }
};

constexpr int64_t kHns = 10'000'000;   // 100-ns units per second

// Source reader that hands over compressed samples: no decoder is created, so
// the samples can be written straight into the new file.
bool openPassthroughReader(const std::wstring& src, Com<IMFSourceReader>& reader,
                           Com<IMFMediaType>& videoType, DWORD* streamIndex) {
    Com<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(attrs.put(), 1))) return false;
    // Without this the reader would insert a decoder for the video stream.
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE);
    if (FAILED(MFCreateSourceReaderFromURL(src.c_str(), attrs.get(), reader.put()))) return false;

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    *streamIndex = MF_SOURCE_READER_FIRST_VIDEO_STREAM;
    if (FAILED(reader->SetStreamSelection(*streamIndex, TRUE))) return false;
    if (FAILED(reader->GetNativeMediaType(*streamIndex, 0, videoType.put()))) return false;
    return true;
}

} // namespace

double mediaDurationSec(const std::wstring& path) {
    MfSession mf;
    if (!mf.ok) return -1;
    Com<IMFSourceReader> reader;
    Com<IMFMediaType> type;
    DWORD stream = 0;
    if (!openPassthroughReader(path, reader, type, &stream)) return -1;
    PROPVARIANT var;
    PropVariantInit(&var);
    double seconds = -1;
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                                   MF_PD_DURATION, &var))) {
        int64_t hns = 0;
        if (SUCCEEDED(PropVariantToInt64(var, &hns))) seconds = (double)hns / (double)kHns;
    }
    PropVariantClear(&var);
    return seconds;
}

KeyframeReport probeKeyframes(const std::wstring& path) {
    KeyframeReport r;
    MfSession mf;
    if (!mf.ok) return r;
    Com<IMFSourceReader> reader;
    Com<IMFMediaType> type;
    DWORD stream = 0;
    if (!openPassthroughReader(path, reader, type, &stream)) return r;
    r.durationSec = mediaDurationSec(path);

    int64_t lastKey = -1;
    double gapSum = 0;
    int gaps = 0;
    for (;;) {
        DWORD flags = 0;
        int64_t ts = 0;
        Com<IMFSample> sample;
        if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, &ts, sample.put()))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ++r.samples;
        UINT32 clean = 0;
        if (SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean) {
            ++r.keyframes;
            if (lastKey >= 0) {
                double gap = (double)(ts - lastKey) / (double)kHns;
                gapSum += gap;
                ++gaps;
                if (gap > r.maxGapSec) r.maxGapSec = gap;
            }
            lastKey = ts;
        }
    }
    if (gaps > 0) r.meanGapSec = gapSum / gaps;
    return r;
}

RecordingInfo readRecordingSidecar(const std::wstring& mp4Path) {
    RecordingInfo info;
    info.path = mp4Path;
    std::wstring sidecar = mp4Path + L".json";
    FILE* f = nullptr;
    if (_wfopen_s(&f, sidecar.c_str(), L"rb") != 0 || !f) return info;
    std::string text;
    char buf[512];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    try {
        auto j = nlohmann::json::parse(text);
        info.startGameTimeSec = j.value("start_game_time_sec", -1.0);
        info.gameStartOffsetSec = j.value("game_start_offset_sec", -1.0);
        info.startedAtMs = j.value("started_at_ms", (int64_t)0);
        info.valid = info.startGameTimeSec >= 0 && info.startedAtMs > 0;
    } catch (...) {}
    return info;
}

std::vector<RecordingInfo> listRecordings(const std::wstring& folder) {
    std::vector<RecordingInfo> out;
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) return out;
    for (auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != L".mp4") continue;
        RecordingInfo info = readRecordingSidecar(e.path().wstring());
        if (info.valid) out.push_back(std::move(info));
    }
    return out;
}

std::vector<RecordingInfo> recordingsFor(const std::vector<RecordingInfo>& recordings,
                                         int64_t gameCreationMs, int gameDurationSec) {
    // Recording starts after the match is created and before it ends. A minute
    // of slack covers the clock skew between the client and the local time.
    const int64_t slackMs = 60'000;
    int64_t from = gameCreationMs - slackMs;
    int64_t to = gameCreationMs + (int64_t)gameDurationSec * 1000 + slackMs;
    std::vector<RecordingInfo> out;
    for (auto& r : recordings)
        if (r.startedAtMs >= from && r.startedAtMs <= to) out.push_back(r);
    std::sort(out.begin(), out.end(),
              [](const RecordingInfo& a, const RecordingInfo& b) {
                  return a.startedAtMs < b.startedAtMs;
              });
    return out;
}

std::wstring pickRecordingFor(const std::vector<RecordingInfo>& recordings,
                              int64_t gameCreationMs, int gameDurationSec) {
    auto all = recordingsFor(recordings, gameCreationMs, gameDurationSec);
    return all.empty() ? std::wstring() : all.front().path;
}

double videoPositionSec(double startGameTimeSec, int64_t gameTimestampMs,
                        double gameStartOffsetSec) {
    if (gameStartOffsetSec < 0) gameStartOffsetSec = 0;   // unknown: assume same origin
    double apiTime = (double)gameTimestampMs / 1000.0 + gameStartOffsetSec;
    return apiTime - startGameTimeSec;
}

CutResult cutClips(const std::wstring& src, const std::vector<CutRequest>& cuts) {
    CutResult res;
    MfSession mf;
    if (!mf.ok) {
        res.error = "Media Foundation no arranco";
        return res;
    }
    if (cuts.empty()) return res;

    for (auto& cut : cuts) {
        Com<IMFSourceReader> reader;
        Com<IMFMediaType> videoType;
        DWORD stream = 0;
        if (!openPassthroughReader(src, reader, videoType, &stream)) {
            res.error = "no se pudo leer la grabacion";
            return res;
        }

        Com<IMFSinkWriter> writer;
        if (FAILED(MFCreateSinkWriterFromURL(cut.outPath.c_str(), nullptr, nullptr,
                                             writer.put())))
            continue;
        DWORD outStream = 0;
        // Same compressed type in and out: the muxer only re-wraps the samples.
        if (FAILED(writer->AddStream(videoType.get(), &outStream))) continue;
        if (FAILED(writer->SetInputMediaType(outStream, videoType.get(), nullptr))) continue;
        if (FAILED(writer->BeginWriting())) continue;

        // Seek. The reader lands on the keyframe at or before this position,
        // so the clip can start a little earlier than requested.
        PROPVARIANT pos;
        InitPropVariantFromInt64((int64_t)(cut.startSec * kHns), &pos);
        reader->SetCurrentPosition(GUID_NULL, pos);
        PropVariantClear(&pos);

        int64_t firstTs = -1;
        int64_t lengthHns = (int64_t)(cut.durationSec * kHns);
        bool wroteAny = false;
        for (;;) {
            DWORD flags = 0;
            int64_t ts = 0;
            Com<IMFSample> sample;
            if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, &ts, sample.put()))) break;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample) continue;               // gap or format change tick
            if (firstTs < 0) firstTs = ts;
            int64_t rel = ts - firstTs;
            if (rel >= lengthHns) break;
            sample->SetSampleTime(rel);
            if (SUCCEEDED(writer->WriteSample(outStream, sample.get()))) wroteAny = true;
        }
        writer->Finalize();
        if (wroteAny) ++res.written;
        else DeleteFileW(cut.outPath.c_str());   // an empty clip helps nobody
    }
    if (res.written == 0 && res.error.empty()) res.error = "ningun corte produjo video";
    return res;
}

} // namespace rl
