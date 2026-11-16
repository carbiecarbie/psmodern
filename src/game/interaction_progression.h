// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_progression(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,7> interaction_progression_entries{{
    {1,0x4E02},{1,0x4E25},{1,0x4E3C},{1,0x4E4D},
    {1,0x4E5F},{1,0x4E6D},{1,0x4E91},
}};
}
