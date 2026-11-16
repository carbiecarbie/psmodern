// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_damage(RoutineRegistry &);
namespace battle_damage {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x1573}, {0, 0x1573}}, Segment{{0, 0x1579}, {0, 0x1573}},
    Segment{{0, 0x159C}, {0, 0x1573}}, Segment{{0, 0x15A3}, {0, 0x1573}},
    Segment{{0, 0x15B5}, {0, 0x1573}}, Segment{{0, 0x15C2}, {0, 0x1573}},
    Segment{{0, 0x121D}, {0, 0x121D}}, Segment{{0, 0x1232}, {0, 0x121D}},
    Segment{{0, 0x1239}, {0, 0x121D}}, Segment{{0, 0x1254}, {0, 0x121D}},
    Segment{{0, 0x125E}, {0, 0x125E}}, Segment{{0, 0x1267}, {0, 0x125E}},
    Segment{{0, 0x1279}, {0, 0x1279}}, Segment{{0, 0x1284}, {0, 0x1279}},
    Segment{{0, 0x1288}, {0, 0x1279}}, Segment{{0, 0x18A9}, {0, 0x18A9}},
    Segment{{0, 0x18B6}, {0, 0x18A9}}, Segment{{0, 0x18B9}, {0, 0x18B9}},
    Segment{{0, 0x18C1}, {0, 0x18B9}}, Segment{{0, 0x18CE}, {0, 0x18CE}},
    Segment{{0, 0x18DB}, {0, 0x18CE}}, Segment{{0, 0x18E3}, {0, 0x18CE}},
    Segment{{0, 0x18EF}, {0, 0x18CE}}};
} // namespace battle_damage
} // namespace psmodern::game
