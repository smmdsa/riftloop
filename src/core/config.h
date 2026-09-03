// Machine-local settings: %LOCALAPPDATA%\RiftLoop\config.json.
// The optional Riot dev API key is stored with DPAPI, never in clear text.
#pragma once
#include <string>

namespace rl {

struct Config {
    bool overlayEnabled = true;
    bool captureEnabled = false;     // capture is out of iteration 1; kept off
    bool lcuReadEnabled = true;      // kill flag (PRD 17.3); writes do not exist
    std::string routing = "americas";    // Riot API routing for optional fetch
    std::string leagueLockfilePath;      // "" = auto-detect
    int  matchImportCount = 20;

    static Config load();
    void save() const;

    // Optional local dev key (DPAPI-protected at rest).
    static std::string loadApiKey();
    static void saveApiKey(const std::string& key);   // empty removes it
};

} // namespace rl
