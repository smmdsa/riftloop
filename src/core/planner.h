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

// Runes prefer the local meta sample (core/meta.h) and fall back to the curated
// class templates when the sample is too small. The page always says which.
RunePlan  planRunes(Db& db, const Ddragon& dd, const PlanInput& in);

// Makes a page selectable in the client: keystone from row 0, one primary perk
// per row, two secondary perks from DIFFERENT rows, one shard per row. Repairs
// what it can and sets *degraded when it had to change anything. Exposed so a
// test can prove an impossible page never ships.
void validateRunePage(const Ddragon& dd, RunePage& page, bool* degraded);
SpellPlan planSpells(const Ddragon& dd, const PlanInput& in);
ItemPlan  planItems(Db& db, const Ddragon& dd, const PlanInput& in);

// Loading-screen quiz from the locked composition (PRD 9.8). Max 3 questions,
// only legitimate draft information.
std::vector<QuizQuestion> buildQuiz(const Ddragon& dd, const PlanInput& in);

} // namespace rl
