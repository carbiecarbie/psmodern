// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"

namespace psmodern::game {
// Boundaries used by Options, field waits, tempo audio and automap tracking.
inline constexpr std::array<RoutineEntry,13> host_observed_instructions{{
    {0,0x00AF},{0,0x0055},{0,0x0085},{0,0x0B14},{0,0x0F48},{0,0x2D93},{0,0x02ED},{0,0x02F5},
    {12,0x8043},{12,0x82EF},{0,0x1D2D},{0,0x06B4},{0,0x05F4}
}};
}
