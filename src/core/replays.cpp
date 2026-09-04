#include "core/replays.h"
#include "core/http.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>

namespace rl {

using nlohmann::json;

namespace {

constexpr int kReplayApiPort = 2999;

http::Options replayApiOptions() {
    http::Options opt;
    opt.ignoreCertErrors = true;     // local self-signed cert
    opt.timeoutMs = 2000;
    opt.headers["Content-Type"] = "application/json";
    return opt;
}

} // namespace

ReplayConfig parseReplayConfig(const std::string& text) {
    ReplayConfig c;
    if (text.empty()) return c;
    try {
        json j = json::parse(text);
        c.enabled = j.value("isReplaysEnabled", false);
        c.forMatchHistory = j.value("isReplaysForMatchHistoryEnabled", false);
        c.playingReplay = j.value("isPlayingReplay", false);
        c.playingGame = j.value("isPlayingGame", false);
        c.gameVersion = j.value("gameVersion", "");
    } catch (...) {}
    return c;
}

ReplayMetadata parseReplayMetadata(const std::string& text) {
    ReplayMetadata m;
    if (text.empty()) return m;
    try {
        json j = json::parse(text);
        // The client reports the state as a string; the exact spelling has
        // changed between builds, so match on substrings and default to
        // Unknown rather than guessing that the file is there.
        std::string state = j.value("state", j.value("downloadStatus", ""));
        for (auto& ch : state) ch = (char)tolower((unsigned char)ch);
        if (state.find("notdownload") != std::string::npos) m.state = ReplayState::NotDownloaded;
        else if (state.find("download") != std::string::npos) m.state = ReplayState::Downloading;
        else if (state.find("watch") != std::string::npos) m.state = ReplayState::Available;
        else if (state.find("found") != std::string::npos) m.state = ReplayState::Available;
        else if (state.find("lost") != std::string::npos) m.state = ReplayState::Lost;
        else if (state.find("incompatible") != std::string::npos) m.state = ReplayState::Lost;
        // downloadProgress is already a percentage when the download runs, and
        // an unrelated number when it does not (a live client returned
        // 4170411667 for a replay it had not downloaded). Anything outside
        // 0..100 is not a progress value, so report nothing.
        double raw = j.value("downloadProgress", -1.0);
        m.progressPct = (raw >= 0.0 && raw <= 100.0) ? (int)(raw + 0.5) : 0;
    } catch (...) {}
    return m;
}

int64_t gameIdOfMatch(const std::string& matchId) {
    size_t sep = matchId.rfind('_');
    if (sep == std::string::npos || sep + 1 >= matchId.size()) return 0;
    try {
        return std::stoll(matchId.substr(sep + 1));
    } catch (...) {
        return 0;
    }
}

ReplayConfig replayConfig(Lcu& lcu) {
    return parseReplayConfig(lcu.getRaw("/lol-replays/v1/configuration"));
}

ReplayMetadata replayMetadata(Lcu& lcu, int64_t gameId) {
    return parseReplayMetadata(lcu.getRaw("/lol-replays/v1/metadata/" + std::to_string(gameId)));
}

bool requestReplayDownload(Lcu& lcu, int64_t gameId) {
    if (gameId <= 0) return false;
    return lcu.postRaw("/lol-replays/v1/rofls/" + std::to_string(gameId) + "/download", "{}");
}

bool watchReplay(Lcu& lcu, int64_t gameId) {
    if (gameId <= 0) return false;
    return lcu.postRaw("/lol-replays/v1/rofls/" + std::to_string(gameId) + "/watch", "{}");
}

std::string replayFolder(Lcu& lcu) {
    std::string body = lcu.getRaw("/lol-replays/v1/rofls/path");
    if (body.size() >= 2 && body.front() == '"' && body.back() == '"')
        return body.substr(1, body.size() - 2);
    return body;
}

ReplayPlayback parsePlayback(const std::string& text) {
    ReplayPlayback p;
    if (text.empty()) return p;
    try {
        json j = json::parse(text);
        if (!j.contains("time")) return p;
        p.timeSec = j.value("time", 0.0);
        p.lengthSec = j.value("length", 0.0);
        p.speed = j.value("seeking", false) ? 0.0 : j.value("speed", 1.0);
        p.paused = j.value("paused", false);
        p.valid = true;
    } catch (...) {}
    return p;
}

ReplayPlayback replayPlayback() {
    auto r = http::get("127.0.0.1", kReplayApiPort, true, "/replay/playback", replayApiOptions());
    return r.status == 200 ? parsePlayback(r.body) : ReplayPlayback{};
}

bool seekReplay(double timeSec, double speed) {
    if (timeSec < 0) timeSec = 0;
    json body{{"time", timeSec}, {"paused", false}, {"speed", speed}};
    auto r = http::post("127.0.0.1", kReplayApiPort, true, "/replay/playback", body.dump(),
                        replayApiOptions());
    return r.status >= 200 && r.status < 300;
}

bool pauseReplay() {
    json body{{"paused", true}, {"speed", 0.0}};
    auto r = http::post("127.0.0.1", kReplayApiPort, true, "/replay/playback", body.dump(),
                        replayApiOptions());
    return r.status >= 200 && r.status < 300;
}

bool seekAndConfirm(double timeSec, double toleranceSec, double* observedSec) {
    if (observedSec) *observedSec = -1;
    if (!seekReplay(timeSec, 1.0)) return false;
    // The client needs a moment to jump; three reads cover a slow seek without
    // turning this into a busy wait.
    for (int attempt = 0; attempt < 3; ++attempt) {
        Sleep(1200);
        ReplayPlayback pb = replayPlayback();
        if (!pb.valid) continue;
        if (observedSec) *observedSec = pb.timeSec;
        double drift = pb.timeSec - timeSec;
        if (drift < 0) drift = -drift;
        if (drift <= toleranceSec) return true;
    }
    return false;
}

std::vector<ClipRequest> planClips(const AnalysisResult& analysis, int maxClips) {
    struct Candidate {
        const Evidence* ev;
        std::string detectorId;
    };
    std::vector<Candidate> all;
    for (auto& f : analysis.findings) {
        if (f.failures == 0) continue;          // nothing happened: nothing to show
        for (auto& ev : f.evidence)
            if (ev.gameTimestampMs > 0) all.push_back({&ev, f.detectorId});
    }
    std::sort(all.begin(), all.end(), [](const Candidate& a, const Candidate& b) {
        return a.ev->gameTimestampMs < b.ev->gameTimestampMs;
    });

    std::vector<ClipRequest> out;
    int64_t lastTs = -kClipMergeMs * 2;
    for (auto& c : all) {
        if ((int)out.size() >= maxClips) break;
        if (c.ev->gameTimestampMs - lastTs < kClipMergeMs) continue;   // same moment
        lastTs = c.ev->gameTimestampMs;
        ClipRequest r;
        r.evidenceId = c.ev->evidenceId;
        r.matchId = c.ev->matchId.empty() ? analysis.matchId : c.ev->matchId;
        r.detectorId = c.detectorId;
        r.gameTimestampMs = c.ev->gameTimestampMs;
        r.startMs = c.ev->gameTimestampMs > kClipLeadMs ? c.ev->gameTimestampMs - kClipLeadMs : 0;
        r.seconds = kClipSeconds;
        r.fileName = r.matchId + "_" + (r.evidenceId.empty() ? std::to_string(r.gameTimestampMs)
                                                             : r.evidenceId) + ".mp4";
        // A file name reaches the file system: keep it to safe characters
        // (PRD 16.2 asks for sanitized file names).
        for (auto& ch : r.fileName)
            if (!isalnum((unsigned char)ch) && ch != '.' && ch != '_' && ch != '-') ch = '_';
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace rl
