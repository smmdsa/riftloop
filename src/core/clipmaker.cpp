#include "core/clipmaker.h"

#include "core/config.h"
#include "core/ingest.h"
#include "core/replays.h"
#include "core/util.h"
#include "core/videocut.h"

#include <nlohmann/json.hpp>

#include <filesystem>

namespace rl {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// The recording made during a match, with its alignment data.
struct Aligned {
    RecordingInfo info;
    bool          found = false;
};

Aligned findRecording(Db& db, const std::string& matchId, MatchSummary* summaryOut,
                      int* unreadable) {
    Aligned out;
    if (unreadable) *unreadable = 0;
    auto m = parseMatch(db.matchJson(matchId));
    if (!m) return out;
    if (summaryOut) *summaryOut = *m;
    auto all = listRecordings(util::clipsDir().wstring());
    // A crashed recording leaves a truncated MP4 with no index next to the good
    // one. Media Foundation cannot read it, so skip to the next candidate
    // instead of failing every cut.
    for (auto& candidate : recordingsFor(all, m->gameCreationMs, m->gameDurationSec)) {
        if (mediaDurationSec(candidate.path) <= 0) {
            if (unreadable) ++*unreadable;
            continue;
        }
        out.info = candidate;
        out.found = true;
        return out;
    }
    return out;
}

} // namespace

bool hasRecordingFor(Db& db, const std::string& matchId) {
    int ignored = 0;
    return findRecording(db, matchId, nullptr, &ignored).found;
}

ClipMakerResult makeClipsFromRecording(Db& db, const std::string& matchId,
                                       const std::function<void(ClipProgress)>& onProgress) {
    ClipMakerResult res;
    auto report = [&](int step, int total, const std::string& label) {
        if (onProgress) onProgress({step, total, label});
    };

    auto analysis = db.loadAnalysis(matchId);
    if (!analysis) {
        res.message = "Esa partida no esta analizada todavia.";
        return res;
    }
    MatchSummary summary;
    int unreadable = 0;
    Aligned rec = findRecording(db, matchId, &summary, &unreadable);
    if (!rec.found) {
        res.message = unreadable > 0
            ? "La grabacion de esa partida esta incompleta y no se puede leer (" +
                  std::to_string(unreadable) +
                  " archivo(s)). Suele pasar cuando la grabacion no se cerro bien."
            : "No hay grabacion alineada de esa partida.";
        return res;
    }

    auto plan = planClips(*analysis);
    res.planned = (int)plan.size();
    if (plan.empty()) {
        res.message = "Esa partida no tiene evidencias con instante: no hay nada que cortar.";
        return res;
    }

    double videoLen = mediaDurationSec(rec.info.path);
    if (rec.info.gameStartOffsetSec < 0) {
        // Recordings made before the offset was captured cannot be aligned:
        // the two clocks differ by the loading screen. Measured at 57 s on the
        // recording of LA2_1622009391, so a blind cut lands a minute early.
        res.message = "Esa grabacion no guarda el origen del reloj de partida, asi que los "
                      "cortes caerian desplazados. Se conserva sin tocar.";
        return res;
    }
    fs::path clipDir = util::clipDirFor(matchId);
    std::error_code ec;
    fs::create_directories(clipDir, ec);

    std::vector<CutRequest> cuts;
    std::vector<const ClipRequest*> kept;
    int skippedOutside = 0;
    for (auto& c : plan) {
        double pos = videoPositionSec(rec.info.startGameTimeSec, c.startMs,
                                     rec.info.gameStartOffsetSec);
        if (pos < 0 || (videoLen > 0 && pos > videoLen)) {
            ++skippedOutside;
            continue;
        }
        CutRequest cut;
        cut.startSec = pos;
        cut.durationSec = c.seconds;
        cut.outPath = (clipDir / c.fileName).wstring();
        cuts.push_back(cut);
        kept.push_back(&c);
    }
    if (cuts.empty()) {
        res.message = "Ningun momento cae dentro de la grabacion (" +
                      std::to_string(skippedOutside) + " fuera de rango).";
        return res;
    }

    // Cut one at a time so the caller can show real progress instead of a
    // spinner that means nothing.
    int total = (int)cuts.size();
    for (int i = 0; i < total; ++i) {
        report(i + 1, total,
               "Cortando el momento de " + util::formatGameClock(kept[i]->gameTimestampMs));
        CutResult one = cutClips(rec.info.path, {cuts[i]});
        if (one.written == 0) continue;
        ++res.made;
        res.clipFiles.push_back(kept[i]->fileName);
        for (auto& f : analysis->findings)
            for (auto& ev : f.evidence)
                if (ev.evidenceId == kept[i]->evidenceId) ev.clipFile = kept[i]->fileName;
    }

    if (res.made == 0) {
        res.message = "No se pudo cortar ningun clip; la grabacion se conserva.";
        return res;
    }
    db.saveAnalysis(*analysis);
    res.ok = true;

    // The raw file only goes when every planned cut produced a clip: a partial
    // result must not cost the user the footage the rest of it came from.
    bool complete = res.made == total && skippedOutside == 0;
    if (Config::load().keepFullRecording) {
        res.message = std::to_string(res.made) + " clips generados. Se conserva la partida "
                                                 "completa (activado en Ajustes).";
    } else if (!complete) {
        res.message = std::to_string(res.made) + " de " + std::to_string(res.planned) +
                      " clips. La grabacion se conserva para poder reintentar.";
    } else {
        report(total, total, "Borrando la grabacion completa");
        res.freedBytes = (int64_t)fs::file_size(rec.info.path, ec);
        fs::remove(rec.info.path, ec);
        fs::remove(rec.info.path + L".json", ec);
        res.rawRemoved = true;
        char buf[64];
        snprintf(buf, sizeof buf, "%.1f GB", (double)res.freedBytes / (1024.0 * 1024.0 * 1024.0));
        res.message = std::to_string(res.made) + " clips generados. Grabacion original "
                                                 "borrada, " + buf + " liberados.";
        db.audit("recording_reduced", json{{"match", matchId},
                                           {"clips", res.made},
                                           {"freed_bytes", res.freedBytes}}
                                          .dump());
    }
    return res;
}

} // namespace rl
