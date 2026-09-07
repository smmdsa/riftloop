// The curve control: gold, cs and xp by minute, over the band of the user's
// own median in that role (TASK-0021, EP-03).
#pragma once
#include "core/curves.h"

#include <windows.h>

namespace rlui {

void registerCurveView(HINSTANCE inst);

// Replaces the content. The chosen series survives the change, so a refresh
// does not throw the reader back to the first tab.
void cvSet(HWND view, rl::MatchCurves curves);

} // namespace rlui
