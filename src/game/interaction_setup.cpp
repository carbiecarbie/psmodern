// SPDX-License-Identifier: GPL-3.0-or-later
// Prepare shared dialogue and enemy-group state before native calls.
#include "game/interaction_setup.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit prepare_dialogue_context(Z& z,InteractionSetup item) {
    const auto at=item.entry.address;
    z.load_hl(at);
    z.store_hl_direct(static_cast<std::uint16_t>(at+3));
    z.load_hl(static_cast<std::uint16_t>(at+6));
    return k::continues(static_cast<std::uint16_t>(at+9),item.call ? k::call_cycles : k::jump_cycles);
}
template<class Z> k::Exit prepare_enemy_group(Z& z) {
    z.load_immediate(0x49E6,z.r.a);
    z.store_a_direct(0x49E8);
    return k::continues(0x49EB,k::call_cycles);
}
}
void register_interaction_setup(RoutineRegistry& registry) {
    // A zero event selects the field menu. A nonzero event resumes at
    // 4752, where DE is based at the interaction table minus one word.
    k::add_segment(registry,interaction_dispatch_entries[0],
        k::segment(interaction_dispatch_entries[0],{k::conditional_at(0x474F)}),
        [](auto& z) -> k::Exit {
            z.load_a_direct(0x474B);
            z.or_a(0x474E);
            return k::jump_if_exit(0x474F);
        });
    k::add_segment(registry,interaction_dispatch_entries[1],
        k::segment(interaction_dispatch_entries[0],{k::core_final_at(0x4755)},{.scratch_stack=2}),
        [](auto& z) -> k::Exit {
            z.load_de(0x4752);
            return k::continues(0x4755,k::call_cycles);
        });
    // After the handler, a remaining interaction closes the window directly;
    // otherwise the next original CALL closes it and resumes field flow.
    k::add_segment(registry,interaction_dispatch_entries[2],
        k::segment(interaction_dispatch_entries[0],{k::conditional_at(0x475C)}),
        [](auto& z) -> k::Exit {
            z.load_a_direct(0x4758);
            z.or_a(0x475B);
            return k::jump_if_exit(0x475C);
        });
    // Some interaction handlers discard their own return and wait for a button.
    k::add_segment(registry,interaction_dispatch_entries[3],
        k::segment(interaction_dispatch_entries[0],{k::core_final_at(0x4766,2)},{.inherited_stack=2}),
        [](auto& z) -> k::Exit {
            z.pop(0x4765,z.r.h,z.r.l);
            return k::continues(0x4766,k::jump_cycles);
        });
    for (auto item:interaction_dialogue_setup) {
        const auto exit=static_cast<std::uint16_t>(item.entry.address+9);
        k::Permissions permissions{};
        permissions.scratch_stack=item.call ? 2 : 0;
        k::add_segment(registry,item.entry,k::segment(item.entry,{k::core_final_at(exit)},permissions),
                       [item](auto& z) { return prepare_dialogue_context(z,item); });
    }
    const RoutineEntry enemy{1,0x49E6};
    k::add_segment(registry,enemy,k::segment(enemy,{k::core_final_at(0x49EB)},{.scratch_stack=2}),
                   [](auto& z) { return prepare_enemy_group(z); });
}
}
