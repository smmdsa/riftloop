#include "core/rofl.h"

#include "core/replays.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <shlobj.h>

#include <cstdio>

using nlohmann::json;
namespace fs = std::filesystem;

namespace rl {

namespace {

constexpr const char* kMagic = "RIOT";
constexpr const char* kBlockStart = "{\"gameLength\"";
// The block sits in the last kilobytes. Reading a whole 18 MB replay to get
// them would break the disk budget of PRD 15 for nothing.
constexpr std::streamoff kTailBytes = 1 << 20;
constexpr std::streamoff kHeadBytes = 64;

// A stat value arrives as a string. A field that is not a number is not a
// metric, and it is dropped rather than stored as zero.
bool toInt(const json& v, int& out) {
    try {
        if (v.is_number_integer()) { out = v.get<int>(); return true; }
        if (v.is_number_float())   { out = (int)v.get<double>(); return true; }
        if (!v.is_string()) return false;
        const std::string& s = v.get_ref<const std::string&>();
        if (s.empty()) return false;
        size_t used = 0;
        long long n = std::stoll(s, &used);
        if (used != s.size()) return false;
        out = (int)n;
        return true;
    } catch (...) {
        return false;
    }
}

// The end of the JSON object that starts at "from". The file keeps a few
// padding bytes after the block, and a parser that must consume its whole
// input chokes on them, so the object is delimited here instead.
// Returns npos when the braces never balance.
size_t blockEnd(const std::string& bytes, size_t from) {
    int depth = 0;
    bool inString = false, escaped = false;
    for (size_t i = from; i < bytes.size(); ++i) {
        char c = bytes[i];
        if (inString) {
            if (escaped)        escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"')  inString = false;
            continue;
        }
        if (c == '"')      inString = true;
        else if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i + 1;
    }
    return std::string::npos;
}

// The version follows the six magic bytes as plain ASCII digits and dots.
std::string readVersion(const std::string& bytes) {
    std::string out;
    for (size_t i = 6; i < bytes.size() && i < 64; ++i) {
        char c = bytes[i];
        if ((c >= '0' && c <= '9') || c == '.') out += c;
        else if (!out.empty()) break;
    }
    return out;
}

} // namespace

const std::vector<std::string>& kRoflMetrics() {
    static const std::vector<std::string> k = {
        // Ability and summoner usage: how much the player actually pressed.
        "SPELL1_CAST", "SPELL2_CAST", "SPELL3_CAST", "SPELL4_CAST",
        "SUMMON_SPELL1_CAST", "SUMMON_SPELL2_CAST",
        // Time. Death time is the cost of a mistake, in seconds.
        "TOTAL_TIME_SPENT_DEAD", "LONGEST_TIME_SPENT_LIVING", "TIME_PLAYED",
        "TIME_CCING_OTHERS", "TOTAL_TIME_CROWD_CONTROL_DEALT_TO_CHAMPIONS",
        // Communication.
        "ALL_IN_PINGS", "ASSIST_ME_PINGS", "BASIC_PINGS", "COMMAND_PINGS",
        "DANGER_PINGS", "ENEMY_MISSING_PINGS", "ENEMY_VISION_PINGS",
        "GET_BACK_PINGS", "HOLD_PINGS", "NEED_VISION_PINGS", "ON_MY_WAY_PINGS",
        "PUSH_PINGS", "RETREAT_PINGS", "VISION_CLEARED_PINGS",
        // Vision. The LCU timeline emits no ward event at all, so this is the
        // only vision the local data has.
        "VISION_SCORE", "WARD_PLACED", "WARD_KILLED", "WARD_PLACED_DETECTOR",
        "VISION_WARDS_BOUGHT_IN_GAME", "SIGHT_WARDS_BOUGHT_IN_GAME",
        // Map pressure and jungle.
        "Missions_TurretPlatesDestroyed", "Missions_GoldFromTurretPlatesTaken",
        "Missions_CreepScoreBy10Minutes",
        "NEUTRAL_MINIONS_KILLED_ENEMY_JUNGLE", "NEUTRAL_MINIONS_KILLED_YOUR_JUNGLE",
        "OBJECTIVES_STOLEN", "TURRET_TAKEDOWNS",
        // Damage shapes that match-v5 keeps to itself.
        "TOTAL_DAMAGE_SELF_MITIGATED", "TOTAL_DAMAGE_SHIELDED_ON_TEAMMATES",
        "LARGEST_KILLING_SPREE", "KILLING_SPREES",
    };
    return k;
}

const RoflPlayer* RoflStats::user() const {
    for (const auto& p : players)
        if (p.isUser) return &p;
    return nullptr;
}

RoflStats parseRoflStats(const std::string& bytes, const std::string& myPuuid) {
    RoflStats out;
    if (bytes.size() < 8 || bytes.compare(0, 4, kMagic) != 0) {
        out.error = "El archivo no empieza por RIOT, asi que no es un replay. "
                    "Comprueba la ruta.";
        return out;
    }
    out.gameVersion = readVersion(bytes);

    size_t at = bytes.rfind(kBlockStart);
    if (at == std::string::npos) {
        out.error = "El replay no trae el bloque de metadata en claro. El formato de Riot "
                    "cambia entre parches; no se lee nada de este archivo.";
        return out;
    }

    size_t end = blockEnd(bytes, at);
    if (end == std::string::npos) {
        out.error = "El bloque de metadata esta incompleto. El archivo esta truncado; "
                    "vuelve a descargar el replay.";
        return out;
    }
    json meta;
    try {
        meta = json::parse(bytes.begin() + at, bytes.begin() + end, nullptr, true, false);
    } catch (const std::exception& e) {
        out.error = std::string("El bloque de metadata no es JSON valido: ") + e.what() +
                    ". El archivo puede estar truncado.";
        return out;
    }

    out.gameLengthMs = meta.value("gameLength", (int64_t)0);
    out.chunks = meta.value("lastGameChunkId", 0);
    out.keyFrames = meta.value("lastKeyFrameId", 0);

    json stats;
    try {
        stats = json::parse(meta.value("statsJson", std::string("[]")));
    } catch (...) {
        out.error = "El bloque no trae statsJson legible. No hay metricas que leer.";
        return out;
    }
    if (!stats.is_array() || stats.empty()) {
        out.error = "El replay no lista jugadores. No hay metricas que leer.";
        return out;
    }

    for (const auto& row : stats) {
        if (!row.is_object()) continue;
        RoflPlayer p;
        // The puuid decides whose row this is, and then it is gone. It is never
        // copied into RoflPlayer, so nothing downstream can store it.
        if (!myPuuid.empty() && row.value("PUUID", std::string()) == myPuuid) p.isUser = true;
        p.champion = row.value("SKIN", std::string());
        p.position = row.value("TEAM_POSITION", std::string());
        p.win = row.value("WIN", std::string()) == "Win";
        int team = 0;
        if (toInt(row.value("TEAM", json()), team)) p.team = team;

        for (const auto& key : kRoflMetrics()) {
            if (!row.contains(key)) continue;
            int v = 0;
            if (toInt(row[key], v)) p.metrics[key] = v;
        }
        out.players.push_back(std::move(p));
    }

    out.ok = true;
    return out;
}

RoflStats readRoflFile(const fs::path& file, const std::string& myPuuid) {
    RoflStats out;
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        out.error = "No existe " + file.string() + ". El cliente borra los replays viejos; "
                    "vuelve a descargarlo desde el historial.";
        return out;
    }
    auto size = (std::streamoff)fs::file_size(file, ec);
    if (ec || size <= 0) {
        out.error = "No se puede leer el tamano de " + file.string() + ". Comprueba los permisos.";
        return out;
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, file.wstring().c_str(), L"rb") != 0 || !f) {
        out.error = "No se puede abrir " + file.string() + ". El cliente puede tenerlo abierto; "
                    "cierra el replay e intentalo otra vez.";
        return out;
    }
    std::string bytes;
    if (size <= kHeadBytes + kTailBytes) {
        bytes.resize((size_t)size);
        size_t got = fread(bytes.data(), 1, bytes.size(), f);
        bytes.resize(got);
    } else {
        bytes.resize((size_t)kHeadBytes);
        size_t got = fread(bytes.data(), 1, bytes.size(), f);
        bytes.resize(got);
        if (_fseeki64(f, -kTailBytes, SEEK_END) == 0) {
            std::string tail((size_t)kTailBytes, '\0');
            size_t gotTail = fread(tail.data(), 1, tail.size(), f);
            tail.resize(gotTail);
            bytes += tail;
        }
    }
    fclose(f);
    return parseRoflStats(bytes, myPuuid);
}

fs::path roflPathForMatch(const std::string& matchId, const fs::path& folder) {
    int64_t gameId = gameIdOfMatch(matchId);
    if (gameId <= 0) return {};
    std::string platform = matchId.substr(0, matchId.find('_'));
    if (platform.empty() || platform == matchId) return {};

    std::vector<fs::path> roots;
    if (!folder.empty()) roots.push_back(folder);
    else {
        // The Documents folder can be redirected into OneDrive, so the known
        // folder id is the only path that is right on every machine.
        PWSTR docs = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
            roots.push_back(fs::path(docs) / "League of Legends" / "Replays");
            CoTaskMemFree(docs);
        }
    }

    std::string name = platform + "-" + std::to_string(gameId) + ".rofl";
    std::error_code ec;
    for (const auto& root : roots) {
        fs::path p = root / name;
        if (fs::exists(p, ec)) return p;
    }
    return {};
}

} // namespace rl
