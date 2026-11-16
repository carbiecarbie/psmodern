// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_handoffs(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,9> interaction_dialogue_handoffs{{
    {1,0x4940},{1,0x4AD3},{1,0x4B9D},{1,0x4D7A},{1,0x4DAB},
    {1,0x4DD4},{1,0x4F15},{1,0x4F58},{1,0x50DA},
}};
struct InteractionHandoff { RoutineEntry entry,unit; };
inline constexpr std::array<InteractionHandoff,15> interaction_dialogue_followups{{
    {{1,0x4E7E},{1,0x4DFB}},
    {{1,0x4AE4},{1,0x4AD3}},{{1,0x4AF5},{1,0x4AD3}},
    {{1,0x4C76},{1,0x4C70}},{{1,0x4CAD},{1,0x4C70}},{{1,0x4CB9},{1,0x4C70}},
    {{1,0x4CCF},{1,0x4C70}},{{1,0x4CFB},{1,0x4C70}},{{1,0x4D42},{1,0x4C70}},
    {{1,0x49F4},{1,0x49E6}},{{1,0x4A1A},{1,0x49E6}},
    {{1,0x49BD},{1,0x49B5}},{{1,0x4A57},{1,0x4A50}},{{1,0x4A75},{1,0x4A50}},{{1,0x4A8C},{1,0x4A50}},
}};
inline constexpr std::array<InteractionHandoff,10> interaction_item_check_followups{{
    {{1,0x4DBC},{1,0x4DAB}},{{1,0x4DE5},{1,0x4DD4}},{{1,0x50EB},{1,0x50DA}},
    {{1,0x4E77},{1,0x4DFB}},{{1,0x4E84},{1,0x4DFB}},
    {{1,0x4C30},{1,0x4C0D}},
    {{1,0x4A29},{1,0x49E6}},{{1,0x49CB},{1,0x49B5}},{{1,0x4A68},{1,0x4A50}},{{1,0x4A92},{1,0x4A50}},
}};
inline constexpr std::array<RoutineEntry,4> interaction_item_check_entries{{
    {1,0x4966},{1,0x4973},{1,0x4AA9},{1,0x4ABE},
}};
}
