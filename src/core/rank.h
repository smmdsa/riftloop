// The user's own rank, read from the client and remembered with its date
// (TASK-0024, EP-03).
//
// Tier and division only. PRD 7 forbids any score equivalent to an alternate
// MMR, so nothing here is derived, averaged or estimated. A rank with no date
// is a rank that ages in silence, so the date travels with the value.
#pragma once
#include "core/db.h"
#include "core/lcu.h"

#include <string>

namespace rl {

struct RankView {
    bool     known = false;      // there is something to show
    bool     live = false;       // read from the client in this call
    LcuRank  rank;
    std::string note;            // why there is nothing, or how old it is
};

// Reads the client when it is connected and remembers the answer. With no
// client it returns what was stored, and the note says when that was read.
// A stored value younger than maxAgeSec is reused without asking the client:
// this runs on every refresh of the profile page, and a rank does not move
// between two repaints.
RankView currentRankView(Db& db, Lcu* lcu, int maxAgeSec = 300);

// The stored value alone. Never touches the client.
RankView storedRankView(Db& db);

} // namespace rl
