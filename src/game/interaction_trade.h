// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_trade(RoutineRegistry& registry);
struct TradeSegment { RoutineEntry entry,unit; };
inline constexpr std::array<TradeSegment,6> interaction_trade_entries{{
    {{1,0x4F1E},{1,0x4F15}},
    {{1,0x4DC3},{1,0x4DAB}},{{1,0x4DC7},{1,0x4DAB}},
    {{1,0x4D86},{1,0x4D7A}},{{1,0x4D91},{1,0x4D7A}},{{1,0x4EA5},{1,0x4E9C}},
}};
}
