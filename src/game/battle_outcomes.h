// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_outcomes(RoutineRegistry &);
namespace battle_outcomes {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x15DC}, {0, 0x15D9}}, Segment{{0, 0x15EC}, {0, 0x15D9}},
    Segment{{0, 0x15FC}, {0, 0x15D9}}, Segment{{0, 0x1602}, {0, 0x1602}},
    Segment{{0, 0x1613}, {0, 0x1602}}, Segment{{0, 0x1630}, {0, 0x1602}},
    Segment{{0, 0x1636}, {0, 0x1602}}, Segment{{0, 0x163E}, {0, 0x163E}},
    Segment{{0, 0x1645}, {0, 0x163E}}, Segment{{0, 0x1656}, {0, 0x1656}},
    Segment{{0, 0x1663}, {0, 0x1656}}, Segment{{0, 0x1679}, {0, 0x1656}},
    Segment{{0, 0x1689}, {0, 0x1656}}, Segment{{0, 0x169A}, {0, 0x1656}},
    Segment{{0, 0x16B2}, {0, 0x16B2}}, Segment{{0, 0x16C2}, {0, 0x16B2}},
    Segment{{0, 0x16CD}, {0, 0x16B2}}, Segment{{0, 0x16D6}, {0, 0x16B2}},
    Segment{{0, 0x16E3}, {0, 0x16B2}}, Segment{{0, 0x16EB}, {0, 0x16B2}},
    Segment{{0, 0x16F1}, {0, 0x16F1}}, Segment{{0, 0x16FE}, {0, 0x16F1}},
    Segment{{0, 0x15E7}, {0, 0x15E7}}, Segment{{0, 0x16C0}, {0, 0x16C0}},
    Segment{{0, 0x16CB}, {0, 0x16CB}}, Segment{{0, 0x16E1}, {0, 0x16E1}}};
inline constexpr std::array copy_entries{RoutineEntry{0, 0x15E7}, RoutineEntry{0, 0x16C0},
                                         RoutineEntry{0, 0x16CB}, RoutineEntry{0, 0x16E1}};
} // namespace battle_outcomes
} // namespace psmodern::game
