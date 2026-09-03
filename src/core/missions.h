// Mission engine (PRD 9.14, 12.1-12.3): one primary mission, blocks of 3-5
// games, opportunity-based verification, skill tree updates.
#pragma once
#include "core/db.h"
#include "core/models.h"

#include <optional>
#include <string>

namespace rl {

// Longitudinal prioritization over recent analyses (PRD 12.1):
// priority = recurrence x severity x controllability x confidence.
// Returns a suggested mission, or nullopt when no pattern earns one.
std::optional<Mission> suggestMission(Db& db);

// Marks the mission active. Any other active mission is discarded first
// (PRD 3.4: one primary mission).
void activateMission(Db& db, int64_t missionId);

// Called after each analysis: records opportunities for the active mission
// and evaluates the block when enough data exists.
void trackMatchForMission(Db& db, const AnalysisResult& res);

struct MissionProgress {
    Mission mission;
    int gamesTracked = 0;
    int validOpportunities = 0;
    int successes = 0;
};
std::optional<MissionProgress> activeMissionProgress(Db& db);

// Mission template for a detector (used by suggestMission and tests).
Mission missionTemplate(const std::string& detectorId);

} // namespace rl
