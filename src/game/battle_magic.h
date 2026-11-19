// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_magic(RoutineRegistry &);
namespace battle_magic {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x1EA7}, {0, 0x1EA7}}, Segment{{0, 0x1EAB}, {0, 0x1EA7}},
    Segment{{0, 0x1EB2}, {0, 0x1EB2}}, Segment{{0, 0x1EBC}, {0, 0x1EB2}},
    Segment{{0, 0x1EC9}, {0, 0x1EB2}}, Segment{{0, 0x1ED0}, {0, 0x1EB2}},
    Segment{{0, 0x1EDB}, {0, 0x1EB2}}, Segment{{0, 0x1EE4}, {0, 0x1EB2}},
    Segment{{0, 0x1EE6}, {0, 0x1EE6}}, Segment{{0, 0x1EEA}, {0, 0x1EE6}},
    Segment{{0, 0x1EEE}, {0, 0x1EEE}}, Segment{{0, 0x1EF0}, {0, 0x1EEE}},
    Segment{{0, 0x1EF9}, {0, 0x1EEE}}, Segment{{0, 0x1F04}, {0, 0x1EEE}},
    Segment{{0, 0x1F0E}, {0, 0x1EEE}}, Segment{{0, 0x1F14}, {0, 0x1EEE}},
    Segment{{0, 0x1F17}, {0, 0x1EEE}},
    Segment{{0, 0x1F1F}, {0, 0x1EEE}}, Segment{{0, 0x1F23}, {0, 0x1EEE}},
    Segment{{0, 0x1F25}, {0, 0x1F25}}, Segment{{0, 0x1F29}, {0, 0x1F25}}};
} // namespace battle_magic
} // namespace psmodern::game
