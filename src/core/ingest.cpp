#include "core/ingest.h"
#include "core/http.h"
#include "core/util.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace rl {

namespace {

TLType eventType(const std::string& t) {
    if (t == "CHAMPION_KILL")     return TLType::ChampionKill;
    if (t == "ITEM_PURCHASED")    return TLType::ItemPurchased;
    if (t == "ITEM_SOLD")         return TLType::ItemSold;
    if (t == "ITEM_UNDO")         return TLType::ItemUndo;
    if (t == "WARD_PLACED")       return TLType::WardPlaced;
    if (t == "ELITE_MONSTER_KILL") return TLType::EliteMonsterKill;
    if (t == "BUILDING_KILL")     return TLType::BuildingKill;
    return TLType::Other;
}

std::string riotHost(const std::string& routing) {
    return routing + ".api.riotgames.com";
}

std::string apiGet(const RiotApiConfig& c, const std::string& path, std::string* error) {
    http::Options opt;
    opt.headers["X-Riot-Token"] = c.apiKey;
    auto r = http::get(riotHost(c.routing), 443, true, path, opt);
    if (r.status == 200) return r.body;
    if (error) {
        *error = r.status == 0 ? ("red: " + r.error)
                               : ("Riot API status " + std::to_string(r.status));
    }
    return {};
}

} // namespace

