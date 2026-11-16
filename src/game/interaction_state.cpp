// SPDX-License-Identifier: GPL-3.0-or-later
// Check story flags and counters before interaction-specific branches.
#include "game/interaction_state.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit check_story_flag(Z& z,InteractionState item) {
    const auto at=item.entry.address;
    if (item.indirect) {
        z.load_hl(at);
        z.load_from_hl(static_cast<std::uint16_t>(at+3),z.r.a);
        z.or_a(static_cast<std::uint16_t>(at+4));
        return k::relative_if_exit(static_cast<std::uint16_t>(at+5),!z.zero());
    }
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    return k::continues(static_cast<std::uint16_t>(at+4),item.absolute ? k::jump_cycles : ((z.zero()==item.branch_when_set) ? 12u : 7u));
}
template<class Z> k::Exit check_story_counter(Z& z,InteractionState item) {
    const auto at=item.entry.address;
    if (item.indirect) {
        z.load_hl(at);
        z.load_from_hl(static_cast<std::uint16_t>(at+3),z.r.a);
        z.compare_immediate(static_cast<std::uint16_t>(at+4));
        return k::jump_if_exit(static_cast<std::uint16_t>(at+6));
    }
    z.load_a_direct(at);
    z.compare_immediate(static_cast<std::uint16_t>(at+3));
    return k::continues(static_cast<std::uint16_t>(at+5),item.absolute ? k::jump_cycles : (((z.r.f&k::carry_flag)!=0)==item.branch_when_set ? 12u : 7u));
}
}
void register_interaction_state(RoutineRegistry& registry) {
    for (auto item:interaction_state_zero) {
        const auto at=item.entry.address;
        const bool indirect=item.indirect;
        const auto exit=static_cast<std::uint16_t>(at+(indirect ? 5 : 4));
        k::add_segment(registry,item.entry,k::segment(item.entry,{k::conditional_at(exit)}),
                       [item](auto& z) { return check_story_flag(z,item); });
    }
    for (auto item:interaction_state_compare) {
        const auto at=item.entry.address;
        const bool indirect=item.indirect;
        const auto exit=static_cast<std::uint16_t>(at+(indirect ? 6 : 5));
        k::add_segment(registry,item.entry,k::segment(item.entry,{k::conditional_at(exit)}),
                       [item](auto& z) { return check_story_counter(z,item); });
    }
}
}
