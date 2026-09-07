#include "core/curves.h"

#include "core/analysis.h"
#include "core/ingest.h"
#include "core/meta.h"

#include <algorithm>
#include <map>

namespace rl {

namespace {

constexpr int64_t kMinuteMs = 60'000;

int valueOf(const TLFrame& f, int pid, Series s) {
    switch (s) {
        case Series::Gold: {
            auto it = f.totalGold.find(pid);
            return it == f.totalGold.end() ? 0 : it->second;
        }
        case Series::Cs:
            return f.cs(pid);
        default: {
            auto it = f.xp.find(pid);
            return it == f.xp.end() ? 0 : it->second;
        }
    }
}

// One curve per series for one participant. A frame the source never sent
// simply is not there: the curve stops, it does not fall to zero.
void fillCurves(const Timeline& t, int pid, Curve out[kSeriesCount]) {
    for (const auto& f : t.frames) {
        if (f.totalGold.find(pid) == f.totalGold.end()) continue;
        int minute = (int)(f.tsMs / kMinuteMs);
        for (int s = 0; s < kSeriesCount; ++s) {
            CurvePoint pt{ minute, valueOf(f, pid, (Series)s) };
            // The closing frame lands on the same whole minute as the one
            // before it: a game of 31:37 ends inside minute 31. Two points on
            // one X draw a vertical spike, so the later one replaces it.
            if (!out[s].points.empty() && out[s].points.back().minute == minute)
                out[s].points.back() = pt;
            else
                out[s].points.push_back(pt);
        }
    }
}

int median(std::vector<int>& v) {
    if (v.empty()) return 0;
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    int hi = v[mid];
    if (v.size() % 2 == 1) return hi;
    std::nth_element(v.begin(), v.begin() + mid - 1, v.end());
    return (v[mid - 1] + hi) / 2;
}

} // namespace

int Curve::maxValue() const {
    int m = 0;
    for (const auto& p : points) m = std::max(m, p.value);
    return m;
}

const char* seriesName(Series s) {
    switch (s) {
        case Series::Gold: return "Oro";
        case Series::Cs:   return "CS";
        default:           return "Experiencia";
    }
}

MatchCurves buildCurves(Db& db, const std::string& matchId, int bandMatches) {
    MatchCurves out;
    out.matchId = matchId;

    std::string puuid = resolveUserPuuid(db);
    if (puuid.empty()) {
        out.note = "No se sabe cual de los diez jugadores eres. Importa mas partidas "
                   "o define tu Riot ID en Perfil.";
        return out;
    }
    auto match = parseMatch(db.matchJson(matchId));
    if (!match) {
        out.note = "Esa partida no esta guardada. Abre Partidas y elige una de la lista.";
        return out;
    }
    const Participant* me = match->byPuuid(puuid);
    if (!me) {
        out.note = "No apareces en esa partida, asi que no hay curva tuya que dibujar.";
        return out;
    }
    auto tl = parseTimeline(db.timelineJson(matchId), matchId);
    if (!tl) {
        out.note = "Esa partida se importo sin timeline. Sin frames por minuto no hay curva.";
        return out;
    }

    out.role = me->position;
    out.champion = me->championName;
    out.durationSec = match->gameDurationSec;
    fillCurves(*tl, me->participantId, out.user);

    // The lane opponent is the enemy in the same position of this same match.
    // It is a game already played, never a scout of a hidden identity.
    for (const auto& p : match->participants) {
        if (p.teamId == me->teamId || p.position != me->position || me->position.empty()) continue;
        out.laneChampion = p.championName;
        fillCurves(*tl, p.participantId, out.lane);
        break;
    }

    if (auto a = db.loadAnalysis(matchId)) {
        for (const auto& f : a->findings)
            for (const auto& ev : f.evidence)
                out.marks.push_back(ev.gameTimestampMs);
        std::sort(out.marks.begin(), out.marks.end());
        out.marks.erase(std::unique(out.marks.begin(), out.marks.end()), out.marks.end());
    }

    // --- the band: the same role, the same player, the other matches --------
    std::map<int, std::vector<int>> byMinute[kSeriesCount];
    int used = 0;
    if (!out.role.empty()) {
        for (const auto& row : db.listMatches(bandMatches * 3)) {
            if (used >= bandMatches) break;
            if (row.matchId == matchId) continue;      // never its own reference
            if (row.userRole != out.role) continue;
            auto m = parseMatch(db.matchJson(row.matchId));
            if (!m) continue;
            const Participant* p = m->byPuuid(puuid);
            if (!p) continue;
            auto t = parseTimeline(db.timelineJson(row.matchId), row.matchId);
            if (!t) continue;
            Curve one[kSeriesCount];
            fillCurves(*t, p->participantId, one);
            for (int s = 0; s < kSeriesCount; ++s)
                for (const auto& pt : one[s].points)
                    byMinute[s][pt.minute].push_back(pt.value);
            ++used;
        }
    }

    if (used < kMetaMinSample) {
        out.note = out.role.empty()
                 ? "Esa partida no registra tu posicion, asi que no hay rol con el que comparar."
                 : "Solo " + std::to_string(used) + " partidas tuyas en " + out.role +
                   ". Hacen falta " + std::to_string(kMetaMinSample) +
                   " para una referencia; la curva se dibuja sin banda.";
    } else {
        for (int s = 0; s < kSeriesCount; ++s) {
            out.band[s].samples = used;
            for (auto& [minute, values] : byMinute[s]) {
                // A minute that only two games reached is not a median of the
                // role: it is the tail of the longest games.
                if ((int)values.size() < kMetaMinSample) continue;
                out.band[s].median.points.push_back({ minute, median(values) });
            }
            std::sort(out.band[s].median.points.begin(), out.band[s].median.points.end(),
                      [](const CurvePoint& a, const CurvePoint& b) { return a.minute < b.minute; });
            // Gold, cs and xp only ever go up. The median of a minute can still
            // fall, because the set of games that reach minute 28 is not the
            // set that reached 27. A falling reference reads as a loss that
            // never happened, so the curve is held at its running maximum.
            int running = 0;
            for (auto& pt : out.band[s].median.points) {
                running = std::max(running, pt.value);
                pt.value = running;
            }
        }
    }

    out.ok = true;
    return out;
}

} // namespace rl
