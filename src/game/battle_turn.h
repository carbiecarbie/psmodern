// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_turn(RoutineRegistry &);
namespace battle_turn {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x100F}, {0, 0x100F}}, Segment{{0, 0x1034}, {0, 0x100F}},
    Segment{{0, 0x103B}, {0, 0x100F}}, Segment{{0, 0x104A}, {0, 0x100F}},
    Segment{{0, 0x1055}, {0, 0x1055}}, Segment{{0, 0x1077}, {0, 0x1055}},
    Segment{{0, 0x1080}, {0, 0x1055}}, Segment{{0, 0x108A}, {0, 0x1055}},
    Segment{{0, 0x109B}, {0, 0x1055}}, Segment{{0, 0x10A9}, {0, 0x1055}},
    Segment{{0, 0x10B7}, {0, 0x1055}}, Segment{{0, 0x10C4}, {0, 0x1055}},
    Segment{{0, 0x111C}, {0, 0x10D1}}, Segment{{0, 0x10D1}, {0, 0x10D1}},
    Segment{{0, 0x10D6}, {0, 0x10D1}}, Segment{{0, 0x10E7}, {0, 0x10D1}},
    Segment{{0, 0x10FC}, {0, 0x10D1}}, Segment{{0, 0x1111}, {0, 0x10D1}},
    Segment{{0, 0x1124}, {0, 0x1124}}, Segment{{0, 0x1134}, {0, 0x1124}},
    Segment{{0, 0x18F2}, {0, 0x18F2}}, Segment{{0, 0x18FA}, {0, 0x18F2}},
    Segment{{0, 0x1043}, {0, 0x100F}}, Segment{{0, 0x1071}, {0, 0x1055}},
    Segment{{0, 0x1074}, {0, 0x1055}}, Segment{{0, 0x1095}, {0, 0x1055}},
    Segment{{0, 0x1098}, {0, 0x1055}}, Segment{{0, 0x10B4}, {0, 0x1055}},
    Segment{{0, 0x10CB}, {0, 0x1055}}, Segment{{0, 0x10CE}, {0, 0x1055}},
    Segment{{0, 0x10E1}, {0, 0x10D1}}, Segment{{0, 0x128C}, {0, 0x128C}}};
} // namespace battle_turn
} // namespace psmodern::game
