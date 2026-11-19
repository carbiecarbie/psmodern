// SPDX-License-Identifier: GPL-3.0-or-later
// The frame's sound update visits the active voice records in order.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
constexpr auto ix=k::Index::ix;
constexpr RoutineEntry unit{12,0x8043};

template<class Z> k::Exit choose_voice_group(Z& z) {
    z.load_a_direct(0x8053);
    z.or_a(0x8056);
    return z.final_jump_if(0x8057,k::Condition::m);
}
// The saved counter is restored after each voice's optional update.
template<class Z> k::Exit first_group_iteration(Z& z) {
    z.push(0x8060,z.r.b,z.r.c);
    z.load_immediate(0x8061,z.r.a);
    z.compare_register(0x8063,z.r.b);
    return z.final_relative_if(0x8064,k::Condition::z);
}
template<class Z> k::Exit begin_first_group(Z& z) {
    z.load_index(0x805A,ix);
    z.load_immediate(0x805E,z.r.b);
    return first_group_iteration(z);
}
template<class Z> k::Exit call_first_tone(Z& z) {
    z.test_bit_indexed(0x8066,ix,7);
    return z.final_call_if(0x806A,k::Condition::nz);
}
template<class Z> k::Exit call_first_noise(Z& z) {
    z.test_bit_indexed(0x8076,ix,7);
    return z.final_call_if(0x807A,k::Condition::nz);
}
template<class Z> k::Exit next_first_voice(Z& z) {
    z.load_de(0x806D);
    z.add_to_index(0x8070,ix,z.de());
    z.pop(0x8072,z.r.b,z.r.c);
    return z.final_decrement_jump(0x8073);
}
template<class Z> k::Exit second_group_iteration(Z& z) {
    z.push(0x8085,z.r.b,z.r.c);
    z.load_immediate(0x8086,z.r.a);
    z.compare_register(0x8088,z.r.b);
    return z.final_relative_if(0x8089,k::Condition::z);
}
template<class Z> k::Exit begin_second_group(Z& z) {
    z.load_index(0x807F,ix);
    z.load_immediate(0x8083,z.r.b);
    return second_group_iteration(z);
}
template<class Z> k::Exit call_second_tone(Z& z) {
    z.test_bit_indexed(0x808B,ix,7);
    return z.final_call_if(0x808F,k::Condition::nz);
}
template<class Z> k::Exit call_second_noise(Z& z) {
    z.test_bit_indexed(0x809B,ix,7);
    return z.final_call_if(0x809F,k::Condition::nz);
}
template<class Z> k::Exit next_second_voice(Z& z) {
    z.load_de(0x8092);
    z.add_to_index(0x8095,ix,z.de());
    z.pop(0x8097,z.r.b,z.r.c);
    return z.final_decrement_jump(0x8098);
}
// Joining instructions of the tick loop: the return that ends a group when
// the voice counter runs out, and the jump back to the next voice.
void add_tick_return(RoutineRegistry& registry,std::uint16_t at) {
    auto spec=k::segment(unit,{k::returned_at(at)});
    spec.single_instruction=true;
    k::add_segment(registry,{12,at},std::move(spec),[at](auto& z) { return z.final_return(at); });
}
void add_tick_next_voice(RoutineRegistry& registry,std::uint16_t at) {
    auto spec=k::segment(unit,{k::core_final_at(at)});
    spec.single_instruction=true;
    k::add_segment(registry,{12,at},std::move(spec),[at](auto& z) { return z.final_relative(at); });
}
}
void register_sound_tick(RoutineRegistry& registry) {
    k::add_segment(registry,{12,0x8053},k::segment(unit,{k::conditional_at(0x8057)}),[](auto& z) { return choose_voice_group(z); });
    k::add_segment(registry,{12,0x805A},k::segment(unit,{k::conditional_at(0x8064,-2)},{.scratch_stack=2}),[](auto& z) { return begin_first_group(z); });
    k::add_segment(registry,{12,0x8060},k::segment(unit,{k::conditional_at(0x8064,-2)},{.scratch_stack=2}),[](auto& z) { return first_group_iteration(z); });
    k::add_segment(registry,{12,0x8066},k::segment(unit,{k::core_final_at(0x806A)},{.scratch_stack=2,.inherited_stack=2}),[](auto& z) { return call_first_tone(z); });
    k::add_segment(registry,{12,0x806D},k::segment(unit,{k::conditional_at(0x8073,2)},{.inherited_stack=2}),[](auto& z) { return next_first_voice(z); });
    k::add_segment(registry,{12,0x8076},k::segment(unit,{k::core_final_at(0x807A)},{.scratch_stack=2,.inherited_stack=2}),[](auto& z) { return call_first_noise(z); });
    k::add_segment(registry,{12,0x807F},k::segment(unit,{k::conditional_at(0x8089,-2)},{.scratch_stack=2}),[](auto& z) { return begin_second_group(z); });
    k::add_segment(registry,{12,0x8085},k::segment(unit,{k::conditional_at(0x8089,-2)},{.scratch_stack=2}),[](auto& z) { return second_group_iteration(z); });
    k::add_segment(registry,{12,0x808B},k::segment(unit,{k::core_final_at(0x808F)},{.scratch_stack=2,.inherited_stack=2}),[](auto& z) { return call_second_tone(z); });
    k::add_segment(registry,{12,0x8092},k::segment(unit,{k::conditional_at(0x8098,2)},{.inherited_stack=2}),[](auto& z) { return next_second_voice(z); });
    k::add_segment(registry,{12,0x809B},k::segment(unit,{k::core_final_at(0x809F)},{.scratch_stack=2,.inherited_stack=2}),[](auto& z) { return call_second_noise(z); });
    add_tick_return(registry,0x8075);
    add_tick_next_voice(registry,0x807D);
    add_tick_return(registry,0x809A);
    add_tick_next_voice(registry,0x80A2);
}
}
