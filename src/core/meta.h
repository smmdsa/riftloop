// Local meta: what actually wins on the installed patch, aggregated from the
// user's own imported matches (10 builds and 10 rune pages per game).
//
// No scraping and no third-party site: the sample comes from the client's own
// match history (PRD 13.1). The sample is small and biased towards the user's
// elo and region, so every result carries its count and says so (RF-ITEM-003).
#pragma once
#include "core/db.h"
#include "core/ddragon.h"

#include <optional>
#include <string>
#include <vector>

namespace rl {

// Minimum rows before a result is offered at all. Below this the caller must
// fall back to the curated templates.
inline constexpr int kMetaMinSample = 5;

// A rune page is only offered when one page clearly dominates the sample: at
// least kMetaMinPage rows AND kMetaMinPageShare of them. A sample split evenly
// across five pages answers nothing, so it falls back to the template.
inline constexpr int    kMetaMinPage = 3;
inline constexpr double kMetaMinPageShare = 0.40;

// Enemy comp the page has to answer. Unknown before the draft fills in.
struct ThreatContext {
    bool   known = false;
    double burst = 0;
    double cc = 0;
};

// Thresholds the planner uses to name a threat in the page reasons. The
// matchup filter does NOT use them: it cuts the sample at its own median,
// because a fixed number does not separate real comps (see narrowToMatchup).
inline constexpr double kThreatBurst = 1.8;
inline constexpr double kThreatCc = 2.0;

struct MetaSample {
    int         games = 0;
    int         wins = 0;
    int         userRows = 0;        // rows the profile owner played
    std::string patch;               // patch the sample came from
    bool        fromOlderPatch = false;
    std::string queueLabel;          // "ranked" | "normales"
    bool        matchupFiltered = false;   // narrowed to a similar enemy comp
    std::string confidence;          // "baja" | "media" | "alta"
    // One line naming the sample and its limits. Never omit it in the UI.
    std::string note() const;
};

struct MetaRunes {
    int              primaryStyle = 0;
    int              subStyle = 0;
    std::vector<int> perks;          // 4 primary + 2 secondary, most common per slot
    MetaSample       sample;
};

struct MetaItems {
    std::vector<int> core;           // most common completed items, by frequency
    std::vector<int> boots;
    std::vector<int> starting;       // not derivable from final items; empty for now
    MetaSample       sample;
};

// Both return nullopt when the sample is under kMetaMinSample. They first look
// at the requested patch, then fall back to any patch and flag it.
std::optional<MetaRunes> metaRunes(Db& db, const std::string& champion,
                                   const std::string& role, const std::string& patch,
                                   ThreatContext threat = {});
std::optional<MetaItems> metaItems(Db& db, const Ddragon& dd, const std::string& champion,
                                   const std::string& role, const std::string& patch);

// Rows currently held for this champion/role/patch. Used by the UI to explain
// why a recommendation is a template instead of data.
int metaSampleSize(Db& db, const std::string& champion, const std::string& role,
                   const std::string& patch);

} // namespace rl
