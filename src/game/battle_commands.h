// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_commands(RoutineRegistry &);
namespace battle_commands {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x191C}, {0, 0x191C}}, Segment{{0, 0x1923}, {0, 0x191C}},
    Segment{{0, 0x1934}, {0, 0x192B}}, Segment{{0, 0x1940}, {0, 0x192B}},
    Segment{{0, 0x1951}, {0, 0x192B}}, Segment{{0, 0x1964}, {0, 0x192B}},
    Segment{{0, 0x196D}, {0, 0x192B}}, Segment{{0, 0x1982}, {0, 0x192B}},
    Segment{{0, 0x19A5}, {0, 0x199C}}, Segment{{0, 0x19AC}, {0, 0x199C}},
    Segment{{0, 0x19B8}, {0, 0x199C}}, Segment{{0, 0x19C5}, {0, 0x199C}},
    Segment{{0, 0x19D1}, {0, 0x199C}}, Segment{{0, 0x19DE}, {0, 0x19DE}},
    Segment{{0, 0x1A0D}, {0, 0x19DE}}, Segment{{0, 0x1A1C}, {0, 0x19DE}},
    Segment{{0, 0x1A36}, {0, 0x19DE}}, Segment{{0, 0x1A42}, {0, 0x19DE}},
    Segment{{0, 0x1A4B}, {0, 0x19DE}}, Segment{{0, 0x1AB1}, {0, 0x1AB1}},
    Segment{{0, 0x1AB5}, {0, 0x1AB1}}, Segment{{0, 0x1ABD}, {0, 0x1AB1}},
    Segment{{0, 0x1AC5}, {0, 0x1AB1}}, Segment{{0, 0x1AD0}, {0, 0x1AD0}},
    Segment{{0, 0x1AD6}, {0, 0x1AD0}}, Segment{{0, 0x1ADE}, {0, 0x1ADE}},
    Segment{{0, 0x1AE2}, {0, 0x1ADE}}, Segment{{0, 0x1AEA}, {0, 0x1ADE}},
    Segment{{0, 0x1AF2}, {0, 0x1ADE}}, Segment{{0, 0x1AFD}, {0, 0x1AFD}},
    Segment{{0, 0x1B05}, {0, 0x1AFD}}, Segment{{0, 0x1B0D}, {0, 0x1B0D}},
    Segment{{0, 0x1B11}, {0, 0x1B0D}}, Segment{{0, 0x1B17}, {0, 0x1B0D}},
    Segment{{0, 0x1B31}, {0, 0x1B31}}, Segment{{0, 0x1B35}, {0, 0x1B31}},
    Segment{{0, 0x1B36}, {0, 0x1B31}}, Segment{{0, 0x1B43}, {0, 0x1B31}},
    Segment{{0, 0x1B51}, {0, 0x1B31}}, Segment{{0, 0x1B66}, {0, 0x1B31}},
    Segment{{0, 0x1BA4}, {0, 0x1B9E}}, Segment{{0, 0x1BB1}, {0, 0x1B9E}},
    Segment{{0, 0x1BB9}, {0, 0x1BB9}}, Segment{{0, 0x1BCE}, {0, 0x1BCE}},
    Segment{{0, 0x1A48}, {0, 0x19DE}}};
} // namespace battle_commands
} // namespace psmodern::game
