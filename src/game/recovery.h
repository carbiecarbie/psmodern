// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_recovery(RoutineRegistry&);
namespace recovery {
struct Segment { RoutineEntry entry, unit; };
inline constexpr std::array segments{
 Segment{{0,0x1B87},{0,0x1B87}},
 Segment{{0,0x1E27},{0,0x1E27}}, Segment{{0,0x1E2B},{0,0x1E2B}}, Segment{{0,0x1E32},{0,0x1E27}},
 Segment{{0,0x1E3E},{0,0x1E27}}, Segment{{0,0x1E46},{0,0x1E46}}, Segment{{0,0x1E4A},{0,0x1E4A}},
 Segment{{0,0x1E52},{0,0x1E46}}, Segment{{0,0x1E53},{0,0x1E53}}, Segment{{0,0x1E58},{0,0x1E53}},
 Segment{{0,0x1E5F},{0,0x1E5F}}, Segment{{0,0x1E66},{0,0x1E5F}}, Segment{{0,0x1E72},{0,0x1E5F}},
 Segment{{0,0x2333},{0,0x2333}}, Segment{{0,0x2337},{0,0x2337}}, Segment{{0,0x2346},{0,0x2333}},
 Segment{{0,0x2351},{0,0x2333}}, Segment{{0,0x235A},{0,0x2333}}, Segment{{0,0x2361},{0,0x2333}},
 Segment{{0,0x1B9E},{0,0x1B87}}, Segment{{0,0x1BA1},{0,0x1B87}}, Segment{{0,0x1E43},{0,0x1E27}},
 Segment{{0,0x235E},{0,0x2333}}
};
}
}
