// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_setup(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,6> interaction_dispatch_entries{{{1,0x474B},{1,0x4752},{1,0x4758},{1,0x4765},{1,0x475F},{1,0x4762}}};
struct InteractionSetup { RoutineEntry entry; bool call; };
inline constexpr std::array<InteractionSetup,3> interaction_dialogue_setup{{
    {{1,0x4F9B},true},{{1,0x504C},false},{{1,0x5070},true},
}};
inline constexpr std::array<RoutineEntry,1> interaction_enemy_setup{{{1,0x49E6}}};
}
