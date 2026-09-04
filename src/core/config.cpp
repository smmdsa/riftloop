#include "core/config.h"
#include "core/util.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <wincrypt.h>

using nlohmann::json;

namespace rl {

namespace {

std::filesystem::path configPath() { return util::dataDir() / "config.json"; }
std::filesystem::path keyPath()    { return util::dataDir() / "riot_key.bin"; }

} // namespace

Config Config::load() {
    Config c;
    try {
        json j = json::parse(util::readFile(configPath()));
        c.overlayEnabled = j.value("overlay_enabled", true);
        c.captureEnabled = j.value("capture_enabled", false);   // opt-in (RF-REC-001)
        c.lcuReadEnabled = j.value("lcu_read_enabled", true);
        c.routing = j.value("routing", "americas");
        c.leagueLockfilePath = j.value("league_lockfile_path", "");
        c.dataLocale = j.value("data_locale", "");
        c.clipsEnabled = j.value("clips_enabled", false);
        c.runeWriteEnabled = j.value("rune_write_enabled", false);
        c.keepFullRecording = j.value("keep_full_recording", false);
        c.matchImportCount = j.value("match_import_count", 20);
        c.metaImportCount = j.value("meta_import_count", 60);
        c.overlayX = j.value("overlay_x", -1);
        c.overlayY = j.value("overlay_y", -1);
    } catch (...) {}
    return c;
}

void Config::save() const {
    json j;
    j["overlay_enabled"] = overlayEnabled;
    j["capture_enabled"] = captureEnabled;
    j["lcu_read_enabled"] = lcuReadEnabled;
    j["routing"] = routing;
    j["league_lockfile_path"] = leagueLockfilePath;
    j["data_locale"] = dataLocale;
    j["clips_enabled"] = clipsEnabled;
    j["rune_write_enabled"] = runeWriteEnabled;
    j["keep_full_recording"] = keepFullRecording;
    j["match_import_count"] = matchImportCount;
    j["meta_import_count"] = metaImportCount;
    j["overlay_x"] = overlayX;
    j["overlay_y"] = overlayY;
    util::writeFile(configPath(), j.dump(2));
}

std::string Config::loadApiKey() {
    std::string blob = util::readFile(keyPath());
    if (blob.empty()) return {};
    DATA_BLOB in{(DWORD)blob.size(), (BYTE*)blob.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return {};
    std::string key((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return key;
}

void Config::saveApiKey(const std::string& key) {
    if (key.empty()) {
        std::error_code ec;
        std::filesystem::remove(keyPath(), ec);
        return;
    }
    DATA_BLOB in{(DWORD)key.size(), (BYTE*)key.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"RiftLoop Riot API key", nullptr, nullptr, nullptr, 0, &out))
        return;
    util::writeFile(keyPath(), std::string((char*)out.pbData, out.cbData));
    LocalFree(out.pbData);
}

} // namespace rl
