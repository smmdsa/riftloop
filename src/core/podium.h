// The podium of one match: the ten players ordered, and why (TASK-0026).
//
// This scores ONE GAME. It is not a rating of a player, it is never stored, and
// it is never compared across matches. PRD 7 forbids a public score equivalent
// to an alternate MMR, and that is exactly what a persisted version of this
// would be.
//
// Every number the UI shows comes from here with its own breakdown attached, so
// the view explains the score instead of inventing an explanation (PRD 30).
// No title is negative: PRD 9.11 forbids naming a teammate as a cause, so the
// bottom of the podium carries a position and nothing else.
#pragma once
#include "core/models.h"

#include <string>
#include <vector>

namespace rl {

// One line of the breakdown: what it measures, the raw numbers behind it, and
// how many of the 100 points it contributed.
struct PodiumFactor {
    std::string label;
    std::string detail;
    int         points = 0;
    int         weight = 0;
};

struct PodiumEntry {
    int         participantId = 0;
    int         rank = 0;            // 1 = best of the ten
    int         score = 0;           // 0..100
    int         teamSharePct = 0;    // share of the team's plays
    std::string title;               // "" when this row has no title
    std::string titleWhy;            // one line, why this row earned it
    std::vector<PodiumFactor> factors;
};

struct MatchPodium {
    std::vector<PodiumEntry> entries;   // best first
    const PodiumEntry* byParticipant(int participantId) const;
};

// Pure. Needs nothing but the match already stored on disk.
MatchPodium buildPodium(const MatchSummary& m);

} // namespace rl
