// Turns a stored match into an anonymous test fixture (PRD 16, 29).
// The scrub runs while the file is written, never as a second pass over a file
// that already holds a name.
#pragma once
#include <string>

namespace rl {

struct FixtureExport {
    bool        ok = false;
    std::string text;            // {"match": ..., "timeline": ...}
    std::string error;           // what happened, why, what to do next
    int         participants = 0;
    int         frames = 0;
    int         events = 0;
    int         framesWithPosition = 0;
    int         eventsWithPosition = 0;
};

// Rewrites both payloads with no identity in them. The player who owns myPuuid
// becomes "me-puuid". The other nine become "p2".."p10" in participantId order.
// Every other field stays as it is: position, xp, gold, cs, level, timestamps
// and championId carry no identity and the tests need them.
// newMatchId replaces the real match id, which resolves to identities through
// the Riot API. An empty newMatchId keeps the stored id.
FixtureExport anonymizeFixture(const std::string& matchJson,
                               const std::string& timelineJson,
                               const std::string& myPuuid,
                               const std::string& newMatchId);

} // namespace rl
