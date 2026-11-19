// SPDX-License-Identifier: GPL-3.0-or-later
// Companion encounter effects, followed by existing dialogue and scene paths.
#include "game/joins.h"
#include "game/interaction_companion.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit start_companion_event(Z& z) {
    z.load_immediate(0x49FF,z.r.a);
    z.store_a_direct(0x4A01);
    z.load_immediate(0x4A04,z.r.a);
    z.store_a_direct(0x4A06);
    z.load_hl(0x4A09);
    return z.final_call(0x4A0C);
}
template<class Z> k::Exit remove_companion(Z& z) {
    z.load_hl(0x4A0F);
    z.store_hl_direct(0x4A12);
    z.load_hl(0x4A15);
    z.relative(0x4A18);
    return z.final_call(0x4A1D);
}
template<class Z> k::Exit prepare_companion_reward(Z& z) {
    z.load_hl(0x4A30);
    z.store_hl_direct(0x4A33);
    z.load_immediate(0x4A36,z.r.a);
    z.store_a_direct(0x4A38);
    return z.final_jump(0x4A3B);
}
}
void register_interaction_companion(RoutineRegistry& registry) {
    using k::core_final_at; using k::segment; const RoutineEntry unit{1,0x49E6};
    k::add_segment(registry,{1,0x49FF},segment(unit,{core_final_at(0x4A0C)},{.scratch_stack=2}),[](auto& z) { return start_companion_event(z); });
    k::add_segment(registry,{1,0x4A0F},segment(unit,{core_final_at(0x4A1D)},{.scratch_stack=2}),[](auto& z) { return remove_companion(z); });
    k::add_segment(registry,{1,0x4A30},segment(unit,{core_final_at(0x4A3B)}),[](auto& z) { return prepare_companion_reward(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_interaction_companion_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4A20},{1,0x49E6},{kit::core_final_at(0x4A20)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4A20); });
    kit::add_joining(registry,{1,0x5389},{1,0x49E6},{kit::core_final_at(0x5389)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x5389); });
}
}
