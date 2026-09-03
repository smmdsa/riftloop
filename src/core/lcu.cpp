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
    opt.timeoutMs = 2000;
    auto r = http::get("127.0.0.1", conn_.port, true, path, opt);
    return r.status == 200 ? r.body : "";
}

std::string Lcu::gameflowPhase() {
    std::string body = get("/lol-gameflow/v1/gameflow-phase");
    if (body.size() >= 2 && body.front() == '"' && body.back() == '"')
        return body.substr(1, body.size() - 2);
    return {};
}

std::optional<ChampSelectView> Lcu::champSelect() {
    std::string body = get("/lol-champ-select/v1/session");
    if (body.empty()) return std::nullopt;
    try {
        json j = json::parse(body);
        ChampSelectView v;
        int localCell = j.value("localPlayerCellId", -1);
        for (auto& p : j.value("myTeam", json::array())) {
            int champ = p.value("championId", 0);
            if (p.value("cellId", -2) == localCell) {
                v.localChampionId = champ;
                v.assignedRole = p.value("assignedPosition", "");
                // LCU uses lowercase ("bottom"); normalize to match-v5 casing.
                for (auto& ch : v.assignedRole) ch = (char)toupper((unsigned char)ch);
                if (v.assignedRole == "BOT") v.assignedRole = "BOTTOM";
            } else if (champ > 0) {
                v.allyChampionIds.push_back(champ);
            }
        }
        for (auto& p : j.value("theirTeam", json::array())) {
            int champ = p.value("championId", 0);
            if (champ > 0) v.enemyChampionIds.push_back(champ);
        }
        if (j.contains("bans")) {
            for (auto& b : j["bans"].value("myTeamBans", json::array()))
                if (b.get<int>() > 0) v.banIds.push_back(b.get<int>());
            for (auto& b : j["bans"].value("theirTeamBans", json::array()))
                if (b.get<int>() > 0) v.banIds.push_back(b.get<int>());
        }
        std::string pickable = get("/lol-champ-select/v1/pickable-champion-ids");
        if (!pickable.empty()) {
            try {
                for (auto& id : json::parse(pickable)) v.pickableChampionIds.push_back(id.get<int>());
            } catch (...) {}
        }
        return v;
    } catch (...) {
        return std::nullopt;
    }
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

} // namespace rl
