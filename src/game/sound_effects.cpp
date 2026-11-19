// SPDX-License-Identifier: GPL-3.0-or-later
// Sound requests that stop voices or set up fades.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
// Start the fade-out and silence one channel while the others fade.
template<class Z> k::Exit fade_out_common(Z& z) {
    z.load_immediate(0x81D5,z.r.a);
    z.store_a_direct(0x81D7);
    z.load_a_direct(0x81DA);
    z.or_a(0x81DD);
    if (!z.jump_if(0x81DE,z.sign())) { z.xor_a(0x81E1);
    z.store_a_direct(0x81E2); }
    z.load_immediate(0x81E5,z.r.a);
    z.output(0x81E7);
    z.xor_a(0x81E9);
    z.store_a_direct(0x81EA);
    return z.final_jump(0x81ED);
}
template<class Z> k::Exit fade_out_start(Z& z) {
    z.load_immediate(0x81CD,z.r.a);
    z.store_a_direct(0x81CF);
    z.store_a_direct(0x81D2);
    return fade_out_common(z);
}
// Conditional dispatch is kept at its original boundary; the active branch
// clears the voice and sends its two chip commands in order.
template<class Z> k::Exit stop_voice_check(Z& z) {
    z.load_hl(0x81F0);
    z.test_bit_at_hl(0x81F3,7);
    return z.final_jump_if(0x81F5,k::Condition::z);
}
template<class Z> k::Exit stop_voice(Z& z) {
    z.xor_a(0x81F8);
    z.store_register_to_hl(0x81F9,z.r.a);
    z.load_immediate(0x81FA,z.r.a);
    z.output(0x81FC);
    z.load_immediate(0x81FE,z.r.a);
    z.output(0x8200);
    return z.final_jump(0x8202);
}
template<class Z> k::Exit fade_request_check(Z& z) {
    z.load_a_direct(0x8205);
    z.compare_immediate(0x8208);
    return z.final_jump_if(0x820A,k::Condition::z);
}
template<class Z> k::Exit fade_request_mark(Z& z) {
    z.load_immediate(0x820D,z.r.a);
    z.store_a_direct(0x820F);
    return z.final_jump(0x8212);
}
// Clear the shared-channel bit in three consecutive voice records.
template<class Z> void clear_three(Z& z) {
    z.load_de(0x8234);
    z.load_immediate(0x8237,z.r.b);
    do {
        z.bus().fetch(0x8239);
        z.bus().fetch(0x823A);
        const auto value=z.bus().read(z.hl());
        z.bus().write(z.hl(),static_cast<std::uint8_t>(value&~4u));
        z.bus().advance(15);
        z.add_to_hl(0x823B,z.de());
    } while (z.decrement_jump(0x823C));
}
template<class Z> k::Exit clear_three_entry(Z& z) {
    clear_three(z);
    return z.final_return(0x823E);
}
// Stop both groups of shared voices, then send the ROM's silence commands.
template<class Z> k::Exit stop_shared(Z& z) {
    z.xor_a(0x8215);
    z.store_a_direct(0x8216);
    z.store_a_direct(0x8219);
    z.load_hl(0x821C);
    if (!z.inline_call(0x821F,0x823E,[](auto& v) { clear_three(v); })) return k::refused();
    z.load_hl(0x8222);
    if (!z.inline_call(0x8225,0x823E,[](auto& v) { clear_three(v); })) return k::refused();
    z.load_hl(0x8228);
    z.load_immediate(0x822B,z.r.c);
    z.load_immediate(0x822D,z.r.b);
    do z.output_increment_repeat(0x822F); while (z.r.b);
    return z.final_jump(0x8231);
}
// Clear a shared-channel flag without changing the processor flags.
template<class Z> void clear_shared_flag(Z& z,std::uint16_t at) {
    z.bus().fetch(at);
    z.bus().fetch(static_cast<std::uint16_t>(at+1));
    const auto value=z.bus().read(z.hl());
    z.bus().write(z.hl(),static_cast<std::uint8_t>(value&~4u));
    z.bus().advance(15);
}
// Ending this effect quiets its channel and restores any interrupted voices.
template<class Z> k::Exit finish_shared_effect(Z& z) {
    z.load_hl(0x8627);
    clear_shared_flag(z,0x862A);
    z.xor_a(0x862C);
    z.store_a_direct(0x862D);
    z.store_to_indexed(0x8630,k::Index::ix,z.r.a);
    z.load_from_indexed(0x8633,k::Index::ix,z.r.a);
    z.add_immediate(0x8636);
    z.output(0x8638);
    z.load_immediate(0x863A,z.r.a);
    z.output(0x863C);
    z.load_a_direct(0x863E);
    z.and_immediate(0x8641);
    return z.final_relative_if(0x8643,k::Condition::z);
}
template<class Z> k::Exit restore_first_shared_pitch(Z& z) {
    z.load_hl(0x8645);
    z.load_from_hl(0x8648,z.r.a);
    z.increment_hl(0x8649);
    z.load_from_hl(0x864A,z.r.h);
    z.copy(0x864B,z.r.l,z.r.a);
    z.load_a_direct(0x864C);
    return z.final_call(0x864F);
}
template<class Z> k::Exit clear_shared_group(Z& z) {
    z.load_hl(0x8657);
    clear_shared_flag(z,0x865A);
    z.load_hl(0x865C);
    clear_shared_flag(z,0x865F);
    z.load_hl(0x8661);
    clear_shared_flag(z,0x8664);
    z.load_a_direct(0x8666);
    z.and_immediate(0x8669);
    return z.final_relative_if(0x866B,k::Condition::z);
}
template<class Z> k::Exit after_first_shared_pitch(Z& z) {
    z.load_hl(0x8652);
    clear_shared_flag(z,0x8655);
    return clear_shared_group(z);
}
template<class Z> k::Exit restore_second_shared_pitch(Z& z) {
    z.load_hl(0x866D);
    z.load_from_hl(0x8670,z.r.a);
    z.increment_hl(0x8671);
    z.load_from_hl(0x8672,z.r.h);
    z.copy(0x8673,z.r.l,z.r.a);
    z.load_a_direct(0x8674);
    return z.final_call(0x8677);
}
// This effect was entered with two saved words, which are discarded before
// returning to the original caller.
template<class Z> k::Exit return_from_shared_effect(Z& z) {
    z.load_hl(0x867F);
    clear_shared_flag(z,0x8682);
    z.pop(0x8684,z.r.h,z.r.l);
    z.pop(0x8685,z.r.h,z.r.l);
    return z.final_return(0x8686);
}
template<class Z> k::Exit after_second_shared_pitch(Z& z) {
    z.load_hl(0x867A);
    clear_shared_flag(z,0x867D);
    return return_from_shared_effect(z);
}
// Following the original silence call, save each voice level and initialize
// a fade-in. The caller's IX is saved and restored around the six records.
template<class Z> k::Exit prepare_fade_in(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_direct(0x8199);
    z.store_a_direct(0x819C);
    z.load_immediate(0x819F,z.r.a);
    z.store_a_direct(0x81A1);
    z.load_immediate(0x81A4,z.r.a);
    z.store_a_direct(0x81A6);
    z.load_hl(0x81A9);
    z.set_bit_at_hl(0x81AC,2);
    z.push_index(0x81AE,ix);
    z.load_index(0x81B0,ix);
    z.load_immediate(0x81B4,z.r.b);
    z.load_de(0x81B6);
    do {
        z.load_from_indexed(0x81B9,ix,z.r.a);
        z.store_to_indexed(0x81BC,ix,z.r.a);
        z.load_immediate(0x81BF,z.r.a);
        z.store_to_indexed(0x81C1,ix,z.r.a);
        z.add_to_index(0x81C4,ix,z.de());
    } while (z.decrement_jump(0x81C6));
    z.pop_index(0x81C8,ix);
    return z.final_jump(0x81CA);
}
}
void register_sound_effects(RoutineRegistry& registry) {
    const RoutineEntry shared_end{12,0x8627};
    // The shared-effect end writes through IX, which must point into the voice
    // records in work RAM (other values reach the mapper). The guard reads
    // registers only: valid at entry and after a delivery.
    k::add_segment(registry,shared_end,k::segment(shared_end,{k::conditional_at(0x8643)}),
        k::Guard{"IX points into the voice records in work RAM",[](const Machine& machine) {
            const auto address=machine.cpu().ix_iy[0].uint16_value;
            return address>=0xc000 && address<=0xdfe8;
        }},[](auto& z) { return finish_shared_effect(z); });
    k::add_segment(registry,{12,0x8645},k::segment(shared_end,{k::core_final_at(0x864F)},{.scratch_stack=2}),[](auto& z) { return restore_first_shared_pitch(z); });
    k::add_segment(registry,{12,0x8652},k::segment(shared_end,{k::conditional_at(0x866B)}),[](auto& z) { return after_first_shared_pitch(z); });
    k::add_segment(registry,{12,0x8657},k::segment(shared_end,{k::conditional_at(0x866B)}),[](auto& z) { return clear_shared_group(z); });
    k::add_segment(registry,{12,0x866D},k::segment(shared_end,{k::core_final_at(0x8677)},{.scratch_stack=2}),[](auto& z) { return restore_second_shared_pitch(z); });
    k::add_segment(registry,{12,0x867A},k::segment(shared_end,{k::returned_at(0x8686,4)},{.inherited_stack=4}),[](auto& z) { return after_second_shared_pitch(z); });
    k::add_segment(registry,{12,0x867F},k::segment(shared_end,{k::returned_at(0x8686,4)},{.inherited_stack=4}),[](auto& z) { return return_from_shared_effect(z); });
    k::add_segment(registry,{12,0x8199},k::segment({12,0x8196},{k::core_final_at(0x81CA)},{.scratch_stack=2}),[](auto& z) { return prepare_fade_in(z); });
    k::add_segment(registry,sound::fade_out,k::segment(sound::fade_out,{k::core_final_at(0x81ED)}),[](auto& z) { return fade_out_start(z); });
    k::add_segment(registry,{12,0x81D5},k::segment(sound::fade_out,{k::core_final_at(0x81ED)}),[](auto& z) { return fade_out_common(z); });
    k::add_segment(registry,sound::stop_voice,k::segment(sound::stop_voice,{k::conditional_at(0x81F5)}),[](auto& z) { return stop_voice_check(z); });
    // Admitted only while HL points at the voice record at C12E, the pointer
    // this entry was verified with. Registers only: valid across a delivery.
    k::add_segment(registry,{12,0x81F8},k::segment(sound::stop_voice,{k::core_final_at(0x8202)}),
        k::Guard{"HL points at the voice record C12E",[](const Machine& machine) { return machine.cpu().hl.uint16_value==0xc12e; }},
        [](auto& z) { return stop_voice(z); });
    k::add_segment(registry,sound::fade_request,k::segment(sound::fade_request,{k::conditional_at(0x820A)}),[](auto& z) { return fade_request_check(z); });
    k::add_segment(registry,{12,0x820D},k::segment(sound::fade_request,{k::core_final_at(0x8212)}),[](auto& z) { return fade_request_mark(z); });
    k::add_segment(registry,sound::stop_shared,k::segment(sound::stop_shared,{k::core_final_at(0x8231)},{.scratch_stack=2}),[](auto& z) { return stop_shared(z); });
    // Admitted only while HL points at the voice records at C08E or C1AE, the
    // pointers this entry was verified with. Registers only: valid across a
    // delivery.
    k::add_segment(registry,{12,0x8234},k::segment({12,0x8234},{k::returned_at(0x823E)}),
        k::Guard{"HL points at the voice records C08E or C1AE",[](const Machine& machine) {
            const auto hl=machine.cpu().hl.uint16_value;
            return hl==0xc08e || hl==0xc1ae;
        }},[](auto& z) { return clear_three_entry(z); });
}
}
