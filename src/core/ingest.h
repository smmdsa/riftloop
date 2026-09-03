// Parse Riot match-v5 and timeline JSON into model structs.
// Sources: local JSON files (always) or Riot API with an optional local dev
// key (goal: no servers in MVP1; the PRD server-side-key rule applies later).
#pragma once
#include "core/models.h"

#include <optional>
#include <string>

namespace rl {

// Accepts a match-v5 payload ({"metadata":..,"info":..}).
std::optional<MatchSummary> parseMatch(const std::string& jsonText);

// Accepts a timeline payload ({"metadata":..,"info":{"frames":[..]}}).
std::optional<Timeline> parseTimeline(const std::string& jsonText, const std::string& matchId);

// Accepts a combined export {"match": {...}, "timeline": {...}} or a plain
// match payload. Returns raw pair (match, timeline) as JSON strings.
struct RawPair { std::string matchJson, timelineJson; };
std::optional<RawPair> splitImport(const std::string& fileText);

struct RiotApiConfig {
    std::string apiKey;              // optional local dev key
    std::string routing = "americas";    // americas | europe | asia | sea
};

// Fetch helpers. They return raw JSON or empty on error (message in *error).
std::string fetchMatchIds(const RiotApiConfig& c, const std::string& puuid, int count,
                          std::string* error);
std::string fetchMatch(const RiotApiConfig& c, const std::string& matchId, std::string* error);
std::string fetchTimeline(const RiotApiConfig& c, const std::string& matchId, std::string* error);
std::string fetchPuuidByRiotId(const RiotApiConfig& c, const std::string& gameName,
                               const std::string& tagLine, std::string* error);

} // namespace rl
