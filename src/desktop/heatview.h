// The heat map control: the Rift, and the user's kills and deaths on it.
// It draws only what core/heatmap.h already aggregated (TASK-0020, EP-03).
#pragma once
#include "core/heatmap.h"

#include <windows.h>

#include <string>
#include <vector>

namespace rlui {

struct HeatViewData {
    std::vector<rl::HeatPoint> points;
    int          matches = 0;        // how many matches the cloud comes from
    std::wstring note;               // drawn instead of the map when not empty
    std::string  mapUrl;             // Data Dragon minimap; "" draws the board
};

void registerHeatView(HINSTANCE inst);

// Replaces the content. The layer toggles keep their state across a set, so a
// refresh does not undo what the user just switched off.
void hmSet(HWND view, HeatViewData data);

} // namespace rlui
