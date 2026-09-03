#include "core/analysis.h"
#include "core/detectors.h"
#include "core/ingest.h"
#include "core/missions.h"
#include "core/util.h"

#include <algorithm>
#include <map>

namespace rl {

namespace {

double confidenceWeight(const std::string& c) {
    if (c == "alta") return 1.0;
    if (c == "media") return 0.75;
    return 0.5;
}

} // namespace

std::string resolveUserPuuid(Db& db) {
    Profile p = db.loadProfile();
    if (!p.puuid.empty()) return p.puuid;

    // Single local user: the puuid that appears in every stored match.
    std::map<std::string, int> counts;
    auto rows = db.listMatches(30);
    for (auto& r : rows) {
        auto m = parseMatch(db.matchJson(r.matchId));
        if (!m) continue;
        for (auto& pa : m->participants) ++counts[pa.puuid];
    }
    std::string best;
    int bestN = 0;
    for (auto& [puuid, n] : counts)
        if (n > bestN) { bestN = n; best = puuid; }
    if (rows.size() >= 2 && bestN >= (int)rows.size()) {
        p.puuid = best;
        db.saveProfile(p);
        return best;
    }
    return rows.size() == 1 ? best : "";
}

std::optional<AnalysisResult> analyzeMatch(Db& db, const Ddragon* dd, const std::string& matchId) {
    auto match = parseMatch(db.matchJson(matchId));
    if (!match) return std::nullopt;
    std::string tlRaw = db.timelineJson(matchId);
    if (tlRaw.empty()) return std::nullopt;
    auto tl = parseTimeline(tlRaw, matchId);
    if (!tl) return std::nullopt;

    std::string puuid = resolveUserPuuid(db);
    const Participant* me = puuid.empty() ? nullptr : match->byPuuid(puuid);
    if (!me) return std::nullopt;

    DetectorInput in{*match, *tl, me->participantId, dd};
    AnalysisResult res;
    res.matchId = matchId;
    res.rulesetVersion = kRulesetVersion;
    res.strength = detectStrength(in);
    res.findings = runDetectors(in);

    // Priority within the match: severity x fail rate x confidence (PRD 12.1).
    std::sort(res.findings.begin(), res.findings.end(), [](const Finding& a, const Finding& b) {
        auto score = [](const Finding& f) {
            double failRate = f.opportunities ? (double)f.failures / f.opportunities : 0.0;
            return f.severity * failRate * confidenceWeight(f.confidence) *
                   detectorControllability(f.detectorId);
        };
        return score(a) > score(b);
    });

    res.limitations =
        "Analisis basado en Match Timeline; wave state, spacing e intencion no son observables. "
        "Un detector no equivale a causalidad (PRD 9.12).";

    db.saveAnalysis(res);
    db.audit("analysis", "{\"match\":\"" + matchId + "\",\"ruleset\":\"" + res.rulesetVersion + "\"}");

    // Feed the active mission with this match's opportunities.
    trackMatchForMission(db, res);
    return res;
}

int analyzePending(Db& db, const Ddragon* dd) {
    int done = 0;
    for (auto& id : db.pendingAnalysis())
        if (analyzeMatch(db, dd, id)) ++done;
    return done;
}

} // namespace rl
