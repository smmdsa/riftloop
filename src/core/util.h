// Shared helpers: paths, strings, time.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace rl {

// Version of the whole client. Bump the string on a release; the build stamp
// comes from the compiler, so two builds of the same day are still tellable
// apart by the time.
inline constexpr const char* kAppVersion = "0.2.0";
inline const char* appBuildStamp() { return __DATE__ " " __TIME__; }

} // namespace rl

namespace rl::util {

// %LOCALAPPDATA%\RiftLoop, created on first call.
std::filesystem::path dataDir();
std::filesystem::path cacheDir();    // dataDir()/cache

std::wstring widen(const std::string& utf8);
std::string  narrow(const std::wstring& wide);

std::string nowIso();                // "2026-09-02T20:00:00Z"
std::string todayLocal();            // "2026-09-02" local time

std::string readFile(const std::filesystem::path& p);          // "" on error
bool        writeFile(const std::filesystem::path& p, const std::string& content);

// "16.17.702.1234" -> "16.17"
std::string patchFamily(const std::string& gameVersion);

std::string formatGameClock(int64_t tsMs);                     // "18:42"

} // namespace rl::util
