// League Client API, strictly read-only (PRD 17.2). Finds the lockfile,
// authenticates with its credentials and reads gameflow + champ select.
// The LCU has no SLA: every call degrades to "unknown" (PRD 13.1).
#pragma once
#include "core/ddragon.h"
#include "core/models.h"

#include <optional>
#include <string>
#include <vector>

namespace rl {

struct LcuConnection {
    int         port = 0;
    std::string password;
};

struct ChampSelectView {
    std::string assignedRole;            // TOP/JUNGLE/MIDDLE/BOTTOM/UTILITY
    int         localChampionId = 0;     // 0 = not locked
    std::vector<int> allyChampionIds;    // excluding local player
    std::vector<int> enemyChampionIds;
    std::vector<int> banIds;
    std::vector<int> pickableChampionIds;
};

class Lcu {
public:
    // Locates the lockfile: explicit path from config, then default installs,
    // then the running LeagueClientUx process directory.
    bool connect(const std::string& configuredLockfilePath = "");
    bool connected() const { return conn_.port != 0; }

    // "None","Lobby","Matchmaking","ReadyCheck","ChampSelect","InProgress",
    // "WaitingForStats","PreEndOfGame","EndOfGame" or "" on error.
    std::string gameflowPhase();

    std::optional<ChampSelectView> champSelect();

    GameState toGameState(const std::string& phase) const;

private:
    std::string get(const std::string& path);
    LcuConnection conn_;
};

// Live Client Data API (game process, 127.0.0.1:2999). Liveness only.
bool liveGameRunning();

} // namespace rl
