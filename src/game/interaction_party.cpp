// SPDX-License-Identifier: GPL-3.0-or-later
// Select dialogue according to the current party membership state.
#include "game/interaction_party.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit select_party_dialogue(Z& z,std::uint16_t at) {
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    z.load_hl(static_cast<std::uint16_t>(at+4));
    if (!z.relative_if(static_cast<std::uint16_t>(at+7),z.zero()))
        z.load_hl(static_cast<std::uint16_t>(at+9));
    return k::continues(static_cast<std::uint16_t>(at+12),k::jump_cycles);
}
}
void register_interaction_party(RoutineRegistry& registry) {
    for (auto entry:interaction_party_entries) {
        const auto jump=static_cast<std::uint16_t>(entry.address+12);
        k::add_segment(registry,entry,k::segment(entry,{k::core_final_at(jump)}),
                       [entry](auto& z) { return select_party_dialogue(z,entry.address); });
    }
}
}
