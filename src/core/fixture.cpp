#include "core/fixture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using nlohmann::json;

namespace rl {

namespace {

// Keys that carry an identity. The scrub walks the whole tree and rewrites
// every one of them, wherever it sits. A key that is not here survives.
const std::set<std::string>& puuidKeys() {
    static const std::set<std::string> k = { "puuid", "summonerPuuid" };
    return k;
}
const std::set<std::string>& nameKeys() {
    static const std::set<std::string> k = {
        "riotIdGameName", "riotIdName", "summonerName", "gameName", "playerName"
    };
    return k;
}
const std::set<std::string>& tagKeys() {
    static const std::set<std::string> k = { "riotIdTagline", "tagLine", "riotIdTagLine" };
    return k;
}
const std::set<std::string>& accountKeys() {
    static const std::set<std::string> k = { "summonerId", "accountId" };
    return k;
}

std::string displayName(const std::string& alias) {
    if (alias == "me-puuid") return "Me";
    if (alias.size() > 1 && alias[0] == 'p') return "Player" + alias.substr(1);
    return "Anon";
}

// Aliases, built from the match participants and reused by the timeline.
struct Aliases {
    std::map<std::string, std::string> byPuuid;   // real puuid -> alias
    std::map<int, std::string>         byPid;     // participantId -> alias
};

// Replaces every identity under node. alias is the participant that owns the
// object being walked, and it is empty when nothing says who that is.
void scrub(json& node, const Aliases& a) {
    if (node.is_array()) {
        for (auto& child : node) scrub(child, a);
        return;
    }
    if (!node.is_object()) return;

    // Who owns this object? The puuid decides first, the participantId second.
    std::string alias;
    if (node.contains("puuid") && node["puuid"].is_string()) {
        auto it = a.byPuuid.find(node["puuid"].get<std::string>());
        if (it != a.byPuuid.end()) alias = it->second;
    }
    if (alias.empty() && node.contains("participantId") && node["participantId"].is_number_integer()) {
        auto it = a.byPid.find(node["participantId"].get<int>());
        if (it != a.byPid.end()) alias = it->second;
    }

    for (auto& [key, value] : node.items()) {
        if (puuidKeys().count(key) && value.is_string()) {
            auto it = a.byPuuid.find(value.get<std::string>());
            value = it != a.byPuuid.end() ? it->second : std::string("anon");
        } else if (nameKeys().count(key) && value.is_string()) {
            value = alias.empty() ? std::string("Anon") : displayName(alias);
        } else if (tagKeys().count(key) && value.is_string()) {
            value = "LAS";
        } else if (accountKeys().count(key)) {
            if (value.is_string())      value = "";
            else if (value.is_number()) value = 0;
        } else {
            scrub(value, a);
        }
    }
}

// The flat puuid list of metadata.participants. Its entries are bare strings,
// so the object walk above never reaches them.
void scrubPuuidList(json& node, const Aliases& a) {
    if (!node.is_array()) return;
    for (auto& v : node) {
        if (!v.is_string()) continue;
        auto it = a.byPuuid.find(v.get<std::string>());
        v = it != a.byPuuid.end() ? it->second : std::string("anon");
    }
}

// items() over a value() result walks a temporary that dies at the end of the
// expression. Both loops below iterate a real reference instead.
int countPositions(const json& info, bool inFrames) {
    if (!info.contains("frames") || !info["frames"].is_array()) return 0;
    int n = 0;
    for (const auto& f : info["frames"]) {
        if (inFrames) {
            if (!f.contains("participantFrames") || !f["participantFrames"].is_object()) continue;
            for (const auto& [pidStr, pf] : f["participantFrames"].items()) {
                (void)pidStr;
                if (pf.contains("position")) ++n;
            }
        } else {
            if (!f.contains("events") || !f["events"].is_array()) continue;
            for (const auto& e : f["events"])
                if (e.contains("position")) ++n;
        }
    }
    return n;
}

} // namespace

FixtureExport anonymizeFixture(const std::string& matchJson,
                               const std::string& timelineJson,
                               const std::string& myPuuid,
                               const std::string& newMatchId) {
    FixtureExport out;
    if (matchJson.empty()) {
        out.error = "No hay match_json para esa partida. La base no lo guardo. "
                    "Importa la partida otra vez con --fetch-lcu.";
        return out;
    }

    json match, timeline;
    try {
        match = json::parse(matchJson);
        if (!timelineJson.empty()) timeline = json::parse(timelineJson);
    } catch (const std::exception& e) {
        out.error = std::string("El JSON guardado no se puede parsear: ") + e.what()
                  + ". La fila esta corrupta. Vuelve a importar la partida.";
        return out;
    }

    if (!match.contains("info") || !match["info"].contains("participants")) {
        out.error = "El match no tiene info.participants. No es un payload match-v5. "
                    "Comprueba el matchId.";
        return out;
    }

    // Aliases first: every later rewrite reads this map, so nothing is guessed
    // twice and the timeline agrees with the match.
    Aliases a;
    std::vector<std::pair<int, std::string>> byOrder;   // participantId, puuid
    for (auto& p : match["info"]["participants"]) {
        int pid = p.value("participantId", 0);
        std::string puuid = p.value("puuid", "");
        if (puuid.empty()) continue;
        byOrder.emplace_back(pid, puuid);
    }
    std::sort(byOrder.begin(), byOrder.end());
    int next = 2;
    for (auto& [pid, puuid] : byOrder) {
        std::string alias = (!myPuuid.empty() && puuid == myPuuid)
                          ? std::string("me-puuid")
                          : "p" + std::to_string(next++);
        a.byPuuid[puuid] = alias;
        if (pid > 0) a.byPid[pid] = alias;
    }
    out.participants = (int)a.byPuuid.size();
    if (out.participants == 0) {
        out.error = "Ningun participante trae puuid. Sin el no se puede anonimizar "
                    "de forma estable. Importa la partida otra vez.";
        return out;
    }

    scrub(match, a);
    if (match.contains("metadata") && match["metadata"].contains("participants"))
        scrubPuuidList(match["metadata"]["participants"], a);
    if (!timeline.is_null()) {
        scrub(timeline, a);
        if (timeline.contains("metadata") && timeline["metadata"].contains("participants"))
            scrubPuuidList(timeline["metadata"]["participants"], a);
    }

    if (!newMatchId.empty()) {
        if (match.contains("metadata")) match["metadata"]["matchId"] = newMatchId;
        if (!timeline.is_null() && timeline.contains("metadata"))
            timeline["metadata"]["matchId"] = newMatchId;
    }

    if (!timeline.is_null() && timeline.contains("info")) {
        const json& info = timeline["info"];
        out.frames = (int)info.value("frames", json::array()).size();
        for (auto& f : info.value("frames", json::array()))
            out.events += (int)f.value("events", json::array()).size();
        out.framesWithPosition = countPositions(info, true);
        out.eventsWithPosition = countPositions(info, false);
    }

    json wrapper;
    wrapper["match"] = match;
    if (!timeline.is_null()) wrapper["timeline"] = timeline;
    out.text = wrapper.dump();
    out.ok = true;
    return out;
}

} // namespace rl
