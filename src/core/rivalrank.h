// Official rank of the ten players of a match already played (TASK-0025).
//
// Policy, and it is the whole reason this file is shaped like this:
// PRD 17.2 marks scouting of a hidden identity as red. In champion select the
// enemies are anonymised, so nothing here may run there. In the history of a
// game already played those names are public to the user, and that half is
// green. The barrier is not a comment: every entry point needs a matchId that
// is already stored in the matches table, and champion select has none.
//
// What is stored: tier, division, LP and a date, keyed by (matchId,
// participantId). No puuid, no name, no summonerId ever reaches the database
// (PRD 16, 29). The user chose this on 2026-09-07.
#pragma once
#include "core/db.h"
#include "core/ingest.h"

#include <functional>
#include <string>
#include <vector>

namespace rl {

struct RivalRank {
    int         participantId = 0;
    std::string tier;              // "" = the queue has no rank for this player
    std::string division;
    int         leaguePoints = 0;
    int64_t     readAtEpoch = 0;

    bool cached() const { return readAtEpoch > 0; }
    bool ranked() const { return !tier.empty(); }
    std::string display() const;   // "BRONZE II · 34 LP" / "Sin clasificar"
};

struct MatchRankView {
    std::vector<RivalRank> rows;   // one per stored row, by participantId
    int known = 0;                 // rows with a rank
    int unranked = 0;              // rows read, with no rank in this queue
    int missing = 0;               // participants never read
    // The average of the official tiers, or empty when too many are missing.
    // It never becomes a number the user could read as an alternate MMR
    // (PRD 7): it is one of the official steps, and nothing else.
    std::string averageTier;
    std::string averageDivision;
    std::string note;              // how many are missing, and what to do

    const RivalRank* byParticipant(int pid) const;
};

// Reads the cache only. Never touches the network.
MatchRankView storedMatchRanks(Db& db, const std::string& matchId);

struct RankFetchProgress {
    int done = 0;
    int total = 0;
    std::string label;
};

struct RankFetchResult {
    bool ok = false;
    int  fetched = 0;
    int  failed = 0;
    std::string message;
};

// Asks Riot for the ranks the cache does not have. Two calls per player:
// summoner-v4 by puuid, then league-v4 by summoner id.
//
// onlyParticipantId 0 refreshes every player of the match; any other value
// refreshes just that one. force re-reads rows that are already cached.
// spacingMs separates the calls so a development key stays inside its quota:
// 20 calls at 1300 ms fit in the 100 requests per 2 minutes it allows.
RankFetchResult refreshMatchRanks(Db& db, const std::string& matchId,
                                  const RiotApiConfig& api,
                                  int onlyParticipantId = 0,
                                  bool force = false,
                                  const std::function<void(RankFetchProgress)>& onProgress = {},
                                  int spacingMs = 1300);

// How long a full refresh of one match takes, in seconds. The UI says this
// before it starts: the user asked to be warned.
int estimatedRefreshSeconds(int players, int spacingMs = 1300);

// "LA2_1622503726" -> "la2". Empty when the id has no platform.
std::string platformOfMatch(const std::string& matchId);

} // namespace rl
