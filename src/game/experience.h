// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/options.h"
#include "machine/routines.h"
#include <array>

namespace psmodern::game {
// Segment entries of the experience award, as planned in milestone 4 (award
// unit 00:170D, per-character helper unit 00:1754). Registered by
// register_experience.
inline constexpr RoutineEntry experience_award_unit{0,0x170D};
inline constexpr RoutineEntry experience_helper_unit{0,0x1754};
inline constexpr RoutineEntry experience_add_entry{0,0x1773};
inline constexpr std::array<RoutineEntry,10> experience_entries{
    experience_award_unit, RoutineEntry{0,0x1710}, RoutineEntry{0,0x171C}, RoutineEntry{0,0x172A},
    RoutineEntry{0,0x1739}, RoutineEntry{0,0x1748},
    experience_helper_unit, experience_add_entry, RoutineEntry{0,0x1774}, RoutineEntry{0,0x179D}};

void register_experience(RoutineRegistry& registry, const Options& options);
}
