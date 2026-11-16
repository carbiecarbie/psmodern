// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_rescue(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,7> interaction_rescue_entries{{
    {1,0x4C19},{1,0x4C37},{1,0x4C41},{1,0x4C44},
    {1,0x4C48},{1,0x4C52},{1,0x4C5A},
}};
}
