// Cutting evidence clips out of a finished recording (PRD 9.10 RF-REC-003).
//
// The recording is a full game. After the analysis names the moments that
// matter, each one becomes a short clip and the raw file goes away. Cuts copy
// the compressed samples as they are: no re-encode, so a cut costs seconds and
// loses no quality.
//
// A cut starts at the keyframe at or before the requested second, so a clip can
// begin slightly earlier than asked. The lead margin absorbs that.
#pragma once
#include <string>
#include <vector>

namespace rl {

struct CutRequest {
    double       startSec = 0;
    double       durationSec = 0;
    std::wstring outPath;
};

struct CutResult {
    int         written = 0;         // clips actually produced
    std::string error;               // "" when every cut succeeded
};

// Cuts every request out of src. A failing cut is skipped; the rest continue.
CutResult cutClips(const std::wstring& src, const std::vector<CutRequest>& cuts);

// Duration of a media file in seconds, or -1 when it cannot be read.
double mediaDurationSec(const std::wstring& path);

// How far apart the keyframes are. A cut can only start on a keyframe, so a
// long spacing means a clip that begins minutes before the moment it should
// show. Used to decide between copying samples and re-encoding.
struct KeyframeReport {
    double durationSec = 0;
    int    samples = 0;
    int    keyframes = 0;
    double meanGapSec = 0;
    double maxGapSec = 0;
};

KeyframeReport probeKeyframes(const std::wstring& path);

// ------------------------------------------------- aligning video and match

// Sidecar Capture writes next to a recording. Without startGameTimeSec the
// recording cannot be aligned with the match clock and no cut is attempted.
struct RecordingInfo {
    std::wstring path;
    double       startGameTimeSec = -1;   // API clock when recording began
    double       gameStartOffsetSec = -1; // API clock at match time 0:00
    int64_t      startedAtMs = 0;         // wall clock, epoch ms
    bool         valid = false;
};

RecordingInfo readRecordingSidecar(const std::wstring& mp4Path);

// Every recording in the folder that carries a readable sidecar.
std::vector<RecordingInfo> listRecordings(const std::wstring& folder);

// Recordings made during a given match, earliest first. More than one is
// normal: a crashed recording leaves a truncated file next to the good one,
// so the caller tries them in order until one can be read. Pure.
std::vector<RecordingInfo> recordingsFor(const std::vector<RecordingInfo>& recordings,
                                         int64_t gameCreationMs, int gameDurationSec);

// First candidate, or an empty path. Pure.
std::wstring pickRecordingFor(const std::vector<RecordingInfo>& recordings,
                              int64_t gameCreationMs, int gameDurationSec);

// Where a match moment sits inside the video. Timeline timestamps count from
// 0:00, so they are moved onto the API clock with gameStartOffsetSec before
// subtracting where the recording began. Negative means the recording started
// after the moment, so there is nothing to cut. Pure.
double videoPositionSec(double startGameTimeSec, int64_t gameTimestampMs,
                        double gameStartOffsetSec = 0);

} // namespace rl
