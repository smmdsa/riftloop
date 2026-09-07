#include "core/rivalrank.h"

#include "core/http.h"
#include "core/util.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <ctime>

using nlohmann::json;

namespace rl {

namespace {

// The official ladder, in order. An index on this scale is an official step,
// never a rating of our own (PRD 7).
const std::vector<std::string>& tiers() {
    static const std::vector<std::string> t = {
        "IRON", "BRONZE", "SILVER", "GOLD", "PLATINUM",
        "EMERALD", "DIAMOND", "MASTER", "GRANDMASTER", "CHALLENGER"
    };
    return t;
}

int divisionIndex(const std::string& d) {
    if (d == "IV") return 0;
    if (d == "III") return 1;
    if (d == "II") return 2;
    if (d == "I") return 3;
    return 0;
}

const char* divisionName(int i) {
    switch (i) {
        case 3:  return "I";
        case 2:  return "II";
        case 1:  return "III";
        default: return "IV";
    }
}

// Position on the ladder: four steps per tier. Master and above have no
// divisions, so they sit on their first step.
int ladderStep(const RivalRank& r) {
    const auto& t = tiers();
    auto it = std::find(t.begin(), t.end(), r.tier);
    if (it == t.end()) return -1;
    return (int)(it - t.begin()) * 4 + divisionIndex(r.division);
}

std::string apiGet(const RiotApiConfig& c, const std::string& host, const std::string& path,
                   std::string* error) {
    http::Options opt;
    opt.headers["X-Riot-Token"] = c.apiKey;
    auto r = http::get(host, 443, true, path, opt);
    if (r.status == 200) return r.body;
    if (error) {
        *error = r.status == 0 ? ("red: " + r.error)
               : r.status == 403 ? "la clave de Riot no vale o caduco"
               : r.status == 429 ? "cuota agotada: espera un minuto"
               : ("Riot API status " + std::to_string(r.status));
    }
    return {};
}

} // namespace

std::string RivalRank::display() const {
    if (!cached()) return "Sin leer";
    if (!ranked()) return "Sin clasificar";
    std::string out = tier;
    if (!division.empty()) out += " " + division;
    out += " · " + std::to_string(leaguePoints) + " LP";
    return out;
}

const RivalRank* MatchRankView::byParticipant(int pid) const {
    for (const auto& r : rows)
        if (r.participantId == pid) return &r;
    return nullptr;
}

std::string platformOfMatch(const std::string& matchId) {
    size_t at = matchId.find('_');
    if (at == std::string::npos || at == 0) return {};
    std::string p = matchId.substr(0, at);
    for (auto& c : p) c = (char)std::tolower((unsigned char)c);
    return p;
}

int estimatedRefreshSeconds(int players, int spacingMs) {
    if (players <= 0) return 0;
    // Two calls per player, and the last one needs no wait after it.
    return (players * 2 - 1) * spacingMs / 1000 + 1;
}

MatchRankView storedMatchRanks(Db& db, const std::string& matchId) {
    MatchRankView v;
    v.rows = db.matchRanks(matchId);

    int total = 0;
    auto match = parseMatch(db.matchJson(matchId));
    if (match) total = (int)match->participants.size();
    if (total == 0) {
        v.note = "Esa partida no esta guardada, asi que no hay a quien mirar.";
        return v;
    }

    std::vector<int> steps;
    for (const auto& r : v.rows) {
        if (!r.cached()) continue;
        if (r.ranked()) {
            ++v.known;
            int s = ladderStep(r);
            if (s >= 0) steps.push_back(s);
        } else {
            ++v.unranked;
        }
    }
    v.missing = total - v.known - v.unranked;
    if (v.missing < 0) v.missing = 0;

    // The average is only offered when most of the table is known. Averaging
    // three of ten and calling it "the average of the game" would be a lie.
    if (steps.size() >= 6) {
        int sum = 0;
        for (int s : steps) sum += s;
        int avg = sum / (int)steps.size();
        v.averageTier = tiers()[std::min((size_t)(avg / 4), tiers().size() - 1)];
        v.averageDivision = divisionName(avg % 4);
    }

    if (v.missing > 0) {
        v.note = "Falta el rango de " + std::to_string(v.missing) + " de " +
                 std::to_string(total) + " jugadores. Pulsa Actualizar rangos.";
    } else if (steps.empty()) {
        v.note = "Ninguno de los diez tiene clasificacion en esta cola.";
    }
    return v;
}

RankFetchResult refreshMatchRanks(Db& db, const std::string& matchId, const RiotApiConfig& api,
                                  int onlyParticipantId, bool force,
                                  const std::function<void(RankFetchProgress)>& onProgress,
                                  int spacingMs) {
    RankFetchResult res;

    // The policy barrier. Every path into this function needs a match that is
    // already stored, and champion select has none (PRD 17.2).
    std::string raw = db.matchJson(matchId);
    if (raw.empty()) {
        res.message = "No hay ninguna partida guardada con ese id. Esta funcion solo lee "
                      "partidas ya jugadas, nunca un champion select.";
        return res;
    }
    auto match = parseMatch(raw);
    if (!match) {
        res.message = "La partida guardada no se puede leer. Vuelve a importarla.";
        return res;
    }
    if (api.apiKey.empty()) {
        res.message = "Falta tu clave de Riot. Ponla en Ajustes o con "
                      "--set-key RGAPI-... y vuelve a intentarlo.";
        return res;
    }
    std::string platform = platformOfMatch(matchId);
    if (platform.empty()) {
        res.message = "El id de la partida no dice de que servidor es, y summoner-v4 "
                      "necesita saberlo.";
        return res;
    }
    std::string host = platform + ".api.riotgames.com";

    std::vector<const Participant*> targets;
    auto cached = db.matchRanks(matchId);
    for (const auto& p : match->participants) {
        if (onlyParticipantId != 0 && p.participantId != onlyParticipantId) continue;
        if (p.puuid.empty()) continue;
        if (!force) {
            bool have = false;
            for (const auto& c : cached)
                if (c.participantId == p.participantId && c.cached()) have = true;
            if (have) continue;
        }
        targets.push_back(&p);
    }
    if (targets.empty()) {
        res.ok = true;
        res.message = "Ya estaban todos leidos. Usa Forzar si quieres releerlos.";
        return res;
    }

    int step = 0;
    int total = (int)targets.size() * 2;
    auto tick = [&](const std::string& label) {
        if (onProgress) onProgress({ ++step, total, label });
    };

    for (size_t i = 0; i < targets.size(); ++i) {
        const Participant* p = targets[i];
        std::string err;

        tick(p->championName + ": buscando invocador");
        std::string body = apiGet(api, host, "/lol/summoner/v4/summoners/by-puuid/" + p->puuid,
                                  &err);
        if (spacingMs > 0) Sleep((DWORD)spacingMs);
        std::string summonerId;
        if (!body.empty()) {
            try {
                summonerId = json::parse(body).value("id", "");
            } catch (...) {}
        }
        if (summonerId.empty()) {
            ++res.failed;
            if (res.message.empty() && !err.empty()) res.message = err;
            // 403 and 429 will not fix themselves on the next player.
            if (err.find("clave") != std::string::npos || err.find("cuota") != std::string::npos)
                break;
            continue;
        }

        tick(p->championName + ": leyendo rango");
        std::string entries = apiGet(api, host,
                                     "/lol/league/v4/entries/by-summoner/" + summonerId, &err);
        if (spacingMs > 0 && i + 1 < targets.size()) Sleep((DWORD)spacingMs);
        if (entries.empty()) {
            ++res.failed;
            if (res.message.empty() && !err.empty()) res.message = err;
            if (err.find("clave") != std::string::npos || err.find("cuota") != std::string::npos)
                break;
            continue;
        }

        RivalRank r;
        r.participantId = p->participantId;
        r.readAtEpoch = (int64_t)time(nullptr);
        try {
            for (const auto& e : json::parse(entries)) {
                if (e.value("queueType", "") != "RANKED_SOLO_5x5") continue;
                r.tier = e.value("tier", "");
                r.division = e.value("rank", "");
                r.leaguePoints = e.value("leaguePoints", 0);
                break;
            }
        } catch (...) {}
        // A player with no solo queue rank is stored too, with an empty tier:
        // "read and unranked" is an answer, and it stops a pointless re-read.
        db.upsertMatchRank(matchId, r);
        ++res.fetched;
    }

    res.ok = res.fetched > 0;
    if (res.message.empty()) {
        res.message = std::to_string(res.fetched) + " jugadores leidos" +
                      (res.failed ? ", " + std::to_string(res.failed) + " fallaron" : "") + ".";
    }
    db.audit("rival_rank", "{\"match\":\"" + matchId + "\",\"fetched\":" +
                           std::to_string(res.fetched) + "}");
    return res;
}

} // namespace rl
