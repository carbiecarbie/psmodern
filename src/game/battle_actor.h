// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_actor(RoutineRegistry &);
namespace battle_actor {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x1148}, {0, 0x1148}}, Segment{{0, 0x114F}, {0, 0x1148}},
    Segment{{0, 0x1154}, {0, 0x1148}}, Segment{{0, 0x116B}, {0, 0x1148}},
    Segment{{0, 0x118E}, {0, 0x1148}}, Segment{{0, 0x1193}, {0, 0x1148}},
    Segment{{0, 0x11B8}, {0, 0x1148}}, Segment{{0, 0x115C}, {0, 0x1148}},
    Segment{{0, 0x11C2}, {0, 0x1148}}, Segment{{0, 0x11DC}, {0, 0x1148}},
    Segment{{0, 0x11E1}, {0, 0x1148}}, Segment{{0, 0x11F1}, {0, 0x1148}},
    Segment{{0, 0x11F6}, {0, 0x1148}}, Segment{{0, 0x1203}, {0, 0x1148}},
    Segment{{0, 0x1212}, {0, 0x1148}}};
} // namespace battle_actor
} // namespace psmodern::game
