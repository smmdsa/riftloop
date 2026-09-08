#include "core/lcu_history.h"
#include "core/ingest.h"
#include "core/util.h"

#include <map>
#include <vector>

using nlohmann::json;

namespace rl {

namespace {

constexpr int kSmiteKey = 11;

// v4 lane/role -> v5 teamPosition.
//
// Measured against the client on 2026-09-07, over three of the user's games:
// the role of the support arrives as "SUPPORT", not "DUO_SUPPORT", and the
// carry as "CARRY", not "DUO_CARRY". Matching only the DUO_ names left every
// support labelled BOTTOM, which is why the table showed BOTTOM twice and
// UTILITY never.
std::string positionOf(const json& participant) {
    std::string lane, role;
    if (participant.contains("timeline")) {
        lane = participant["timeline"].value("lane", "");
        role = participant["timeline"].value("role", "");
    }
    bool support = role == "SUPPORT" || role == "DUO_SUPPORT";
    if (lane == "TOP") return "TOP";
    if (lane == "JUNGLE") return "JUNGLE";
    if (lane == "MIDDLE" || lane == "MID") return "MIDDLE";
    if (lane == "BOTTOM" || lane == "BOT") return support ? "UTILITY" : "BOTTOM";
    if (support) return "UTILITY";
    return "";
}

} // namespace

// Two positions arrive wrong from the client, and both are recoverable from
// numbers that the converted payload keeps. Measured on 2026-09-07 over three
// of the user's games.
//
// 1. The top laner is labelled JUNGLE. Every side had two players at
//    lane=JUNGLE role=NONE:
//
//      pid 1  smite=false  neutral=0    minions=133   <- the top laner
//      pid 2  smite=true   neutral=110  minions=9     <- the jungler
//
//    Of the players a side calls JUNGLE, the one carrying Smite keeps it and
//    the other moves to TOP, when that side has no TOP. With no Smite at all,
//    the most camps wins.
//
// 2. A payload converted before the SUPPORT fix has two BOTTOM a side. The
//    support is the one with far less farm: 246 cs against 41 in that game.
//
// Both are heuristics over broken fields, written here and not hidden in a
// view. A side that already reads correctly is left alone.
int repairPositionsV5(json& participants) {
    int moved = 0;
    auto smite = [](const json& p) {
        return p.value("summoner1Id", 0) == kSmiteKey ||
               p.value("summoner2Id", 0) == kSmiteKey;
    };
    auto farm = [](const json& p) {
        return p.value("totalMinionsKilled", 0) + p.value("neutralMinionsKilled", 0);
    };

    for (int teamId : {100, 200}) {
        std::vector<json*> jungle, bottom;
        bool hasTop = false, hasUtility = false;
        for (auto& p : participants) {
            if (p.value("teamId", 0) != teamId) continue;
            std::string pos = p.value("teamPosition", "");
            if (pos == "TOP") hasTop = true;
            if (pos == "UTILITY") hasUtility = true;
            if (pos == "JUNGLE") jungle.push_back(&p);
            if (pos == "BOTTOM") bottom.push_back(&p);
        }

        if (jungle.size() >= 2) {
            json* keep = nullptr;
            for (json* p : jungle)
                if (smite(*p)) { keep = p; break; }
            if (!keep)
                for (json* p : jungle)
                    if (!keep || p->value("neutralMinionsKilled", 0) >
                                 keep->value("neutralMinionsKilled", 0))
                        keep = p;
            for (json* p : jungle) {
                // Only one player fills an empty TOP. A third one stays as it
                // came: a guess repeated is not more true.
                if (p == keep || hasTop) continue;
                (*p)["teamPosition"] = "TOP";
                hasTop = true;
                ++moved;
            }
        }

        if (bottom.size() >= 2 && !hasUtility) {
            json* support = nullptr;
            for (json* p : bottom)
                if (!support || farm(*p) < farm(*support)) support = p;
            if (support) {
                (*support)["teamPosition"] = "UTILITY";
                ++moved;
            }
        }
    }
    return moved;
}

json convertLcuGameToV5(const json& g, const Ddragon* dd) {
    json info;
    info["gameVersion"] = g.value("gameVersion", "");
    info["gameCreation"] = g.value("gameCreation", (int64_t)0);
    info["gameDuration"] = g.value("gameDuration", (int64_t)0);   // seconds in v4
    info["queueId"] = g.value("queueId", 0);

    // participantId -> identity
    std::map<int, json> identities;
    for (auto& pi : g.value("participantIdentities", json::array()))
        identities[pi.value("participantId", 0)] = pi.value("player", json::object());

    json participants = json::array();
    for (auto& p : g.value("participants", json::array())) {
        json v5;
        int pid = p.value("participantId", 0);
        v5["participantId"] = pid;
        int champId = p.value("championId", 0);
        v5["championId"] = champId;
        std::string champName;
        if (dd)
            if (const ChampInfo* c = dd->championByKey(champId)) champName = c->id;
        v5["championName"] = champName;
        v5["teamId"] = p.value("teamId", 0);
        v5["teamPosition"] = positionOf(p);
        v5["summoner1Id"] = p.value("spell1Id", 0);
        v5["summoner2Id"] = p.value("spell2Id", 0);

        const json& st = p.contains("stats") ? p["stats"] : json::object();
        v5["win"] = st.value("win", false);
        v5["kills"] = st.value("kills", 0);
        v5["deaths"] = st.value("deaths", 0);
        v5["assists"] = st.value("assists", 0);
        v5["goldEarned"] = st.value("goldEarned", 0);
        v5["totalMinionsKilled"] = st.value("totalMinionsKilled", 0);
        v5["neutralMinionsKilled"] = st.value("neutralMinionsKilled", 0);
        v5["champLevel"] = st.value("champLevel", 0);
        for (int i = 0; i < 6; ++i)
            v5["item" + std::to_string(i)] = st.value("item" + std::to_string(i), 0);

        // v4 keeps the rune page flat (perk0..perk5 + the two style ids). Rebuild
        // the v5 shape so the ingest has a single parser.
        json primary = json::array(), sub = json::array();
        for (int i = 0; i < 6; ++i) {
            int perk = st.value("perk" + std::to_string(i), 0);
            if (perk <= 0) continue;
            (i < 4 ? primary : sub).push_back(json{{"perk", perk}});
        }
        if (!primary.empty() || !sub.empty()) {
            v5["perks"]["styles"] = json::array(
                {json{{"description", "primaryStyle"},
                      {"style", st.value("perkPrimaryStyle", 0)},
                      {"selections", primary}},
                 json{{"description", "subStyle"},
                      {"style", st.value("perkSubStyle", 0)},
                      {"selections", sub}}});
        }

        auto idIt = identities.find(pid);
        if (idIt != identities.end()) {
            const json& player = idIt->second;
            v5["puuid"] = player.value("puuid", "");
            std::string gname = player.value("gameName", player.value("summonerName", ""));
            v5["riotIdGameName"] = gname;
            v5["riotIdTagline"] = player.value("tagLine", "");
        } else {
            v5["puuid"] = "";
        }
        participants.push_back(std::move(v5));
    }
    repairPositionsV5(participants);
    info["participants"] = std::move(participants);

    std::string platform = g.value("platformId", "");
    int64_t gameId = g.value("gameId", (int64_t)0);
    std::string matchId = (platform.empty() ? "LOCAL" : platform) + "_" + std::to_string(gameId);

    json out;
    out["metadata"]["matchId"] = matchId;
    out["info"] = std::move(info);
    return out;
}

json convertLcuTimelineToV5(const json& tl, const std::string& matchId, const json& v5Match) {
    // participantId -> teamId, to derive killerTeamId for objectives.
    std::map<int, int> teamOf;
    try {
        for (auto& p : v5Match.at("info").at("participants"))
            teamOf[p.value("participantId", 0)] = p.value("teamId", 0);
    } catch (...) {}

    json frames = json::array();
    for (auto& f : tl.value("frames", json::array())) {
        json frame;
        frame["timestamp"] = f.value("timestamp", (int64_t)0);
        frame["participantFrames"] = f.value("participantFrames", json::object());
        json events = json::array();
        for (auto& e : f.value("events", json::array())) {
            json ev = e;
            std::string type = e.value("type", "");
            if ((type == "ELITE_MONSTER_KILL" || type == "BUILDING_KILL") &&
                !e.contains("killerTeamId")) {
                int killer = e.value("killerId", 0);
                auto it = teamOf.find(killer);
                if (it != teamOf.end() && killer > 0) {
                    ev["killerTeamId"] = it->second;
                } else if (type == "BUILDING_KILL" && e.contains("teamId")) {
                    // v4 teamId = team of the DESTROYED building; killer is the
                    // opposite team.
                    ev["killerTeamId"] = e.value("teamId", 0) == 100 ? 200 : 100;
                }
            }
            events.push_back(std::move(ev));
        }
        frame["events"] = std::move(events);
        frames.push_back(std::move(frame));
    }

    json out;
    out["metadata"]["matchId"] = matchId;
    out["info"]["frames"] = std::move(frames);
    return out;
}

int refetchRunePages(Db& db, Lcu& lcu, const Ddragon* dd) {
    if (!lcu.connected()) return 0;
    int updated = 0;
    for (auto& matchId : db.matchesMissingField("perks")) {
        // matchId is PLATFORM_gameId.
        size_t sep = matchId.rfind('_');
        if (sep == std::string::npos) continue;
        int64_t gameId = 0;
        try { gameId = std::stoll(matchId.substr(sep + 1)); } catch (...) { continue; }
        std::string raw = lcu.gameJson(gameId);
        if (raw.empty()) continue;              // client no longer keeps it
        try {
            json v5 = convertLcuGameToV5(json::parse(raw), dd);
            if (!v5["info"]["participants"].empty() &&
                v5["info"]["participants"][0].contains("perks")) {
                db.replaceMatchJson(matchId, v5.dump());
                ++updated;
            }
        } catch (...) {}
    }
    return updated;
}

LcuImportResult importFromClient(Db& db, Lcu& lcu, const Ddragon* dd, int count) {
    LcuImportResult res;
    if (!lcu.connected()) {
        res.error = "el cliente de League no esta abierto";
        return res;
    }

    // Identify the player from the client (no manual Riot ID needed).
    if (auto me = lcu.currentSummoner()) {
        Profile p = db.loadProfile();
        if (p.puuid != me->puuid) {
            p.puuid = me->puuid;
            p.riotId = me->riotId;
            db.saveProfile(p);
        }
    }

    auto ids = lcu.recentGameIds(count);
    if (ids.empty()) {
        res.error = "el cliente no devolvio historial (endpoint sin SLA); reintenta";
        return res;
    }

    for (int64_t gameId : ids) {
        try {
            std::string gRaw = lcu.gameJson(gameId);
            if (gRaw.empty()) { ++res.skipped; continue; }
            json v5 = convertLcuGameToV5(json::parse(gRaw), dd);
            std::string matchId = v5["metadata"]["matchId"].get<std::string>();
            if (db.hasMatch(matchId)) { ++res.skipped; continue; }

            std::string tRaw = lcu.gameTimelineJson(gameId);
            json v5tl;
            if (!tRaw.empty())
                v5tl = convertLcuTimelineToV5(json::parse(tRaw), matchId, v5);

            auto m = parseMatch(v5.dump());
            if (!m) { ++res.skipped; continue; }
            if (db.upsertMatch(*m, v5.dump(), v5tl.is_null() ? "" : v5tl.dump())) {
                // Rewrite the build rows with static data so each one carries
                // the traits of the team it faced.
                if (dd) db.upsertBuilds(*m, dd);
                ++res.imported;
            }
            else
                ++res.skipped;
        } catch (...) {
            ++res.skipped;
        }
    }
    db.audit("lcu_import", json{{"imported", res.imported}, {"skipped", res.skipped}}.dump());
    return res;
}

std::string resolveDataLocale(Db& db, Lcu* lcu, const std::string& configured) {
    if (!configured.empty()) return configured;
    if (lcu && lcu->connected()) {
        std::string l = lcu->clientLocale();
        if (!l.empty()) {
            db.setKv("data_locale", l);
            return l;
        }
    }
    std::string remembered = db.getKv("data_locale");
    return remembered.empty() ? "en_US" : remembered;
}

} // namespace rl
