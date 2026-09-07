// Explainable post-match detectors D01..D10 (PRD 9.12).
// Every detector counts valid opportunities and failures (PRD 12.2) and
// attaches timeline evidence with separated facts and inference (PRD 3.3).
#pragma once
#include "core/ddragon.h"
#include "core/models.h"
#include "core/rofl.h"

#include <vector>

namespace rl {

struct DetectorInput {
    const MatchSummary& match;
    const Timeline&     timeline;
    int                 userId = 0;  // participantId of the local player
    const Ddragon*      dd = nullptr;    // optional (D09 needs it)
    // Plain metadata of the .rofl, when the replay is still on disk. Optional:
    // the client deletes old replays, and every rule here works without it.
    const RoflStats*    rofl = nullptr;
};

// Runs every detector. Returns only findings with at least one opportunity.
std::vector<Finding> runDetectors(const DetectorInput& in);

// One observed strength for the summary viewport (RF-POST-001).
std::string detectStrength(const DetectorInput& in);

// Skill-tree domain for a detector id ("D03" -> "recalls-economia").
std::string detectorDomain(const std::string& detectorId);
// Controllability weight 0..1 for prioritization (PRD 12.1).
double detectorControllability(const std::string& detectorId);

} // namespace rl
