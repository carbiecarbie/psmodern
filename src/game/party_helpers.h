// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_party_helpers(RoutineRegistry &);
namespace party_helpers {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x17BA}, {0, 0x17BA}}, Segment{{0, 0x17C9}, {0, 0x17BA}},
    Segment{{0, 0x17D3}, {0, 0x17BA}}, Segment{{0, 0x17DD}, {0, 0x17BA}},
    Segment{{0, 0x17E4}, {0, 0x17BA}}, Segment{{0, 0x187A}, {0, 0x187A}},
    Segment{{0, 0x187D}, {0, 0x187A}}, Segment{{0, 0x188E}, {0, 0x188E}},
    Segment{{0, 0x1892}, {0, 0x188E}}, Segment{{0, 0x18A4}, {0, 0x188E}}};
} // namespace party_helpers
} // namespace psmodern::game
