// Import matches straight from the League client match history (no API key).
// The LCU serves v4-shaped payloads; this module converts them to the
// match-v5 shape that the rest of the pipeline consumes.
#pragma once
#include "core/db.h"
#include "core/ddragon.h"
#include "core/lcu.h"

#include <nlohmann/json.hpp>

#include <string>

namespace rl {

// Repairs the teamPosition of an already converted v5 payload, in place.
// Returns how many participants it moved.
//
// The client mislabels two roles, measured on 2026-09-07 (see the .cpp):
// the top laner arrives as JUNGLE, and before this the support arrived as
// BOTTOM. Both are separated by numbers that survive into the stored payload,
// so a match imported before the fix can be repaired without asking Riot
// again.
int repairPositionsV5(nlohmann::json& participants);

// v4 game object -> v5-shaped {"metadata":{...},"info":{...}}.
// dd resolves championId -> name; pass nullptr to keep names empty.
nlohmann::json convertLcuGameToV5(const nlohmann::json& lcuGame, const Ddragon* dd);

// v4 timeline -> v5-shaped {"metadata":{...},"info":{"frames":[...]}}.
// v5Match provides participant teams to derive killerTeamId for objective
// events (v4 building events carry the team of the DESTROYED building).
nlohmann::json convertLcuTimelineToV5(const nlohmann::json& lcuTimeline,
                                      const std::string& matchId,
                                      const nlohmann::json& v5Match);

struct LcuImportResult {
    int imported = 0;
    int skipped = 0;
    std::string error;               // "" on success
};

// Downloads up to `count` recent classic games from the client, converts and
// stores them. Also fills the profile's puuid/riotId from the client.
LcuImportResult importFromClient(Db& db, Lcu& lcu, const Ddragon* dd, int count);

// Re-downloads matches whose stored json predates the rune block and replaces
// it. An older import could not carry `perks`, and the meta sample needs it.
// Returns how many matches it updated; games the client no longer serves are
// skipped silently.
int refetchRunePages(Db& db, Lcu& lcu, const Ddragon* dd);

// Data Dragon locale: configured value > client locale (remembered in kv) >
// last remembered > "en_US".
std::string resolveDataLocale(Db& db, Lcu* lcu, const std::string& configured);

} // namespace rl
