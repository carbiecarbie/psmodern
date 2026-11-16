// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_items_story(RoutineRegistry &);
namespace items_story {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x242F}, {0, 0x242F}}, Segment{{0, 0x2435}, {0, 0x242F}},
    Segment{{0, 0x2444}, {0, 0x242F}}, Segment{{0, 0x244E}, {0, 0x242F}},
    Segment{{0, 0x245F}, {0, 0x242F}}, Segment{{0, 0x246D}, {0, 0x242F}},
    Segment{{0, 0x247C}, {0, 0x242F}}, Segment{{0, 0x2491}, {0, 0x2491}},
    Segment{{0, 0x2497}, {0, 0x2491}}, Segment{{0, 0x24A6}, {0, 0x2491}},
    Segment{{0, 0x24B0}, {0, 0x2491}}, Segment{{0, 0x24BE}, {0, 0x24BE}},
    Segment{{0, 0x24CF}, {0, 0x2491}}, Segment{{0, 0x24DA}, {0, 0x2491}},
    Segment{{0, 0x24E9}, {0, 0x24E9}}, Segment{{0, 0x24EF}, {0, 0x24E9}},
    Segment{{0, 0x24F5}, {0, 0x24E9}}, Segment{{0, 0x2504}, {0, 0x24E9}},
    Segment{{0, 0x2509}, {0, 0x24E9}}, Segment{{0, 0x251B}, {0, 0x24E9}},
    Segment{{0, 0x2527}, {0, 0x2524}}, Segment{{0, 0x252D}, {0, 0x2524}},
    Segment{{0, 0x2537}, {0, 0x2537}}, Segment{{0, 0x253D}, {0, 0x2537}},
    Segment{{0, 0x254C}, {0, 0x2537}}, Segment{{0, 0x255C}, {0, 0x2537}},
    Segment{{0, 0x2560}, {0, 0x2537}}, Segment{{0, 0x2566}, {0, 0x2537}},
    Segment{{0, 0x2574}, {0, 0x2537}}, Segment{{0, 0x257D}, {0, 0x2537}},
    Segment{{0, 0x2589}, {0, 0x2589}}, Segment{{0, 0x258F}, {0, 0x2589}},
    Segment{{0, 0x259E}, {0, 0x2589}}, Segment{{0, 0x25AE}, {0, 0x2589}},
    Segment{{0, 0x25BB}, {0, 0x2589}}, Segment{{0, 0x25C3}, {0, 0x25C3}},
    Segment{{0, 0x25D7}, {0, 0x25C3}}, Segment{{0, 0x25DD}, {0, 0x25C3}},
    Segment{{0, 0x25EF}, {0, 0x25C3}}, Segment{{0, 0x25FF}, {0, 0x25C3}},
    Segment{{0, 0x2605}, {0, 0x25C3}}, Segment{{0, 0x2613}, {0, 0x2613}},
    Segment{{0, 0x261F}, {0, 0x2613}}, Segment{{0, 0x2631}, {0, 0x2631}},
    Segment{{0, 0x263D}, {0, 0x2631}}, Segment{{0, 0x264F}, {0, 0x2631}},
    Segment{{0, 0x265B}, {0, 0x2631}}, Segment{{0, 0x267C}, {0, 0x267C}},
    Segment{{0, 0x2683}, {0, 0x267C}}, Segment{{0, 0x2689}, {0, 0x267C}},
    Segment{{0, 0x268E}, {0, 0x267C}}, Segment{{0, 0x26A7}, {0, 0x267C}},
    Segment{{0, 0x26B0}, {0, 0x26B0}}, Segment{{0, 0x26B6}, {0, 0x26B0}}};
} // namespace items_story
} // namespace psmodern::game
