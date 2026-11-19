// SPDX-License-Identifier: GPL-3.0-or-later
// Advance the paid story interaction and its later item checks.
#include "game/joins.h"
#include "game/interaction_progression.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit set_story_actor(Z& z) {
    z.load_immediate(0x4E02,z.r.a);
    return z.final_call(0x4E04);
}
template<class Z> k::Exit quote_story_price(Z& z) {
    z.load_hl(0x4E25);
    z.store_hl_direct(0x4E28);
    z.load_hl(0x4E2B);
    return z.final_call(0x4E2E);
}
template<class Z> k::Exit check_story_price(Z& z) {
    z.load_de(0x4E3C);
    z.load_hl_direct(0x4E3F);
    z.or_a(0x4E42);
    z.subtract_hl_with_carry(0x4E43,z.de());
    return z.final_relative_if(0x4E45,k::Condition::nc);
}
template<class Z> k::Exit purchase_story_progress(Z& z) {
    z.store_hl_direct(0x4E4D);
    z.load_immediate(0x4E50,z.r.a);
    z.store_a_direct(0x4E52);
    z.load_hl(0x4E55);
    return z.final_jump(0x4E58);
}
template<class Z> k::Exit advance_story_progress(Z& z,std::uint16_t at,bool call) {
    z.increment(at,z.r.a);
    z.store_a_direct(static_cast<std::uint16_t>(at+1));
    z.load_hl(static_cast<std::uint16_t>(at+4));
    const auto exit=static_cast<std::uint16_t>(at+7);
    return call ? z.final_call(exit) : z.final_jump(exit);
}
template<class Z> k::Exit finish_story_progress(Z& z) {
    z.load_immediate(0x4E91,z.r.a);
    z.store_a_direct(0x4E93);
    z.load_hl(0x4E96);
    return z.final_jump(0x4E99);
}
}
void register_interaction_progression(RoutineRegistry& registry) {
    using k::core_final_at; using k::segment; const RoutineEntry unit{1,0x4DFB};
    k::add_segment(registry,{1,0x4E02},segment(unit,{core_final_at(0x4E04)},{.scratch_stack=2}),[](auto& z) { return set_story_actor(z); });
    k::add_segment(registry,{1,0x4E25},segment(unit,{core_final_at(0x4E2E)},{.scratch_stack=2}),[](auto& z) { return quote_story_price(z); });
    k::add_segment(registry,{1,0x4E3C},segment(unit,{k::conditional_at(0x4E45)}),[](auto& z) { return check_story_price(z); });
    k::add_segment(registry,{1,0x4E4D},segment(unit,{core_final_at(0x4E58)}),[](auto& z) { return purchase_story_progress(z); });
    k::add_segment(registry,{1,0x4E5F},segment(unit,{core_final_at(0x4E66)}),[](auto& z) { return advance_story_progress(z,0x4E5F,false); });
    k::add_segment(registry,{1,0x4E6D},segment(unit,{core_final_at(0x4E74)},{.scratch_stack=2}),[](auto& z) { return advance_story_progress(z,0x4E6D,true); });
    k::add_segment(registry,{1,0x4E91},segment(unit,{core_final_at(0x4E99)}),[](auto& z) { return finish_story_progress(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_interaction_progression_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4E07},{1,0x4DFB},{kit::core_final_at(0x4E07)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4E07); });
    kit::add_joining(registry,{1,0x4E31},{1,0x4DFB},{kit::core_final_at(0x4E31)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4E31); });
    kit::add_joining(registry,{1,0x4E34},{1,0x4DFB},{kit::conditional_at(0x4E34)},12,0,[](auto& z) { return z.final_relative_if(0x4E34,kit::Condition::z); });
}
}
