#include "core/podium.h"

#include <algorithm>
#include <map>

namespace rl {

namespace {

// The weights. They add up to 100 and they are shown to the reader, one line
// per factor, so the score is never a number out of nowhere.
constexpr int kWeightPlays   = 35;   // share of the team's kills and assists
constexpr int kWeightGold    = 25;   // gold per minute against the best of the game
constexpr int kWeightFarm    = 15;   // cs per minute against the best of the game
constexpr int kWeightAlive   = 15;   // deaths against the worst of the game
constexpr int kWeightWin     = 10;   // winning is the point of the game

// Kills and assists are not worth the same. A kill is the play; an assist is
// being there for it.
int playValue(const Participant& p) {
    return p.kills * 100 + p.assists * 40;
}

int pointsOf(double ratio, int weight) {
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    return (int)(ratio * weight + 0.5);
}

std::string oneDecimal(double v) {
    char buf[32];
    snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

} // namespace

const PodiumEntry* MatchPodium::byParticipant(int participantId) const {
    for (const auto& e : entries)
        if (e.participantId == participantId) return &e;
    return nullptr;
}

MatchPodium buildPodium(const MatchSummary& m) {
    MatchPodium out;
    if (m.participants.empty()) return out;

    double minutes = m.gameDurationSec > 0 ? m.gameDurationSec / 60.0 : 1.0;
    if (minutes < 1.0) minutes = 1.0;

    std::map<int, int> teamPlays;          // teamId -> plays of the whole side
    int bestGold = 0, bestCs = 0, worstDeaths = 0;
    for (const auto& p : m.participants) {
        teamPlays[p.teamId] += playValue(p);
        bestGold = std::max(bestGold, p.goldEarned);
        bestCs = std::max(bestCs, p.totalCs);
        worstDeaths = std::max(worstDeaths, p.deaths);
    }

    for (const auto& p : m.participants) {
        PodiumEntry e;
        e.participantId = p.participantId;

        int teamTotal = teamPlays[p.teamId];
        double share = teamTotal > 0 ? (double)playValue(p) / teamTotal : 0.0;
        e.teamSharePct = (int)(share * 100 + 0.5);

        PodiumFactor plays;
        plays.label = "Participacion en las jugadas de tu equipo";
        plays.detail = std::to_string(p.kills) + " asesinatos y " + std::to_string(p.assists) +
                       " asistencias: el " + std::to_string(e.teamSharePct) +
                       " % de las jugadas de su lado";
        plays.weight = kWeightPlays;
        plays.points = pointsOf(share, kWeightPlays);
        e.score += plays.points;
        e.factors.push_back(std::move(plays));

        PodiumFactor gold;
        gold.label = "Oro por minuto";
        gold.detail = oneDecimal(p.goldEarned / minutes) + " por minuto, contra " +
                      oneDecimal(bestGold / minutes) + " del mejor de la partida";
        gold.weight = kWeightGold;
        gold.points = pointsOf(bestGold > 0 ? (double)p.goldEarned / bestGold : 0.0, kWeightGold);
        e.score += gold.points;
        e.factors.push_back(std::move(gold));

        PodiumFactor farm;
        farm.label = "CS por minuto";
        farm.detail = oneDecimal(p.totalCs / minutes) + " por minuto, contra " +
                      oneDecimal(bestCs / minutes) + " del mejor de la partida";
        farm.weight = kWeightFarm;
        farm.points = pointsOf(bestCs > 0 ? (double)p.totalCs / bestCs : 0.0, kWeightFarm);
        e.score += farm.points;
        e.factors.push_back(std::move(farm));

        PodiumFactor alive;
        alive.label = "Tiempo con vida";
        alive.detail = std::to_string(p.deaths) + " muertes, contra " +
                       std::to_string(worstDeaths) + " del que mas murio";
        alive.weight = kWeightAlive;
        // Nobody died: everyone keeps the full weight instead of none.
        alive.points = worstDeaths > 0
                     ? pointsOf(1.0 - (double)p.deaths / worstDeaths, kWeightAlive)
                     : kWeightAlive;
        e.score += alive.points;
        e.factors.push_back(std::move(alive));

        PodiumFactor win;
        win.label = "Resultado";
        win.detail = p.win ? "gano la partida" : "perdio la partida";
        win.weight = kWeightWin;
        win.points = p.win ? kWeightWin : 0;
        e.score += win.points;
        e.factors.push_back(std::move(win));

        out.entries.push_back(std::move(e));
    }

    // Highest score first. A tie breaks on the plays share, then on the slot,
    // so the order never depends on the order the participants arrived in.
    std::sort(out.entries.begin(), out.entries.end(),
              [](const PodiumEntry& a, const PodiumEntry& b) {
                  if (a.score != b.score) return a.score > b.score;
                  if (a.teamSharePct != b.teamSharePct) return a.teamSharePct > b.teamSharePct;
                  return a.participantId < b.participantId;
              });
    for (size_t i = 0; i < out.entries.size(); ++i)
        out.entries[i].rank = (int)i + 1;

    // --- the titles ---------------------------------------------------------
    // One title per player, and one player per title. Every title is a thing
    // that player DID; none of them points at anybody as a cause (PRD 9.11).
    auto find = [&](int pid) -> PodiumEntry* {
        for (auto& e : out.entries)
            if (e.participantId == pid) return &e;
        return nullptr;
    };
    auto give = [&](int pid, const char* title, const std::string& why) {
        PodiumEntry* e = find(pid);
        if (e && e->title.empty()) {
            e->title = title;
            e->titleWhy = why;
        }
    };

    // 1. The top of the podium.
    out.entries[0].title = "EL SMURFER";
    out.entries[0].titleWhy = "el numero uno de los diez, con " +
                              std::to_string(out.entries[0].score) + " sobre 100";

    // 2. The best of the side that lost. It is a different achievement from
    //    winning, and it deserves its own name.
    for (const auto& e : out.entries) {
        const Participant* p = m.byId(e.participantId);
        if (p && !p->win) {
            give(e.participantId, "MVP-CARRY",
                 "el mejor del equipo que perdio, puesto " + std::to_string(e.rank) +
                 " de la partida");
            break;
        }
    }

    // 3. The rest, by what stands out. Each one goes to a single player, and
    //    only when the number is worth a name.
    auto leader = [&](auto pick) -> const Participant* {
        const Participant* best = nullptr;
        for (const auto& p : m.participants)
            if (!best || pick(p) > pick(*best)) best = &p;
        return best;
    };

    if (const Participant* p = leader([](const Participant& x) { return x.kills; }))
        if (p->kills >= 5)
            give(p->participantId, "EL VERDUGO",
                 std::to_string(p->kills) + " asesinatos, mas que nadie");

    if (const Participant* p = leader([](const Participant& x) { return x.assists; }))
        if (p->assists >= 8)
            give(p->participantId, "EL PEGAMENTO",
                 std::to_string(p->assists) + " asistencias, mas que nadie");

    if (const Participant* p = leader([](const Participant& x) { return x.totalCs; }))
        if (p->totalCs >= 100)
            give(p->participantId, "EL GRANJERO",
                 std::to_string(p->totalCs) + " subditos, mas que nadie");

    // Surviving a whole game is rare enough to have a name of its own.
    for (const auto& p : m.participants)
        if (p.deaths == 0 && m.gameDurationSec >= 15 * 60)
            give(p.participantId, "INTOCABLE", "cero muertes en toda la partida");

    if (const Participant* p = leader([](const Participant& x) { return x.goldEarned; }))
        give(p->participantId, "LA CAJA FUERTE",
             std::to_string(p->goldEarned) + " de oro, mas que nadie");

    return out;
}

} // namespace rl
