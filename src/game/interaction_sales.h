// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_sales(RoutineRegistry& registry);
struct SaleSegment { RoutineEntry entry,unit; };
inline constexpr std::array<SaleSegment,5> interaction_sale_entries{{
    {{1,0x4FB2},{1,0x4F9B}},{{1,0x4FC3},{1,0x4F9B}},{{1,0x4FCC},{1,0x4F9B}},
    {{1,0x5087},{1,0x5070}},{{1,0x5098},{1,0x5070}},
}};
}
