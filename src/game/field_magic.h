// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_field_magic(RoutineRegistry &);
namespace field_magic {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x201C}, {0, 0x201C}}, Segment{{0, 0x2020}, {0, 0x201C}},
    Segment{{0, 0x2039}, {0, 0x201C}}, Segment{{0, 0x205C}, {0, 0x201C}},
    Segment{{0, 0x2064}, {0, 0x2064}}, Segment{{0, 0x2068}, {0, 0x2064}},
    Segment{{0, 0x2078}, {0, 0x2078}}, Segment{{0, 0x2086}, {0, 0x2078}},
    Segment{{0, 0x2091}, {0, 0x2091}}, Segment{{0, 0x2095}, {0, 0x2091}},
    Segment{{0, 0x20AF}, {0, 0x2091}}, Segment{{0, 0x20F8}, {0, 0x20F8}},
    Segment{{0, 0x20FC}, {0, 0x20F8}}, Segment{{0, 0x2102}, {0, 0x2102}},
    Segment{{0, 0x2115}, {0, 0x2102}}, Segment{{0, 0x2120}, {0, 0x2102}},
    Segment{{0, 0x2138}, {0, 0x2102}}, Segment{{0, 0x2140}, {0, 0x2140}},
    Segment{{0, 0x2144}, {0, 0x2140}}, Segment{{0, 0x2159}, {0, 0x2159}},
    Segment{{0, 0x2162}, {0, 0x2159}},
    Segment{{0, 0x2156}, {0, 0x2140}}, Segment{{0, 0x215F}, {0, 0x2159}}};
} // namespace field_magic
} // namespace psmodern::game
