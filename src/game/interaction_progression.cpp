// SPDX-License-Identifier: GPL-3.0-or-later
// Advance the paid story interaction and its later item checks.
#include "game/interaction_progression.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit set_story_actor(Z& z) {
    z.load_immediate(0x4E02,z.r.a);
    return k::continues(0x4E04,k::call_cycles);
}
template<class Z> k::Exit quote_story_price(Z& z) {
    z.load_hl(0x4E25);
    z.store_hl_direct(0x4E28);
    z.load_hl(0x4E2B);
    return k::continues(0x4E2E,k::call_cycles);
}
template<class Z> k::Exit check_story_price(Z& z) {
    z.load_de(0x4E3C);
    z.load_hl_direct(0x4E3F);
    z.or_a(0x4E42);
    z.subtract_hl_with_carry(0x4E43,z.de());
    return k::relative_if_exit(0x4E45,!z.carry());
}
template<class Z> k::Exit purchase_story_progress(Z& z) {
    z.store_hl_direct(0x4E4D);
    z.load_immediate(0x4E50,z.r.a);
    z.store_a_direct(0x4E52);
    z.load_hl(0x4E55);
    return k::continues(0x4E58,k::jump_cycles);
}
template<class Z> k::Exit advance_story_progress(Z& z,std::uint16_t at,bool call) {
    z.increment(at,z.r.a);
    z.store_a_direct(static_cast<std::uint16_t>(at+1));
    z.load_hl(static_cast<std::uint16_t>(at+4));
    return k::continues(static_cast<std::uint16_t>(at+7),call ? k::call_cycles : k::jump_cycles);
}
template<class Z> k::Exit finish_story_progress(Z& z) {
    z.load_immediate(0x4E91,z.r.a);
    z.store_a_direct(0x4E93);
    z.load_hl(0x4E96);
    return k::continues(0x4E99,k::jump_cycles);
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
}
