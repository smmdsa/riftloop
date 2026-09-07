// Gold, cs and xp over the minutes of one match, next to the user's own median
// in the same role. The reference is the user against themselves: there is no
// global percentile here, and inventing one would be a made-up score (PRD 12.2).
#pragma once
#include "core/db.h"
#include "core/models.h"

#include <string>
#include <vector>

namespace rl {

enum class Series { Gold, Cs, Xp };
inline constexpr int kSeriesCount = 3;

struct CurvePoint {
    int minute = 0;
    int value = 0;
};

struct Curve {
    std::vector<CurvePoint> points;
    int last() const { return points.empty() ? 0 : points.back().value; }
    int maxValue() const;
};

// The median of the same role over the user's other matches. It carries its
// own sample size, because a band built on two games explains nothing.
struct Band {
    Curve median;
    int   samples = 0;
};

struct MatchCurves {
    std::string matchId;
    std::string role;
    std::string champion;
    Curve user[kSeriesCount];
    Curve lane[kSeriesCount];        // the lane opponent of this same match
    std::string laneChampion;        // "" when no opponent shares the position
    Band  band[kSeriesCount];
    std::vector<int64_t> marks;      // evidence instants of the stored analysis
    int   durationSec = 0;
    std::string note;                // why something is missing, when it is
    bool  ok = false;
};

const char* seriesName(Series s);

// Reads one stored match and, for the band, the user's other matches in the
// same role. Never downloads anything.
MatchCurves buildCurves(Db& db, const std::string& matchId, int bandMatches = 20);

} // namespace rl
