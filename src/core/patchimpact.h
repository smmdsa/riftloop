// Personalized patch impact (PRD 9.17): compares cached Data Dragon versions
// and reports direct stat changes for the user's champion pool.
#pragma once
#include "core/db.h"
#include "core/ddragon.h"

#include <string>
#include <vector>

namespace rl {

struct PatchImpactEntry {
    std::string champion;
    std::string change;              // "cambio directo: ..." | "sin cambio directo detectado"
    bool        direct = false;
};

struct PatchImpactReport {
    std::string fromVersion;         // "" when no previous snapshot exists
    std::string toVersion;
    bool        earlyData = true;    // always true right after a patch
    std::vector<PatchImpactEntry> entries;
    std::string note;
};

PatchImpactReport patchImpact(Db& db, const Ddragon& dd);

} // namespace rl
