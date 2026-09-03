// Champion select Top 3 (PRD 9.4, 11). Rule-based scoring over the user's
// pool, personal proficiency and composition traits. No win probability is
// shown: it is not calibrated (RF-CS-002).
#pragma once
#include "core/db.h"
#include "core/ddragon.h"
#include "core/models.h"

namespace rl {

// Personal proficiency 0..1 from stored matches + declared games (PRD 11.2).
double proficiency(Db& db, const std::string& champion, const std::string& role);

Top3 recommendTop3(Db& db, const Ddragon& dd, const DraftContext& ctx);

} // namespace rl
