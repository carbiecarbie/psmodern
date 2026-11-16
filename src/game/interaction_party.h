// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_party(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,2> interaction_party_entries{{{1,0x4B43},{1,0x4B52}}};
}
