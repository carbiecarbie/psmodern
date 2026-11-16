// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_companion(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,3> interaction_companion_entries{{{1,0x49FF},{1,0x4A0F},{1,0x4A30}}};
}
