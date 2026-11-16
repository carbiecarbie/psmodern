// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_enemy(RoutineRegistry &);
namespace battle_enemy {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x1290}, {0, 0x128C}}, Segment{{0, 0x129C}, {0, 0x128C}},
    Segment{{0, 0x12B9}, {0, 0x128C}}, Segment{{0, 0x12D4}, {0, 0x128C}},
    Segment{{0, 0x12DE}, {0, 0x128C}}, Segment{{0, 0x12F8}, {0, 0x128C}},
    Segment{{0, 0x1305}, {0, 0x128C}}, Segment{{0, 0x130F}, {0, 0x128C}},
    Segment{{0, 0x1317}, {0, 0x128C}}, Segment{{0, 0x1327}, {0, 0x128C}},
    Segment{{0, 0x132F}, {0, 0x128C}}, Segment{{0, 0x1344}, {0, 0x128C}},
    Segment{{0, 0x1348}, {0, 0x128C}}, Segment{{0, 0x1350}, {0, 0x128C}},
    Segment{{0, 0x1379}, {0, 0x128C}}, Segment{{0, 0x1380}, {0, 0x128C}}};
} // namespace battle_enemy
} // namespace psmodern::game
