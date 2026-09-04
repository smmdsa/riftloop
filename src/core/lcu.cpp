#include "core/lcu.h"
#include "core/http.h"
#include "core/util.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <tlhelp32.h>

#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

namespace rl {

namespace {

// lockfile format: name:pid:port:password:protocol
std::optional<LcuConnection> parseLockfile(const fs::path& p) {
    std::string content = util::readFile(p);
    if (content.empty()) return std::nullopt;
    std::vector<std::string> parts;
    std::stringstream ss(content);
    std::string part;
    while (std::getline(ss, part, ':')) parts.push_back(part);
    if (parts.size() < 5) return std::nullopt;
    LcuConnection c;
    try { c.port = std::stoi(parts[2]); } catch (...) { return std::nullopt; }
    c.password = parts[3];
    return c;
}

// Directory of the running LeagueClientUx.exe, if any.
fs::path leagueProcessDir() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"LeagueClientUx.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"LeagueClient.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!pid) return {};

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    fs::path dir;
    if (QueryFullProcessImageNameW(proc, 0, buf, &n)) dir = fs::path(buf).parent_path();
    CloseHandle(proc);
    return dir;
}

} // namespace

bool Lcu::connect(const std::string& configuredLockfilePath) {
    conn_ = {};
    std::vector<fs::path> candidates;
    if (!configuredLockfilePath.empty()) candidates.push_back(configuredLockfilePath);
    candidates.push_back("C:\\Riot Games\\League of Legends\\lockfile");
    if (fs::path d = leagueProcessDir(); !d.empty()) candidates.push_back(d / "lockfile");

    for (auto& p : candidates) {
        std::error_code ec;
        if (!fs::exists(p, ec)) continue;
        if (auto c = parseLockfile(p)) {
            conn_ = *c;
            // Probe: an unreachable port means a stale lockfile.
            if (!gameflowPhase().empty()) return true;
            conn_ = {};
        }
    }
    return false;
}

std::string Lcu::get(const std::string& path) {
    if (!conn_.port) return {};
    http::Options opt;
    opt.ignoreCertErrors = true;         // LCU self-signed local cert
    opt.basicUser = "riot";
    opt.basicPass = conn_.password;
    // History payloads are large; state polls stay snappy.
    opt.timeoutMs = path.rfind("/lol-match-history", 0) == 0 ? 15000 : 2000;
    auto r = http::get("127.0.0.1", conn_.port, true, path, opt);
    return r.status == 200 ? r.body : "";
}

std::string Lcu::getRaw(const std::string& path, int timeoutMs) {
    if (!connected()) return "";
    http::Options opt;
    opt.ignoreCertErrors = true;
    opt.basicUser = "riot";
    opt.basicPass = conn_.password;
    opt.timeoutMs = timeoutMs;
    auto r = http::get("127.0.0.1", conn_.port, true, path, opt);
    return r.status == 200 ? r.body : "";
}

bool Lcu::requestRaw(const std::string& method, const std::string& path,
                     const std::string& jsonBody, std::string* response, int timeoutMs) {
    if (!connected()) return false;
    http::Options opt;
    opt.ignoreCertErrors = true;
    opt.basicUser = "riot";
    opt.basicPass = conn_.password;
    opt.timeoutMs = timeoutMs;
    opt.headers["Content-Type"] = "application/json";
    auto r = http::request(method, "127.0.0.1", conn_.port, true, path, jsonBody, opt);
    if (response) *response = r.body;
    return r.status >= 200 && r.status < 300;
}

bool Lcu::postRaw(const std::string& path, const std::string& jsonBody, std::string* response,
                  int timeoutMs) {
    return requestRaw("POST", path, jsonBody, response, timeoutMs);
}

std::string Lcu::gameflowPhase() {
    std::string body = get("/lol-gameflow/v1/gameflow-phase");
    if (body.size() >= 2 && body.front() == '"' && body.back() == '"')
        return body.substr(1, body.size() - 2);
    return {};
}

namespace {

void addUnique(std::vector<int>& out, int id) {
    if (id <= 0) return;
    for (int v : out) if (v == id) return;
    out.push_back(id);
}

} // namespace

