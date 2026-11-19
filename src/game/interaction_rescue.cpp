// SPDX-License-Identifier: GPL-3.0-or-later
// Myau rescue: price dialogue, potion and party restoration.
#include "game/joins.h"
#include "game/interaction_rescue.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit prepare_rescue_question(Z& z) {
    z.load_hl(0x4C19);
    z.store_hl_direct(0x4C1C);
    z.load_hl(0x4C1F);
    return z.final_call(0x4C22);
}
template<class Z> k::Exit ask_to_use_potion(Z& z) {
    z.push(0x4C37,z.r.h,z.r.l);
    z.load_hl(0x4C38);
    return z.final_call(0x4C3B);
}
template<class Z> k::Exit resume_potion_choice(Z& z) {
    z.pop(0x4C41,z.r.h,z.r.l);
    return z.final_relative_if(0x4C42,k::Condition::nz);
}
template<class Z> k::Exit consume_potion(Z& z) {
    z.push(0x4C44,z.r.b,z.r.c);
    return z.final_call(0x4C45);
}
template<class Z> k::Exit resume_rescue_dialogue(Z& z) {
    z.pop(0x4C48,z.r.b,z.r.c);
    z.load_hl(0x4C49);
    return z.final_call(0x4C4C);
}
template<class Z> k::Exit unlock_myau(Z& z) {
    z.pop(0x4C52,z.r.h,z.r.l);
    z.load_index(0x4C53,k::Index::iy);
    return z.final_call(0x4C57);
}
template<class Z> k::Exit grant_rescue_reward(Z& z) {
    z.load_immediate(0x4C5A,z.r.a);
    z.store_a_direct(0x4C5C);
    z.load_immediate(0x4C5F,z.r.a);
    z.store_a_direct(0x4C61);
    return z.final_call(0x4C64);
}
}
void register_interaction_rescue(RoutineRegistry& registry) {
    using k::core_final_at; using k::segment; const RoutineEntry unit{1,0x4C0D};
    k::add_segment(registry,{1,0x4C19},segment(unit,{core_final_at(0x4C22)},{.scratch_stack=2}),[](auto& z) { return prepare_rescue_question(z); });
    k::add_segment(registry,{1,0x4C37},segment(unit,{core_final_at(0x4C3B,-2)},{.scratch_stack=4}),[](auto& z) { return ask_to_use_potion(z); });
    k::add_segment(registry,{1,0x4C41},segment(unit,{k::conditional_at(0x4C42,2)},{.inherited_stack=2}),[](auto& z) { return resume_potion_choice(z); });
    k::add_segment(registry,{1,0x4C44},segment(unit,{core_final_at(0x4C45,-2)},{.scratch_stack=4}),[](auto& z) { return consume_potion(z); });
    k::add_segment(registry,{1,0x4C48},segment(unit,{core_final_at(0x4C4C,2)},{.inherited_stack=2}),[](auto& z) { return resume_rescue_dialogue(z); });
    k::add_segment(registry,{1,0x4C52},segment(unit,{core_final_at(0x4C57,2)},{.inherited_stack=2}),[](auto& z) { return unlock_myau(z); });
    k::add_segment(registry,{1,0x4C5A},segment(unit,{core_final_at(0x4C64)},{.scratch_stack=2}),[](auto& z) { return grant_rescue_reward(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_interaction_rescue_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x43CF},{1,0x4C0D},{kit::core_final_at(0x43CF)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x43CF); });
    kit::add_joining(registry,{1,0x4C25},{1,0x4C0D},{kit::core_final_at(0x4C25)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4C25); });
    kit::add_joining(registry,{1,0x4C28},{1,0x4C0D},{kit::conditional_at(0x4C28)},12,0,[](auto& z) { return z.final_relative_if(0x4C28,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x4C3E},{1,0x4C0D},{kit::core_final_at(0x4C3E)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4C3E); });
    kit::add_joining(registry,{1,0x4C4F},{1,0x4C0D},{kit::core_final_at(0x4C4F)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4C4F); });
    kit::add_joining(registry,{1,0x4C67},{1,0x4C0D},{kit::core_final_at(0x4C67)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x4C67); });
}
}
