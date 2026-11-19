// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_items_field(RoutineRegistry &);
namespace items_field {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x228A}, {0, 0x228A}}, Segment{{0, 0x2290}, {0, 0x228A}},
    Segment{{0, 0x2299}, {0, 0x2299}}, Segment{{0, 0x229F}, {0, 0x2299}},
    Segment{{0, 0x22A6}, {0, 0x2299}}, Segment{{0, 0x22AF}, {0, 0x22AF}},
    Segment{{0, 0x22B5}, {0, 0x22AF}}, Segment{{0, 0x22B7}, {0, 0x22AF}},
    Segment{{0, 0x22CC}, {0, 0x22AF}}, Segment{{0, 0x22E5}, {0, 0x22E5}},
    Segment{{0, 0x22EB}, {0, 0x22E5}}, Segment{{0, 0x2300}, {0, 0x22E5}},
    Segment{{0, 0x231A}, {0, 0x231A}}, Segment{{0, 0x2320}, {0, 0x231A}},
    Segment{{0, 0x232A}, {0, 0x231A}}, Segment{{0, 0x2369}, {0, 0x2369}},
    Segment{{0, 0x236F}, {0, 0x2369}}, Segment{{0, 0x2380}, {0, 0x2369}},
    Segment{{0, 0x238F}, {0, 0x2369}}, Segment{{0, 0x2395}, {0, 0x2369}},
    Segment{{0, 0x239D}, {0, 0x239D}}, Segment{{0, 0x23A9}, {0, 0x239D}},
    Segment{{0, 0x23B2}, {0, 0x239D}}, Segment{{0, 0x23B8}, {0, 0x239D}},
    Segment{{0, 0x23BE}, {0, 0x239D}}, Segment{{0, 0x23C7}, {0, 0x239D}},
    Segment{{0, 0x23D9}, {0, 0x239D}}, Segment{{0, 0x23E2}, {0, 0x23E2}},
    Segment{{0, 0x23EC}, {0, 0x23EC}}, Segment{{0, 0x23F2}, {0, 0x23EC}},
    Segment{{0, 0x2401}, {0, 0x23EC}}, Segment{{0, 0x2409}, {0, 0x23EC}},
    Segment{{0, 0x240D}, {0, 0x23EC}}, Segment{{0, 0x2419}, {0, 0x2416}},
    Segment{{0, 0x241F}, {0, 0x2416}}, Segment{{0, 0x2426}, {0, 0x2416}},
    Segment{{0, 0x22E2}, {0, 0x22AF}}, Segment{{0, 0x2317}, {0, 0x22E5}},
    Segment{{0, 0x23D3}, {0, 0x239D}}, Segment{{0, 0x23D6}, {0, 0x239D}}};
} // namespace items_field
} // namespace psmodern::game
