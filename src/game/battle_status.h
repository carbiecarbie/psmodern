// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_battle_status(RoutineRegistry &);
namespace battle_status {
struct Segment {
    RoutineEntry entry, unit;
};
inline constexpr std::array segments{
    Segment{{0, 0x1E8A}, {0, 0x1E8A}}, Segment{{0, 0x1E8E}, {0, 0x1E8E}},
    Segment{{0, 0x1E94}, {0, 0x1E8A}}, Segment{{0, 0x1F36}, {0, 0x1F36}},
    Segment{{0, 0x1F3A}, {0, 0x1F36}}, Segment{{0, 0x1F56}, {0, 0x1F36}},
    Segment{{0, 0x1F68}, {0, 0x1F36}}, Segment{{0, 0x1F80}, {0, 0x1F80}},
    Segment{{0, 0x1F84}, {0, 0x1F80}}, Segment{{0, 0x1F89}, {0, 0x1F89}},
    Segment{{0, 0x1F98}, {0, 0x1F89}}, Segment{{0, 0x1FAC}, {0, 0x1F89}},
    Segment{{0, 0x1FBA}, {0, 0x1F89}}, Segment{{0, 0x1FC0}, {0, 0x1FC0}},
    Segment{{0, 0x1FC4}, {0, 0x1FC0}}, Segment{{0, 0x1FD0}, {0, 0x1FC0}},
    Segment{{0, 0x1FD4}, {0, 0x1FC0}}, Segment{{0, 0x1FDF}, {0, 0x1FDF}},
    Segment{{0, 0x1FE3}, {0, 0x1FDF}}, Segment{{0, 0x1FF6}, {0, 0x1FDF}},
    Segment{{0, 0x2004}, {0, 0x1FDF}}, Segment{{0, 0x20BF}, {0, 0x20BF}},
    Segment{{0, 0x20C3}, {0, 0x20BF}}, Segment{{0, 0x20C7}, {0, 0x20BF}},
    Segment{{0, 0x20D8}, {0, 0x20BF}}};
} // namespace battle_status
} // namespace psmodern::game
