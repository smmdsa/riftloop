// Where the user kills and dies, aggregated over the last N matches.
// Reads only what is already on disk: matches.timeline_json (RF-POST, EP-03).
#pragma once
#include "core/db.h"
#include "core/models.h"

#include <string>
#include <vector>

namespace rl {

// The timeline coordinate space of Summoner's Rift. The origin sits at the
// bottom-left corner, so a drawing surface must flip the Y axis.
constexpr int kRiftMin = 0;
constexpr int kRiftMax = 14820;

// Lane phase ends at minute 14. A death before it and a death after it are not
// the same problem, so every consumer can split the cloud on this line.
constexpr int64_t kLanePhaseEndMs = 14 * 60 * 1000;

enum class HeatKind { Death, Kill, Assist };

struct HeatPoint {
    HeatKind kind = HeatKind::Death;
    int      x = 0;
    int      y = 0;
    int64_t  tsMs = 0;            // game clock of the event
    std::string matchId;
};

struct HeatQuery {
    int         matches = 20;     // how many of the latest matches to read
    std::string role;             // "" = every role
    std::string champion;         // "" = every champion
};

struct HeatMap {
    std::vector<HeatPoint> points;
    int matchesRead = 0;          // matches that gave at least a timeline
    int matchesSkipped = 0;       // no timeline, or the user was not in them
    std::string note;             // why the map is empty, when it is empty

    int count(HeatKind k) const;
    // early = before minute 14. Pass false for the rest of the game.
    int count(HeatKind k, bool early) const;
};

// A point on a square drawing surface, with the origin at its top-left corner.
struct CanvasPoint {
    int x = 0;
    int y = 0;
};

// Projects a Rift coordinate onto a square of the given side. The Rift origin
// sits at the bottom-left corner, so the Y axis flips here. A drawing that
// skips this flip puts the blue base at the top right.
CanvasPoint projectToCanvas(int riftX, int riftY, int side);

// Adds the events of one already parsed match. userId is the participantId of
// the local player. An event with no position is skipped, never placed at 0,0.
void addMatchHeat(const Timeline& t, int userId, HeatMap& out);

// Walks the last q.matches stored matches. Never downloads anything.
HeatMap collectHeat(Db& db, const HeatQuery& q);

} // namespace rl
