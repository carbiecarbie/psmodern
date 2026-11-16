// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_prompts(RoutineRegistry& registry);
struct InteractionPrompt { RoutineEntry entry,answer; bool branch_when_zero; };
inline constexpr std::array<InteractionPrompt,5> interaction_prompts{{
    {{1,0x4BBD},{1,0x4BC6},true},
    {{1,0x4EC5},{1,0x4ECE},false},
    {{1,0x502C},{1,0x5035},true},
    {{1,0x50A6},{1,0x50AF},true},
    {{1,0x50C6},{1,0x50CF},true},
}};
}
