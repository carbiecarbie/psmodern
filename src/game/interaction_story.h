// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_story(RoutineRegistry& registry);
struct StorySegment { RoutineEntry entry,unit; };
inline constexpr std::array<StorySegment,12> interaction_story_segments{{
    {{1,0x48E6},{1,0x48DF}},{{1,0x48EE},{1,0x48DF}},{{1,0x48F4},{1,0x48DF}},
    {{1,0x4A98},{1,0x4A50}},{{1,0x4A9C},{1,0x4A50}},
    {{1,0x4980},{1,0x4973}},{{1,0x4986},{1,0x4973}},
    {{1,0x4902},{1,0x48FC}},{{1,0x4908},{1,0x48FC}},{{1,0x4910},{1,0x48FC}},{{1,0x4917},{1,0x48FC}},{{1,0x491C},{1,0x48FC}},
}};
}
