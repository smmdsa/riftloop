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

struct ChampSelectSeat {
    int       championId = 0;            // 0 = nobody picked or hovered yet
    PickState state = PickState::Empty;
};

struct ChampSelectView {
    std::string assignedRole;            // TOP/JUNGLE/MIDDLE/BOTTOM/UTILITY
    // Five seats per side, sorted by cell id. The index of a player never
    // changes during the draft, so a late pick does not move the others.
    std::array<ChampSelectSeat, 5> allySeats{};
    std::array<ChampSelectSeat, 5> enemySeats{};
    int localSeat = -1;                  // index into allySeats, -1 = unknown
    // Completed bans, in draft order inside each side. The client attributes a
    // ban by its summary lists or by the actor cell of the action; when it does
    // neither, the ban lands in unknownBanIds and no side claims it.
    std::vector<int> allyBanIds;
    std::vector<int> enemyBanIds;
    std::vector<int> unknownBanIds;
    std::vector<int> pickableChampionIds;

    // Every ban, whatever the side. Availability does not care who banned.
    std::vector<int> allBanIds() const;

    // The user's champion, derived from the seat. Locked returns the pick;
    // hover returns 0, because a hover is not a pick.
    int localChampionId() const {
        return localSeat >= 0 && allySeats[localSeat].state == PickState::Locked
                   ? allySeats[localSeat].championId
                   : 0;
    }
    int localHoverChampionId() const {
        return localSeat >= 0 && allySeats[localSeat].state == PickState::Hover
                   ? allySeats[localSeat].championId
                   : 0;
    }
};

// Pure parser for the LCU champ select payloads. The session shape is the one
// of /lol-champ-select/v1/session; pickableJson is the array returned by
// /lol-champ-select/v1/pickable-champion-ids. Exposed for tests.
std::optional<ChampSelectView> parseChampSelect(const std::string& sessionJson,
                                                const std::string& pickableJson);

struct LcuSummoner {
    std::string puuid;
    std::string riotId;              // gameName#tagLine
};

// The official rank of the logged-in player. Tier and division only: PRD 7
// forbids any score equivalent to an alternate MMR, so nothing is derived.
struct LcuRank {
    std::string tier;                // "BRONZE", "" when unranked
    std::string division;            // "II"; empty in the apex tiers
    int         leaguePoints = 0;
    int         wins = 0;
    int         losses = 0;
    std::string queue = "RANKED_SOLO_5x5";
    std::string readAtIso;           // when the client answered, UTC

    bool ranked() const { return !tier.empty(); }
    // "BRONZE II · 34 LP · 164V 186D"
    std::string display() const;
};

// Pure parser for /lol-ranked/v1/current-ranked-stats. Exposed for tests.
std::optional<LcuRank> parseRankedStats(const std::string& json,
                                        const std::string& queue = "RANKED_SOLO_5x5");

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

    // Logged-in player; empty on error.
    std::optional<LcuSummoner> currentSummoner();
    // Client locale, e.g. "es_AR"; "" on error.
    std::string clientLocale();

    // Official rank of the logged-in player, straight from the client and with
    // no Riot API key. Empty when League is closed or the queue is unranked.
    std::optional<LcuRank> currentRank(const std::string& queue = "RANKED_SOLO_5x5");

    // Match history of the logged-in player, straight from the client
    // (no Riot API key). Read-only, no SLA (PRD 13.1).
    std::vector<int64_t> recentGameIds(int count);
    std::string gameJson(int64_t gameId);        // full 10-player game (v4 shape)
    std::string gameTimelineJson(int64_t gameId);    // v4 timeline frames

    GameState toGameState(const std::string& phase) const;

    // Raw access for modules that need endpoints beyond the ones above
    // (core/replays.h). Both return "" on error. POST bodies are JSON.
    std::string getRaw(const std::string& path, int timeoutMs = 2000);
    bool        postRaw(const std::string& path, const std::string& jsonBody,
                        std::string* response = nullptr, int timeoutMs = 4000);
    // Any verb. Used by the rune page writer, the only module that needs
    // PUT and DELETE, and only on the page RiftLoop owns (PRD 9.5).
    bool        requestRaw(const std::string& method, const std::string& path,
                           const std::string& jsonBody, std::string* response = nullptr,
                           int timeoutMs = 4000);

private:
    std::string get(const std::string& path);
    LcuConnection conn_;
};

// Live Client Data API (game process, 127.0.0.1:2999).
bool liveGameRunning();

// Seconds elapsed in the running game, or -1 when no game answers. This is the
// only reliable way to align a recording with the match clock: recording starts
// when the agent notices the game, not at minute zero (RF-REC-003).
double liveGameTimeSec();

// Seconds the Live Client Data clock had already counted when the match clock
// hit 0:00. The API starts counting when the game process starts; the Riot
// timeline counts from 0:00. Without this offset a cut lands minutes away.
// Returns -1 when no game answers or the event is not there yet.
double liveGameStartOffsetSec();

} // namespace rl
