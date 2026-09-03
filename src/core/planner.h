// Pregame planner: runes, summoner spells and the item decision tree
// (PRD 9.5-9.7). Read-only output; nothing is written to the League client
// (writes are Approval-Gated, PRD 17.2).
#pragma once
#include "core/db.h"
#include "core/ddragon.h"
#include "core/models.h"

namespace rl {

struct PlanInput {
    std::string champion;            // user's locked champion (ddragon id)
    std::string role;
    DraftContext draft;              // for threat vectors
};

RunePlan  planRunes(const Ddragon& dd, const PlanInput& in);
SpellPlan planSpells(const Ddragon& dd, const PlanInput& in);
ItemPlan  planItems(Db& db, const Ddragon& dd, const PlanInput& in);

// Loading-screen quiz from the locked composition (PRD 9.8). Max 3 questions,
// only legitimate draft information.
std::vector<QuizQuestion> buildQuiz(const Ddragon& dd, const PlanInput& in);

} // namespace rl
