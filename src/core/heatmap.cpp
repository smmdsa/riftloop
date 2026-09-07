#include "core/heatmap.h"

#include "core/analysis.h"
#include "core/ingest.h"

#include <algorithm>

namespace rl {

int HeatMap::count(HeatKind k) const {
    return (int)std::count_if(points.begin(), points.end(),
                              [k](const HeatPoint& p) { return p.kind == k; });
}

int HeatMap::count(HeatKind k, bool early) const {
    return (int)std::count_if(points.begin(), points.end(), [k, early](const HeatPoint& p) {
        return p.kind == k && (p.tsMs < kLanePhaseEndMs) == early;
    });
}

CanvasPoint projectToCanvas(int riftX, int riftY, int side) {
    auto clamp = [](int v) { return v < kRiftMin ? kRiftMin : v > kRiftMax ? kRiftMax : v; };
    int x = (int)((int64_t)clamp(riftX) * side / kRiftMax);
    int y = side - (int)((int64_t)clamp(riftY) * side / kRiftMax);
    return CanvasPoint{ x, y };
}

void addMatchHeat(const Timeline& t, int userId, HeatMap& out) {
    if (userId <= 0) return;
    for (const auto& e : t.events) {
        if (e.type != TLType::ChampionKill) continue;
        if (e.posX < 0 || e.posY < 0) continue;   // the source gave no position

        HeatPoint p;
        p.x = e.posX;
        p.y = e.posY;
        p.tsMs = e.tsMs;
        p.matchId = t.matchId;

        if (e.victimId == userId) {
            p.kind = HeatKind::Death;
        } else if (e.participantId == userId) {
            p.kind = HeatKind::Kill;
        } else if (std::find(e.assistIds.begin(), e.assistIds.end(), userId) != e.assistIds.end()) {
            p.kind = HeatKind::Assist;
        } else {
            continue;                              // a fight the user was not in
        }
        out.points.push_back(std::move(p));
    }
}

HeatMap collectHeat(Db& db, const HeatQuery& q) {
    HeatMap out;
    int want = q.matches > 0 ? q.matches : 20;

    std::string puuid = resolveUserPuuid(db);
    if (puuid.empty()) {
        out.note = "No se sabe cual de los diez jugadores eres. Importa mas partidas "
                   "con --fetch-lcu o define tu Riot ID con --set-riot-id.";
        return out;
    }

    // Ask for more rows than needed: the role and champion filters drop some,
    // and a match without a timeline gives nothing to draw.
    int scan = q.role.empty() && q.champion.empty() ? want : want * 4;
    for (const auto& row : db.listMatches(scan)) {
        if (out.matchesRead >= want) break;
        if (!q.role.empty() && row.userRole != q.role) continue;
        if (!q.champion.empty() && row.userChampion != q.champion) continue;

        std::string tlRaw = db.timelineJson(row.matchId);
        if (tlRaw.empty()) { ++out.matchesSkipped; continue; }
        auto match = parseMatch(db.matchJson(row.matchId));
        if (!match) { ++out.matchesSkipped; continue; }
        const Participant* me = match->byPuuid(puuid);
        if (!me) { ++out.matchesSkipped; continue; }
        auto tl = parseTimeline(tlRaw, row.matchId);
        if (!tl) { ++out.matchesSkipped; continue; }

        addMatchHeat(*tl, me->participantId, out);
        ++out.matchesRead;
    }

    if (out.matchesRead == 0 && out.note.empty()) {
        out.note = q.role.empty() && q.champion.empty()
                 ? "No hay partidas importadas todavia. Pulsa Importar partidas."
                 : "Ninguna de tus partidas guardadas cumple ese filtro.";
    }
    return out;
}

} // namespace rl
