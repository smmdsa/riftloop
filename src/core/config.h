// Machine-local settings: %LOCALAPPDATA%\RiftLoop\config.json.
// The optional Riot dev API key is stored with DPAPI, never in clear text.
#pragma once
#include <string>

namespace rl {

struct Config {
    bool overlayEnabled = true;
    bool captureEnabled = false;     // recording opt-in, off by default (RF-REC-001)
    // Evidence clips built from the client replay. Opt-in and off by default:
    // producing one opens the game to play a replay (PRD 9.10, 13.1).
    bool clipsEnabled = false;
    // Writing the RiftLoop rune page to the client. Off by default and behind
    // its own kill switch (PRD 9.5 RF-RUN-003, 17.3). Needs one human click
    // every time: nothing is ever applied automatically.
    bool runeWriteEnabled = false;
    // After the analysis names the moments that matter, the recording is cut
    // into clips and the raw file is removed (RF-REC-003). Turn this on to keep
    // the full game as well; it costs about 1 GB per hour.
    bool keepFullRecording = false;
    bool lcuReadEnabled = true;      // kill flag (PRD 17.3); writes do not exist
    std::string routing = "americas";    // Riot API routing for optional fetch
    std::string leagueLockfilePath;      // "" = auto-detect
    std::string dataLocale;              // "" = auto (client locale)
    int  matchImportCount = 20;
    int  metaImportCount = 60;       // daily meta refresh batch (core/meta.h)
    int  overlayX = -1, overlayY = -1;   // -1 = default position

    static Config load();
    void save() const;

    // Optional local dev key (DPAPI-protected at rest).
    static std::string loadApiKey();
    static void saveApiKey(const std::string& key);   // empty removes it
};

} // namespace rl
