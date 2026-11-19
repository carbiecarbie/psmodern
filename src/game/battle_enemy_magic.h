// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_enemy_magic(RoutineRegistry &);
namespace battle_enemy_magic {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x1389}, {0, 0x1386}}, Segment{{0, 0x138E}, {0, 0x1386}},
    Segment{{0, 0x1396}, {0, 0x1386}}, Segment{{0, 0x13A9}, {0, 0x1386}},
    Segment{{0, 0x13B0}, {0, 0x1386}}, Segment{{0, 0x13BC}, {0, 0x1386}},
    Segment{{0, 0x13CC}, {0, 0x1386}}, Segment{{0, 0x13D6}, {0, 0x1386}},
    Segment{{0, 0x13DB}, {0, 0x1386}}, Segment{{0, 0x13F6}, {0, 0x1386}},
    Segment{{0, 0x1402}, {0, 0x1386}}, Segment{{0, 0x1407}, {0, 0x1386}},
    Segment{{0, 0x140F}, {0, 0x1386}}, Segment{{0, 0x1424}, {0, 0x1386}},
    Segment{{0, 0x142C}, {0, 0x1386}}, Segment{{0, 0x1434}, {0, 0x1386}},
    Segment{{0, 0x1440}, {0, 0x1386}}, Segment{{0, 0x1446}, {0, 0x1386}},
    Segment{{0, 0x144E}, {0, 0x1386}}, Segment{{0, 0x145A}, {0, 0x1386}},
    Segment{{0, 0x1468}, {0, 0x1386}}, Segment{{0, 0x1470}, {0, 0x1386}},
    Segment{{0, 0x1480}, {0, 0x1386}}, Segment{{0, 0x1486}, {0, 0x1386}},
    Segment{{0, 0x1491}, {0, 0x1386}}, Segment{{0, 0x1498}, {0, 0x1386}},
    Segment{{0, 0x14A0}, {0, 0x1386}}, Segment{{0, 0x14A5}, {0, 0x1386}},
    Segment{{0, 0x14A7}, {0, 0x1386}}, Segment{{0, 0x14A9}, {0, 0x1386}},
    Segment{{0, 0x14B2}, {0, 0x1386}}, Segment{{0, 0x14BE}, {0, 0x1386}},
    Segment{{0, 0x14C8}, {0, 0x1386}}, Segment{{0, 0x14CC}, {0, 0x1386}},
    Segment{{0, 0x14D4}, {0, 0x1386}}, Segment{{0, 0x14E4}, {0, 0x1386}},
    Segment{{0, 0x14EA}, {0, 0x1386}}, Segment{{0, 0x14F5}, {0, 0x1386}},
    Segment{{0, 0x14FC}, {0, 0x1386}}, Segment{{0, 0x1501}, {0, 0x1386}},
    Segment{{0, 0x1505}, {0, 0x1386}}, Segment{{0, 0x150B}, {0, 0x1386}},
    Segment{{0, 0x1514}, {0, 0x1386}}, Segment{{0, 0x151A}, {0, 0x1386}},
    Segment{{0, 0x1528}, {0, 0x1386}}, Segment{{0, 0x1532}, {0, 0x1386}},
    Segment{{0, 0x153A}, {0, 0x1386}}, Segment{{0, 0x1544}, {0, 0x1386}},
    Segment{{0, 0x154C}, {0, 0x1386}}, Segment{{0, 0x1555}, {0, 0x1386}},
    Segment{{0, 0x155C}, {0, 0x1386}}, Segment{{0, 0x1561}, {0, 0x1386}},
    Segment{{0, 0x1569}, {0, 0x1386}}, Segment{{0, 0x156E}, {0, 0x1386}},
    Segment{{0, 0x13A6}, {0, 0x1386}}, Segment{{0, 0x13AE}, {0, 0x1386}},
    Segment{{0, 0x13C9}, {0, 0x1386}}};
} // namespace battle_enemy_magic
} // namespace psmodern::game
