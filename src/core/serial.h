// JSON (de)serialization for model structs. Used by db, ipc and contracts.
#pragma once
#include "core/models.h"
#include <nlohmann/json.hpp>

namespace rl {
using nlohmann::json;

// ---- enums ------------------------------------------------------------------
NLOHMANN_JSON_SERIALIZE_ENUM(PoolTier, {
    {PoolTier::Main, "main"}, {PoolTier::Comfort, "comfort"},
    {PoolTier::Learning, "learning"}, {PoolTier::DoNotRecommend, "no_recommend"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(AppMode, {
    {AppMode::Escalar, "escalar"}, {AppMode::Aprender, "aprender"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(MissionStatus, {
    {MissionStatus::Suggested, "suggested"}, {MissionStatus::Active, "active"},
    {MissionStatus::Evaluated, "evaluated"}, {MissionStatus::Discarded, "discarded"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(MissionResult, {
    {MissionResult::None, "none"}, {MissionResult::Improved, "improved"},
    {MissionResult::Partial, "partial"}, {MissionResult::NoChange, "no_change"},
    {MissionResult::NotEnoughData, "not_enough_data"},
    {MissionResult::DetectorWrong, "detector_wrong"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(SkillState, {
    {SkillState::NotEvaluated, "not_evaluated"}, {SkillState::Introduced, "introduced"},
    {SkillState::Practicing, "practicing"}, {SkillState::Consistent, "consistent"},
    {SkillState::Mastered, "mastered"}, {SkillState::NeedsRefresh, "needs_refresh"},
})

// ---- structs ----------------------------------------------------------------
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PoolEntry, champion, role, tier, declaredGames)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Profile, riotId, puuid, region, mode,
                                                preferredRoles, pool, language)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Evidence, evidenceId, matchId, gameTimestampMs,
                                                source, observedFacts, inference, confidence,
                                                exclusionsChecked, clipFile)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Finding, detectorId, matchId, title, whyItMatters,
                                                alternative, confidence, severity, opportunities,
                                                failures, evidence)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AnalysisResult, matchId, rulesetVersion, strength,
                                                findings, limitations)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Mission, detectorId, name, hypothesis, metric,
                                                appliesWhen, doesNotApply, blockSize,
                                                targetSuccesses, status, result, createdAt)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Opportunity, missionId, matchId, valid, success, note)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SkillNode, domain, state, confidence, opportunitiesSeen)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChampCard, champion, label, reasons, risk,
                                                experience, confidence)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Top3, cards, uncertaintyReason, patch, available,
                                                unavailableReason)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RunePage, name, primaryStyle, subStyle, perks, reasons,
                                                intent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpellPlan, spells, reason)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ItemBranch, label, items, condition)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ItemPlan, starting, firstBack, firstBackGold, core,
                                                boots, branches, confidence, datasetNote)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(QuizQuestion, conceptTag, text, options, correctIndex,
                                                explanation)

inline void to_json(json& j, const RunePlan& r) {
    j = json{{"main", r.main},
             {"confidence", r.confidence},
             {"draftClosed", r.draftClosed},
             {"missingPicks", r.missingPicks},
             {"noAlternativeReason", r.noAlternativeReason}};
    if (r.situational) j["situational"] = *r.situational;
}
inline void from_json(const json& j, RunePlan& r) {
    j.at("main").get_to(r.main);
    r.confidence = j.value("confidence", "media");
    r.draftClosed = j.value("draftClosed", false);
    r.missingPicks = j.value("missingPicks", 0);
    r.noAlternativeReason = j.value("noAlternativeReason", "");
    if (j.contains("situational")) r.situational = j.at("situational").get<RunePage>();
}

} // namespace rl
