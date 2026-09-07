// Reads the plain metadata block of a .rofl replay (TASK-0022, EP-03).
//
// A .rofl ends with an unencrypted JSON block that carries 367 fields per
// player. This module reads that block and nothing else. The chunks and the
// keyframes stay untouched: they are encrypted, the obfuscation changes every
// patch, and reverse engineering them is forbidden (PRD 14.5, 17.2).
//
// Privacy: the block names all ten players. Only the row of the local player is
// matched, by puuid, and the puuid is dropped right after. Of the other nine
// this module keeps champion, team and metrics, never who they are
// (PRD 16, 29).
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rl {

struct RoflPlayer {
    bool        isUser = false;
    std::string champion;                 // SKIN
    int         team = 0;                 // 100 or 200
    bool        win = false;
    std::string position;                 // TEAM_POSITION
    std::map<std::string, int> metrics;   // only the keys of kRoflMetrics
};

struct RoflStats {
    bool        ok = false;
    std::string error;                    // what happened, why, what to do next
    std::string gameVersion;              // plain text, right after the magic
    int64_t     gameLengthMs = 0;
    int         chunks = 0;
    int         keyFrames = 0;
    std::vector<RoflPlayer> players;
    const RoflPlayer* user() const;
};

// The metrics kept. Every one of them is absent from match-v5, which is the
// only reason to open a replay at all. Behaviour fields of other players
// (mutes, afk, leaver) are deliberately not here: PRD 9.11 forbids blame.
const std::vector<std::string>& kRoflMetrics();

// Pure. bytes can be the whole file or its head plus its tail.
RoflStats parseRoflStats(const std::string& bytes, const std::string& myPuuid);

// Reads the head and the tail of the file only. A replay weighs 13 to 18 MB
// and the block sits in its last kilobytes.
RoflStats readRoflFile(const std::filesystem::path& file, const std::string& myPuuid);

// "LA2_1606112641" -> "<folder>/LA2-1606112641.rofl", when that file exists.
// An empty folder asks Windows for the Documents folder, which follows a
// OneDrive redirection. Returns an empty path when nothing matches.
std::filesystem::path roflPathForMatch(const std::string& matchId,
                                       const std::filesystem::path& folder = {});

} // namespace rl
