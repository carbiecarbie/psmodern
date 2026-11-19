// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include "game/options.h"
#include <array>
namespace psmodern::game {
inline constexpr RoutineEntry money_add_unit{0,0x297A};
inline constexpr RoutineEntry battle_setup_unit{1,0x5FFE};
inline constexpr RoutineEntry battle_money_store{1,0x60BC};
inline constexpr std::array<RoutineEntry,17> battle_setup_entries{
    battle_setup_unit,{1,0x6045},{1,0x6052},{1,0x605E},{1,0x6078},
    {1,0x608D},battle_money_store,{1,0x60BF},{1,0x60D7},{1,0x60ED},
    {1,0x6044},{1,0x6051},{1,0x6059},{1,0x605D},{1,0x608C},{1,0x60D3},{1,0x60EA}};
void register_money_add(RoutineRegistry& registry);
void register_battle_money_store(RoutineRegistry& registry,const Options& options);
void register_battle_setup(RoutineRegistry& registry,const Options& options);
}