std::optional<MatchSummary> parseMatch(const std::string& jsonText) {
    try {
        json j = json::parse(jsonText);
        const json& info = j.at("info");
        MatchSummary m;
        m.matchId = j.contains("metadata") ? j["metadata"].value("matchId", "") : "";
        if (m.matchId.empty()) m.matchId = info.value("gameId", 0) ? std::to_string(info["gameId"].get<int64_t>()) : "unknown";
        m.gameVersion   = info.value("gameVersion", "");
        m.patch         = util::patchFamily(m.gameVersion);
        m.gameCreationMs = info.value("gameCreation", (int64_t)0);
        m.gameDurationSec = (int)info.value("gameDuration", (int64_t)0);
        // Older payloads report gameDuration in ms.
        if (m.gameDurationSec > 10000) m.gameDurationSec /= 1000;
        m.remake = m.gameDurationSec < 300;
        int qid = info.value("queueId", 0);
        m.queue = qid == 420 ? "RANKED_SOLO_5x5"
                : qid == 440 ? "RANKED_FLEX"
                : qid == 400 ? "NORMAL_DRAFT"
                : qid == 430 ? "NORMAL_BLIND"
                : qid == 450 ? "ARAM"
                : "QUEUE_" + std::to_string(qid);

        for (auto& p : info.value("participants", json::array())) {
            Participant pa;
            pa.participantId = p.value("participantId", 0);
            pa.puuid         = p.value("puuid", "");
            std::string gname = p.value("riotIdGameName", "");
            std::string tag   = p.value("riotIdTagline", "");
            pa.riotId        = gname.empty() ? p.value("summonerName", "") : gname + "#" + tag;
            pa.championName  = p.value("championName", "");
            pa.championId    = p.value("championId", 0);
            pa.teamId        = p.value("teamId", 0);
            pa.position      = p.value("teamPosition", p.value("individualPosition", ""));
            pa.win           = p.value("win", false);
            pa.kills         = p.value("kills", 0);
            pa.deaths        = p.value("deaths", 0);
            pa.assists       = p.value("assists", 0);
            pa.goldEarned    = p.value("goldEarned", 0);
            pa.totalCs       = p.value("totalMinionsKilled", 0) + p.value("neutralMinionsKilled", 0);
            pa.champLevel    = p.value("champLevel", 0);
            pa.summonerSpells = { p.value("summoner1Id", 0), p.value("summoner2Id", 0) };
            for (int i = 0; i < 6; ++i)
                pa.finalItems.push_back(p.value("item" + std::to_string(i), 0));
            // Rune page: 4 primary selections then 2 secondary (RF-RUN-001 input).
            if (p.contains("perks")) {
                for (auto& st : p["perks"].value("styles", json::array())) {
                    std::string kind = st.value("description", "");
                    if (kind == "primaryStyle") pa.perkPrimaryStyle = st.value("style", 0);
                    else if (kind == "subStyle") pa.perkSubStyle = st.value("style", 0);
                    for (auto& sel : st.value("selections", json::array()))
                        pa.perks.push_back(sel.value("perk", 0));
                }
            }
            m.participants.push_back(std::move(pa));
        }
        if (m.participants.empty()) return std::nullopt;
        return m;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Timeline> parseTimeline(const std::string& jsonText, const std::string& matchId) {
    try {
        json j = json::parse(jsonText);
        const json& info = j.at("info");
        Timeline t;
        t.matchId = matchId;
        for (auto& f : info.value("frames", json::array())) {
            TLFrame fr;
            fr.tsMs = f.value("timestamp", (int64_t)0);
            if (f.contains("participantFrames")) {
                for (auto& [pidStr, pf] : f["participantFrames"].items()) {
                    int pid = std::stoi(pidStr);
                    fr.totalGold[pid]   = pf.value("totalGold", 0);
                    fr.currentGold[pid] = pf.value("currentGold", 0);
                    fr.cs[pid] = pf.value("minionsKilled", 0) + pf.value("jungleMinionsKilled", 0);
                    fr.level[pid] = pf.value("level", 0);
                }
            }
            for (auto& e : f.value("events", json::array())) {
                TLEvent ev;
                ev.type = eventType(e.value("type", ""));
                if (ev.type == TLType::Other) continue;
                ev.tsMs = e.value("timestamp", (int64_t)0);
                ev.participantId = e.value("participantId",
                                   e.value("killerId", e.value("creatorId", 0)));
                ev.victimId = e.value("victimId", 0);
                for (auto& a : e.value("assistingParticipantIds", json::array()))
                    ev.assistIds.push_back(a);
                ev.itemId       = e.value("itemId", 0);
                ev.killerTeamId = e.value("killerTeamId", e.value("teamId", 0));
                ev.monsterType  = e.value("monsterType", "");
                ev.buildingType = e.value("buildingType", "");
                if (e.contains("position")) {
                    ev.posX = e["position"].value("x", -1);
                    ev.posY = e["position"].value("y", -1);
                }
                t.events.push_back(std::move(ev));
            }
            t.frames.push_back(std::move(fr));
        }
        if (t.frames.empty()) return std::nullopt;
        return t;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<RawPair> splitImport(const std::string& fileText) {
    try {
        json j = json::parse(fileText);
        if (j.contains("match") && j.contains("timeline"))
            return RawPair{ j["match"].dump(), j["timeline"].dump() };
        if (j.contains("info") && j["info"].contains("participants") && j.contains("metadata"))
            return RawPair{ fileText, "" };   // match only, no timeline
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::string fetchMatchIds(const RiotApiConfig& c, const std::string& puuid, int count,
                          std::string* error) {
    return apiGet(c, "/lol/match/v5/matches/by-puuid/" + puuid +
                     "/ids?start=0&count=" + std::to_string(count), error);
}
std::string fetchMatch(const RiotApiConfig& c, const std::string& matchId, std::string* error) {
    return apiGet(c, "/lol/match/v5/matches/" + matchId, error);
}
std::string fetchTimeline(const RiotApiConfig& c, const std::string& matchId, std::string* error) {
    return apiGet(c, "/lol/match/v5/matches/" + matchId + "/timeline", error);
}
std::string fetchPuuidByRiotId(const RiotApiConfig& c, const std::string& gameName,
                               const std::string& tagLine, std::string* error) {
    return apiGet(c, "/riot/account/v1/accounts/by-riot-id/" + gameName + "/" + tagLine, error);
}

} // namespace rl