std::optional<ChampSelectView> parseChampSelect(const std::string& sessionJson,
                                                const std::string& pickableJson) {
    if (sessionJson.empty()) return std::nullopt;
    try {
        json j = json::parse(sessionJson);
        ChampSelectView v;
        int localCell = j.value("localPlayerCellId", -1);
        for (auto& p : j.value("myTeam", json::array())) {
            int champ = p.value("championId", 0);
            int hover = p.value("championPickIntent", 0);
            if (p.value("cellId", -2) == localCell) {
                v.localChampionId = champ;
                v.localHoverChampionId = hover;
                v.assignedRole = p.value("assignedPosition", "");
                // LCU uses lowercase ("bottom"); normalize to match-v5 casing.
                for (auto& ch : v.assignedRole) ch = (char)toupper((unsigned char)ch);
                if (v.assignedRole == "BOT") v.assignedRole = "BOTTOM";
            } else {
                // Before the lock the client shows the ally hover; use it.
                addUnique(v.allyChampionIds, champ > 0 ? champ : hover);
            }
        }
        for (auto& p : j.value("theirTeam", json::array())) {
            int champ = p.value("championId", 0);
            addUnique(v.enemyChampionIds, champ > 0 ? champ : p.value("championPickIntent", 0));
        }
        // Bans live in two places. The summary lists fill late in some client
        // builds, so read the completed ban actions too (RF-CS-001).
        for (auto& group : j.value("actions", json::array()))
            for (auto& a : group)
                if (a.value("type", "") == "ban" && a.value("completed", false))
                    addUnique(v.banIds, a.value("championId", 0));
        if (j.contains("bans")) {
            for (auto& b : j["bans"].value("myTeamBans", json::array()))
                addUnique(v.banIds, b.get<int>());
            for (auto& b : j["bans"].value("theirTeamBans", json::array()))
                addUnique(v.banIds, b.get<int>());
        }
        if (!pickableJson.empty()) {
            try {
                for (auto& id : json::parse(pickableJson))
                    v.pickableChampionIds.push_back(id.get<int>());
            } catch (...) {}
        }
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ChampSelectView> Lcu::champSelect() {
    std::string body = get("/lol-champ-select/v1/session");
    if (body.empty()) return std::nullopt;
    return parseChampSelect(body, get("/lol-champ-select/v1/pickable-champion-ids"));
}

std::optional<LcuSummoner> Lcu::currentSummoner() {
    std::string body = get("/lol-summoner/v1/current-summoner");
    if (body.empty()) return std::nullopt;
    try {
        json j = json::parse(body);
        LcuSummoner s;
        s.puuid = j.value("puuid", "");
        std::string name = j.value("gameName", j.value("displayName", ""));
        std::string tag = j.value("tagLine", "");
        s.riotId = tag.empty() ? name : name + "#" + tag;
        if (s.puuid.empty()) return std::nullopt;
        return s;
    } catch (...) {
        return std::nullopt;
    }
}

std::string Lcu::clientLocale() {
    std::string body = get("/riotclient/region-locale");
    if (body.empty()) return {};
    try {
        return json::parse(body).value("locale", "");
    } catch (...) {
        return {};
    }
}

std::vector<int64_t> Lcu::recentGameIds(int count) {
    std::vector<int64_t> out;
    std::string body = get("/lol-match-history/v1/products/lol/current-summoner/matches"
                           "?begIndex=0&endIndex=" + std::to_string(count));
    if (body.empty()) return out;
    try {
        json j = json::parse(body);
        for (auto& game : j.at("games").at("games")) {
            int64_t id = game.value("gameId", (int64_t)0);
            // Only classic Summoner's Rift queues are analyzable (PRD 13.3).
            int qid = game.value("queueId", 0);
            if (id > 0 && (qid == 420 || qid == 440 || qid == 400 || qid == 430))
                out.push_back(id);
        }
    } catch (...) {}
    return out;
}

std::string Lcu::gameJson(int64_t gameId) {
    return get("/lol-match-history/v1/games/" + std::to_string(gameId));
}

std::string Lcu::gameTimelineJson(int64_t gameId) {
    return get("/lol-match-history/v1/game-timelines/" + std::to_string(gameId));
}

GameState Lcu::toGameState(const std::string& phase) const {
    if (phase.empty()) return connected() ? GameState::ClientOpen : GameState::NoClient;
    if (phase == "None") return GameState::ClientOpen;
    if (phase == "Lobby") return GameState::Lobby;
    if (phase == "Matchmaking") return GameState::Queue;
    if (phase == "ReadyCheck") return GameState::ReadyCheck;
    if (phase == "ChampSelect") return GameState::ChampSelect;
    if (phase == "GameStart" || phase == "InProgress")
        return liveGameRunning() ? GameState::InGame : GameState::Loading;
    if (phase == "WaitingForStats" || phase == "PreEndOfGame" || phase == "EndOfGame")
        return GameState::PostGame;
    return GameState::ClientOpen;
}

bool liveGameRunning() {
    http::Options opt;
    opt.ignoreCertErrors = true;
    opt.timeoutMs = 900;
    auto r = http::get("127.0.0.1", 2999, true, "/liveclientdata/gamestats", opt);
    return r.status == 200;
}

double liveGameStartOffsetSec() {
    http::Options opt;
    opt.ignoreCertErrors = true;
    opt.timeoutMs = 1500;
    auto r = http::get("127.0.0.1", 2999, true, "/liveclientdata/eventdata", opt);
    if (r.status != 200) return -1;
    try {
        json j = json::parse(r.body);
        for (auto& e : j.value("Events", json::array()))
            if (e.value("EventName", "") == "GameStart") return e.value("EventTime", -1.0);
    } catch (...) {}
    return -1;
}

double liveGameTimeSec() {
    http::Options opt;
    opt.ignoreCertErrors = true;
    opt.timeoutMs = 1500;
    auto r = http::get("127.0.0.1", 2999, true, "/liveclientdata/gamestats", opt);
    if (r.status != 200) return -1;
    try {
        return json::parse(r.body).value("gameTime", -1.0);
    } catch (...) {
        return -1;
    }
}

} // namespace rl
