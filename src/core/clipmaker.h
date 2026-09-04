// Turning a finished recording into evidence clips (PRD 9.10 RF-REC-003).
//
// The analysis names the moments that matter; this cuts them out of the
// recording, links each clip to its evidence and drops the raw file. It lives
// in core so both the Analyzer (after an automatic analysis) and the Desktop
// (when the user asks) run exactly the same code.
#pragma once
#include "core/db.h"
#include "core/models.h"

#include <functional>
#include <string>
#include <vector>

namespace rl {

struct ClipProgress {
    int         step = 0;            // 1-based
    int         total = 0;
    std::string label;               // what is happening now, for the UI
};

struct ClipMakerResult {
    bool        ok = false;
    int         made = 0;
    int         planned = 0;
    bool        rawRemoved = false;
    int64_t     freedBytes = 0;
    std::string message;             // shown to the user as-is
    std::vector<std::string> clipFiles;
};

// True when a recording of that match exists and carries alignment data. The
// Desktop uses it to decide between cutting the recording (seconds) and opening
// the replay in the client (minutes).
bool hasRecordingFor(Db& db, const std::string& matchId);

// Cuts the clips. onProgress may be empty; it is called from the calling
// thread. Never throws.
ClipMakerResult makeClipsFromRecording(Db& db, const std::string& matchId,
                                       const std::function<void(ClipProgress)>& onProgress = {});

} // namespace rl
