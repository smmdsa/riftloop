// Replay-based evidence clips (PRD 9.10, 13.1).
//
// A clip does not need the game to have been recorded live: the client keeps
// the .rofl of a recent match. RiftLoop asks the client to download it, opens
// it, seeks to the timestamp of an evidence with the Replay API and records the
// game window with RiftLoop.Capture.
//
// Policy: every call here is a local action on the user's own match. It is
// opt-in and off by default (Config::clipsEnabled), it is audited, and it never
// simulates input. Driving the client's own highlight button would need
// keyboard simulation, which PRD 14.5 forbids.
#pragma once
#include "core/lcu.h"

#include <optional>
#include <string>
#include <vector>

namespace rl {

// ---------------------------------------------------------------- LCU replays

struct ReplayConfig {
    bool enabled = false;            // isReplaysEnabled
    bool forMatchHistory = false;    // isReplaysForMatchHistoryEnabled
    bool playingReplay = false;
    bool playingGame = false;
    std::string gameVersion;
};

enum class ReplayState { Unknown, NotDownloaded, Downloading, Available, Lost };

struct ReplayMetadata {
    ReplayState state = ReplayState::Unknown;
    int         progressPct = 0;
};

// Pure parsers, exposed for tests. Empty or malformed input degrades to a
// disabled config / Unknown state; it never throws.
ReplayConfig   parseReplayConfig(const std::string& json);
ReplayMetadata parseReplayMetadata(const std::string& json);

// gameId from a match id shaped PLATFORM_gameId. 0 when it does not parse.
int64_t gameIdOfMatch(const std::string& matchId);

ReplayConfig   replayConfig(Lcu& lcu);
ReplayMetadata replayMetadata(Lcu& lcu, int64_t gameId);
bool           requestReplayDownload(Lcu& lcu, int64_t gameId);
bool           watchReplay(Lcu& lcu, int64_t gameId);
std::string    replayFolder(Lcu& lcu);

// ------------------------------------------------- Replay API (game, :2999)

struct ReplayPlayback {
    bool   valid = false;            // false = no replay is playing
    double timeSec = 0;
    double lengthSec = 0;
    double speed = 1.0;
    bool   paused = false;
};

ReplayPlayback parsePlayback(const std::string& json);

// Reachable only while a replay plays. Both degrade to false/invalid.
ReplayPlayback replayPlayback();
bool           seekReplay(double timeSec, double speed);
bool           pauseReplay();

// Seeks and then reads the position back. A replay that ignored the seek would
// otherwise produce a clip of the wrong moment, which is worse than no clip
// (PRD 3.3: evidence or silence). Writes the position it actually reached to
// observedSec when that pointer is given.
bool seekAndConfirm(double timeSec, double toleranceSec, double* observedSec);

// ------------------------------------------------------------- clip planning

// RF-REC-003: a clip covers 10-20 s before and after the moment. These give
// 15 s of lead and 25 s in total, and two evidences closer than the merge
// window share one clip instead of recording the same seconds twice.
inline constexpr int64_t kClipLeadMs = 15'000;
inline constexpr int     kClipSeconds = 25;
inline constexpr int64_t kClipMergeMs = 20'000;

struct ClipRequest {
    std::string evidenceId;
    std::string matchId;
    std::string detectorId;
    int64_t     gameTimestampMs = 0;
    int64_t     startMs = 0;         // seek target: timestamp minus the lead
    int         seconds = kClipSeconds;
    std::string fileName;            // inside the clips folder
};

// Clips worth producing for an analysis, oldest first, at most maxClips
// (PRD 10.2 caps the first post-match view at three evidences). Pure.
std::vector<ClipRequest> planClips(const AnalysisResult& analysis, int maxClips = 3);

} // namespace rl
