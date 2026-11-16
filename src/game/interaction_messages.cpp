// SPDX-License-Identifier: GPL-3.0-or-later
// Interactions whose entire decision is to select a dialogue from the image.
#include "game/interaction_messages.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit show_interaction_message(Z& z,std::uint16_t at) {
    z.load_hl(at);
    return k::continues(static_cast<std::uint16_t>(at+3),k::jump_cycles);
}
}
void register_interaction_messages(RoutineRegistry& registry) {
    for (auto followup:interaction_message_followups) {
        const auto jump=static_cast<std::uint16_t>(followup.entry.address+3);
        k::add_segment(registry,followup.entry,k::segment(followup.unit,{k::core_final_at(jump)}),
                       [followup](auto& z) { return show_interaction_message(z,followup.entry.address); });
    }
    for (auto entry:interaction_message_entries) {
        const auto jump=static_cast<std::uint16_t>(entry.address+3);
        k::add_segment(registry,entry,k::segment(entry,{k::core_final_at(jump)}),
                       [entry](auto& z) { return show_interaction_message(z,entry.address); });
    }
}
}
