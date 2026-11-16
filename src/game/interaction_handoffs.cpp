// SPDX-License-Identifier: GPL-3.0-or-later
// Handoffs from story interactions to dialogue and inventory helpers.
#include "game/interaction_handoffs.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit show_story_intro(Z& z,std::uint16_t at) {
    z.load_hl(at);
    return k::continues(static_cast<std::uint16_t>(at+3),k::call_cycles);
}
template<class Z> k::Exit find_story_item(Z& z,std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    return k::continues(static_cast<std::uint16_t>(at+2),k::call_cycles);
}
}
void register_interaction_handoffs(RoutineRegistry& registry) {
    for (auto followup:interaction_dialogue_followups) {
        const auto call=static_cast<std::uint16_t>(followup.entry.address+3);
        k::add_segment(registry,followup.entry,k::segment(followup.unit,{k::core_final_at(call)},{.scratch_stack=2}),
                       [followup](auto& z) { return show_story_intro(z,followup.entry.address); });
    }
    for (auto followup:interaction_item_check_followups) {
        const auto call=static_cast<std::uint16_t>(followup.entry.address+2);
        k::add_segment(registry,followup.entry,k::segment(followup.unit,{k::core_final_at(call)},{.scratch_stack=2}),
                       [followup](auto& z) { return find_story_item(z,followup.entry.address); });
    }
    for (auto entry:interaction_dialogue_handoffs) {
        const auto call=static_cast<std::uint16_t>(entry.address+3);
        k::add_segment(registry,entry,k::segment(entry,{k::core_final_at(call)},{.scratch_stack=2}),
                       [entry](auto& z) { return show_story_intro(z,entry.address); });
    }
    for (auto entry:interaction_item_check_entries) {
        const auto call=static_cast<std::uint16_t>(entry.address+2);
        k::add_segment(registry,entry,k::segment(entry,{k::core_final_at(call)},{.scratch_stack=2}),
                       [entry](auto& z) { return find_story_item(z,entry.address); });
    }
}
}
