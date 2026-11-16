// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_progress(RoutineRegistry& registry);
struct ProgressSegment { RoutineEntry entry,unit; };
inline constexpr std::array<ProgressSegment,8> interaction_progress_segments{{
    {{1,0x4E5B},{1,0x4DFB}},{{1,0x4E69},{1,0x4DFB}},
    {{1,0x49EE},{1,0x49E6}},{{1,0x4A23},{1,0x49E6}},{{1,0x4C0D},{1,0x4C0D}},
    {{1,0x4E0A},{1,0x4DFB}},{{1,0x4E17},{1,0x4DFB}},{{1,0x4E21},{1,0x4DFB}},
}};
}
