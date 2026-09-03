#include "core/util.h"
#include "core/models.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace rl {

const char* toString(GameState s) {
    switch (s) {
        case GameState::NoClient:    return "NoClient";
        case GameState::ClientOpen:  return "ClientOpen";
        case GameState::Lobby:       return "Lobby";
        case GameState::Queue:       return "Queue";
        case GameState::ReadyCheck:  return "ReadyCheck";
        case GameState::ChampSelect: return "ChampSelect";
        case GameState::Loading:     return "Loading";
        case GameState::InGame:      return "InGame";
        case GameState::PostGame:    return "PostGame";
        case GameState::Processing:  return "Processing";
    }
    return "?";
}
const char* toString(SkillState s) {
    switch (s) {
        case SkillState::NotEvaluated: return "No evaluado";
        case SkillState::Introduced:   return "Introducido";
        case SkillState::Practicing:   return "Practicando";
        case SkillState::Consistent:   return "Consistente";
        case SkillState::Mastered:     return "Dominado";
        case SkillState::NeedsRefresh: return "Necesita refresco";
    }
    return "?";
}
const char* toString(MissionStatus s) {
    switch (s) {
        case MissionStatus::Suggested: return "Sugerida";
        case MissionStatus::Active:    return "Activa";
        case MissionStatus::Evaluated: return "Evaluada";
        case MissionStatus::Discarded: return "Descartada";
    }
    return "?";
}
const char* toString(MissionResult r) {
    switch (r) {
        case MissionResult::None:          return "-";
        case MissionResult::Improved:      return "Mejoró de forma consistente";
        case MissionResult::Partial:       return "Mejoró parcialmente";
        case MissionResult::NoChange:      return "Sin cambio observable";
        case MissionResult::NotEnoughData: return "Oportunidades insuficientes";
        case MissionResult::DetectorWrong: return "Detector probablemente incorrecto";
    }
    return "?";
}

} // namespace rl

namespace rl::util {

fs::path dataDir() {
    static fs::path dir = [] {
        PWSTR raw = nullptr;
        fs::path base;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
            base = raw;
            CoTaskMemFree(raw);
        } else {
            base = fs::temp_directory_path();
        }
        fs::path d = base / "RiftLoop";
        std::error_code ec;
        fs::create_directories(d, ec);
        return d;
    }();
    return dir;
}

fs::path cacheDir() {
    fs::path d = dataDir() / "cache";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::string nowIso() {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    tm g{};
    gmtime_s(&g, &t);
    char buf[32];
    snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
    return buf;
}

std::string todayLocal() {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    tm l{};
    localtime_s(&l, &t);
    char buf[16];
    snprintf(buf, sizeof buf, "%04d-%02d-%02d", l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
    return buf;
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const fs::path& p, const std::string& content) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(content.data(), (std::streamsize)content.size());
    return f.good();
}

std::string patchFamily(const std::string& gameVersion) {
    size_t first = gameVersion.find('.');
    if (first == std::string::npos) return gameVersion;
    size_t second = gameVersion.find('.', first + 1);
    return second == std::string::npos ? gameVersion : gameVersion.substr(0, second);
}

std::string formatGameClock(int64_t tsMs) {
    int64_t totalSec = tsMs / 1000;
    char buf[16];
    snprintf(buf, sizeof buf, "%lld:%02lld", totalSec / 60, totalSec % 60);
    return buf;
}

} // namespace rl::util
