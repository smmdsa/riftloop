// Explanation contracts (PRD 30): every relevant recommendation serializes to
// an auditable JSON object. The UI consumes this object; it does not invent
// explanations.
#pragma once
#include "core/models.h"
#include "core/serial.h"
#include "core/util.h"

namespace rl {

inline json makeContract(const std::string& type, const std::string& patch,
                         const json& inputsUsed, const json& payload,
                         const std::string& confidence,
                         const std::string& uncertaintyReason) {
    static int counter = 0;
    json c;
    c["recommendation_id"] = util::nowIso() + "-" + type + "-" + std::to_string(++counter);
    c["type"] = type;
    c["patch"] = patch;
    c["inputs_used"] = inputsUsed;
    c["options"] = payload;
    c["confidence"] = confidence;
    c["uncertainty_reason"] = uncertaintyReason;
    c["model_or_ruleset_version"] = kRulesetVersion;
    c["expires_at"] = "next-patch";
    c["policy_mode"] = "read_only";      // no writes to the League client (PRD 17)
    return c;
}

} // namespace rl
