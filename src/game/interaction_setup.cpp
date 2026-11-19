// SPDX-License-Identifier: GPL-3.0-or-later
// Prepare shared dialogue and enemy-group state before the calls that use it.
// Milestone 14 worked example (menu kit): every final instruction is rendered
// and the two instructions that sat between these segments
// (01:475F, 01:4762) are segments of their own.
#include "game/joins.h"
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
    const auto handoff=static_cast<std::uint16_t>(at+9);
    return item.call ? z.final_call(handoff) : z.final_jump(handoff);
}
template<class Z> k::Exit prepare_enemy_group(Z& z) {
    z.load_immediate(0x49E6,z.r.a);
    z.store_a_direct(0x49E8);
    return z.final_call(0x49EB);
}
}
void register_interaction_setup(RoutineRegistry& registry) {
    const auto dispatch=interaction_dispatch_entries[0];
    // A zero event selects the field menu. A nonzero event resumes at
    // 4752, where DE is based at the interaction table minus one word.
    k::add_segment(registry,interaction_dispatch_entries[0],
        k::segment(dispatch,{k::conditional_at(0x474F)}),
        [](auto& z) -> k::Exit {
            z.load_a_direct(0x474B);
            z.or_a(0x474E);
            return z.final_jump_if(0x474F,k::Condition::z);
        });
    k::add_segment(registry,interaction_dispatch_entries[1],
        k::segment(dispatch,{k::core_final_at(0x4755)},{.scratch_stack=2}),
        [](auto& z) -> k::Exit {
            z.load_de(0x4752);
            return z.final_call(0x4755);
        });
    // After the handler, a remaining interaction closes the window directly
    // (the jump to 00:3464); otherwise the window is closed by a call to the
    // same code (01:475F) and the field flow resumes at 00:15DC (01:4762).
    k::add_segment(registry,interaction_dispatch_entries[2],
        k::segment(dispatch,{k::conditional_at(0x475C)}),
        [](auto& z) -> k::Exit {
            z.load_a_direct(0x4758);
            z.or_a(0x475B);
            return z.final_jump_if(0x475C,k::Condition::nz);
        });
    auto close_window=k::segment(dispatch,{k::core_final_at(0x475F)},{.scratch_stack=2});
    close_window.single_instruction=true;
    k::add_segment(registry,{1,0x475F},std::move(close_window),[](auto& z) { return z.final_call(0x475F); });
    auto resume_field=k::segment(dispatch,{k::core_final_at(0x4762)});
    resume_field.single_instruction=true;
    k::add_segment(registry,{1,0x4762},std::move(resume_field),[](auto& z) { return z.final_jump(0x4762); });
    // Some interaction handlers discard their own return and wait for a button.
    k::add_segment(registry,interaction_dispatch_entries[3],
        k::segment(dispatch,{k::core_final_at(0x4766,2)},{.inherited_stack=2}),
        [](auto& z) -> k::Exit {
            z.pop(0x4765,z.r.h,z.r.l);
            return z.final_jump(0x4766);
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

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_interaction_setup_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4FA7},{1,0x4F9B},{kit::core_final_at(0x4FA7)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4FA7); });
    kit::add_joining(registry,{1,0x4FAA},{1,0x4F9B},{kit::conditional_at(0x4FAA)},12,0,[](auto& z) { return z.final_relative_if(0x4FAA,kit::Condition::z); });
    kit::add_joining(registry,{1,0x507C},{1,0x5070},{kit::core_final_at(0x507C)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x507C); });
    kit::add_joining(registry,{1,0x507F},{1,0x5070},{kit::conditional_at(0x507F)},12,0,[](auto& z) { return z.final_relative_if(0x507F,kit::Condition::z); });
}
}
