// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_letter(RoutineRegistry& registry);
inline constexpr RoutineEntry interaction_letter_copy{1,0x4CF6};
inline constexpr std::array<RoutineEntry,18> interaction_letter_entries{{
    {1,0x4C7F},{1,0x4C92},{1,0x4C97},{1,0x4C9F},{1,0x4CA6},
    {1,0x4CBF},{1,0x4CC8},{1,0x4CDB},{1,0x4CE3},{1,0x4CED},
    {1,0x4D04},{1,0x4D0C},{1,0x4D14},{1,0x4D23},
    {1,0x4D35},{1,0x4D4B},{1,0x4D56},{1,0x4D5E},
}};
}
