// Post-match analysis pipeline: run detectors, prioritize, persist.
#pragma once
#include "core/db.h"
#include "core/ddragon.h"
#include "core/models.h"

#include <optional>
#include <string>

namespace rl {

// Analyzes one stored match. Returns nullopt when the match lacks a timeline
// or the local player cannot be identified.
std::optional<AnalysisResult> analyzeMatch(Db& db, const Ddragon* dd, const std::string& matchId);

// Analyzes every pending match. Returns how many completed.
int analyzePending(Db& db, const Ddragon* dd);

// Resolve the local player's puuid: explicit profile value, or the only puuid
// present in every stored match (single-user local app).
std::string resolveUserPuuid(Db& db);

} // namespace rl
