// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_state(RoutineRegistry& registry);
struct InteractionState { RoutineEntry entry; std::uint16_t state; bool indirect,absolute,branch_when_set; };
inline constexpr std::array<InteractionState,4> interaction_state_zero{{
    {{1,0x48DF},0xC501,true,false,false},{{1,0x48FC},0xC502,false,false,false},{{1,0x4C70},0xC516,false,false,true},
    {{1,0x4DFB},0xC504,false,true,true},
}};
inline constexpr std::array<InteractionState,3> interaction_state_compare{{
    {{1,0x49B5},0xC504,false,true,false},{{1,0x4A50},0xC504,false,false,false},{{1,0x4E9C},0xC504,true,true,true},
}};
}
