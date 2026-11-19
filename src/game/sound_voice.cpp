// SPDX-License-Identifier: GPL-3.0-or-later
// Advance a noise voice and dispatch its score bytes.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
constexpr auto ix=k::Index::ix;

// Increment elapsed time; when a note is due the original conditional CALL
// enters the score reader at 86F9 with its original return address.
template<class Z> k::Exit tick_noise_voice(Z& z) {
    z.increment_indexed(0x86D0,ix);
    z.load_from_indexed(0x86D3,ix,z.r.a);
    z.subtract_indexed(0x86D6,ix);
    return z.final_call_if(0x86D9,k::Condition::z);
}
// Muted, disabled or inactive voices return without a chip write. Active
// voices select their envelope table and leave the original CALL at 86EE.
template<class Z> k::Exit noise_voice_envelope(Z& z) {
    z.test_bit_indexed(0x86DC,ix,2);
    if (auto exit=z.final_return_if(0x86E0,k::Condition::nz)) return *exit;
    z.test_bit_indexed(0x86E1,ix,4);
    if (auto exit=z.final_return_if(0x86E5,k::Condition::nz)) return *exit;
    z.load_from_indexed(0x86E6,ix,z.r.a);
    z.decrement(0x86E9,z.r.a);
    if (auto exit=z.final_return_if(0x86EA,k::Condition::m)) return *exit;
    z.load_hl(0x86EB);
    return z.final_call(0x86EE);
}
template<class Z> k::Exit output_noise_level(Z& z) {
    z.or_immediate(0x86F4);
    z.output(0x86F6);
    return z.final_return(0x86F8);
}
// Fetch a score position from the voice, then classify its next byte. Each
// conditional transfer is an instruction boundary that ends a segment.
template<class Z> k::Exit read_score_position(Z& z) {
    z.load_from_indexed(0x86F9,ix,z.r.e);
    z.load_from_indexed(0x86FC,ix,z.r.d);
    z.load_a_from_pair(0x86FF,z.de());
    return k::fall_through(0x86FF,0x8700);
}
template<class Z> k::Exit classify_score_byte(Z& z) {
    z.increment_pair(0x8700,z.r.d,z.r.e);
    z.compare_immediate(0x8701);
    return z.final_jump_if(0x8703,k::Condition::nc);
}
template<class Z> k::Exit classify_note_byte(Z& z) {
    z.compare_immediate(0x8706);
    return z.final_jump_if(0x8708,k::Condition::c);
}
template<class Z> k::Exit classify_following_byte(Z& z) {
    z.load_a_from_pair(0x870E,z.de());
    z.increment_pair(0x870F,z.r.d,z.r.e);
    z.compare_immediate(0x8710);
    return z.final_jump_if(0x8712,k::Condition::c);
}
template<class Z> k::Exit keep_current_note(Z& z) {
    z.decrement_pair(0x8715,z.r.d,z.r.e);
    z.load_from_indexed(0x8716,ix,z.r.a);
    z.store_to_indexed(0x8719,ix,z.r.a);
    return z.final_jump(0x871C);
}
template<class Z> k::Exit keep_note_from_previous_byte(Z& z) {
    z.decrement_pair(0x871F,z.r.d,z.r.e);
    return z.final_jump(0x8720);
}
template<class Z> k::Exit dispatch_noise_command(Z& z) {
    z.load_hl(0x8723);
    return z.final_jump(0x8726);
}
template<class Z> k::Exit advance_noise_command(Z& z) {
    z.increment_pair(0x8729,z.r.d,z.r.e);
    return z.final_jump(0x872A);
}
// The original switches to its alternate register set before this segment.
// Its work pointer is established by the sound tick and must stay in RAM;
// the final EXX returns to the original register set.
template<class Z> k::Exit mark_tone_update(Z& z) {
    z.store_to_hl(0x878E);
    z.exchange_alternates(0x8790);
    return k::fall_through(0x8790,0x8791);
}
// The tonal voice shares the same note clock, then examines the envelope
// and pitch state. Conditional calls and branches end a segment.
template<class Z> k::Exit tick_tone_voice(Z& z) {
    z.increment_indexed(0x8772,ix);
    z.load_from_indexed(0x8775,ix,z.r.a);
    z.subtract_indexed(0x8778,ix);
    return z.final_call_if(0x877B,k::Condition::z);
}
template<class Z> k::Exit tone_note_result(Z& z) {
    z.store_a_direct(0x877E);
    z.compare_immediate(0x8781);
    return z.final_jump_if(0x8783,k::Condition::z);
}
template<class Z> k::Exit tone_pitch_enabled(Z& z) {
    z.test_bit_indexed(0x8786,ix,5);
    return z.final_jump_if(0x878A,k::Condition::z);
}
template<class Z> k::Exit tone_voice_active(Z& z);
// Advance signed pitch drift, including a carry into the high byte.
template<class Z> k::Exit tone_drift(Z& z) {
    z.load_from_indexed(0x8791,ix,z.r.a);
    z.test_bit(0x8794,7,z.r.a);
    if (!z.relative_if(0x8796,z.zero())) {
        z.add_from_indexed(0x8798,ix);
        if (!z.relative_if(0x879B,z.carry())) {
            z.decrement_indexed(0x879D,ix);
            z.relative(0x87A0);
        }
    } else {
        z.add_from_indexed(0x87A2,ix);
        if (!z.relative_if(0x87A5,!z.carry())) z.increment_indexed(0x87A7,ix);
    }
    z.store_to_indexed(0x87AA,ix,z.r.a);
    return tone_voice_active(z);
}
// Check whether a voice is muted or has reached the envelope sentinel.
template<class Z> k::Exit tone_voice_active(Z& z) {
    z.test_bit_indexed(0x87AD,ix,2);
    if (auto exit=z.final_return_if(0x87B1,k::Condition::nz)) return *exit;
    z.load_from_indexed(0x87B2,ix,z.r.a);
    z.compare_immediate(0x87B5);
    if (auto exit=z.final_return_if(0x87B7,k::Condition::z)) return *exit;
    return z.final_relative_if(0x87B8,k::Condition::nz);
}
template<class Z> k::Exit tone_envelope_marker(Z& z) {
    z.load_from_indexed(0x87BA,ix,z.r.a);
    z.compare_immediate(0x87BD);
    return z.final_jump_if(0x87BF,k::Condition::z);
}
template<class Z> k::Exit tone_envelope_index(Z& z) {
    z.load_from_indexed(0x87C2,ix,z.r.a);
    z.decrement(0x87C5,z.r.a);
    return z.final_jump_if(0x87C6,k::Condition::m);
}
template<class Z> k::Exit tone_envelope_lookup(Z& z) {
    z.load_hl(0x87C9);
    return z.final_call(0x87CC);
}
// Send an envelope value to its sound-chip channel and load the note result.
template<class Z> k::Exit tone_envelope_output(Z& z) {
    z.bus().fetch(0x87D2);
    z.bus().fetch(0x87D3);
    const auto d=static_cast<std::int8_t>(z.bus().read(0x87D4));
    z.r.wz=static_cast<std::uint16_t>(z.r.ix+d);
    z.r.a=static_cast<std::uint8_t>(z.r.a|z.bus().read(z.r.wz));
    kit::logic_flags(z.r,false);
    z.bus().advance(19);
    z.add_immediate(0x87D5);
    z.output(0x87D7);
    z.load_a_direct(0x87D9);
    return k::fall_through(0x87D9,0x87DC);
}
template<class Z> k::Exit tone_note_sign(Z& z) {
    z.or_a(0x87DC);
    return z.final_jump_if(0x87DD,k::Condition::m);
}
template<class Z> k::Exit tone_pitch_index(Z& z) {
    z.test_bit_indexed(0x87E0,ix,7);
    if (auto exit=z.final_return_if(0x87E4,k::Condition::nz)) return *exit;
    z.load_from_indexed(0x87E5,ix,z.r.a);
    z.decrement(0x87E8,z.r.a);
    return z.final_jump_if(0x87E9,k::Condition::p);
}
template<class Z> k::Exit tone_pitch_value(Z& z) {
    z.load_from_indexed(0x87F1,ix,z.r.l);
    z.load_from_indexed(0x87F4,ix,z.r.h);
    return z.final_jump_if(0x87F7,k::Condition::m);
}
template<class Z> k::Exit tone_pitch_from_note(Z& z) {
    z.load_from_indexed(0x87ED,ix,z.r.a);
    z.decrement(0x87F0,z.r.a);
    return tone_pitch_value(z);
}
template<class Z> k::Exit tone_apply_pitch_lookup(Z& z) {
    z.exchange_de_hl(0x87FA);
    z.load_hl(0x87FB);
    return z.final_call(0x87FE);
}
template<class Z> k::Exit tone_choose_channel(Z& z) {
    z.test_bit_indexed(0x8804,ix,6);
    if (auto exit=z.final_return_if(0x8808,k::Condition::nz)) return *exit;
    z.load_from_indexed(0x8809,ix,z.r.a);
    z.compare_immediate(0x880C);
    return z.final_relative_if(0x880E,k::Condition::nz);
}
// Decode the bit fields that choose the envelope pattern for a noise note.
template<class Z> k::Exit test_noise_pattern_3(Z& z) {
    z.test_bit(0x872D,3,z.r.a);
    return z.final_relative_if(0x872F,k::Condition::nz);
}
template<class Z> k::Exit test_noise_pattern_5(Z& z) {
    z.test_bit(0x8731,5,z.r.a);
    return z.final_relative_if(0x8733,k::Condition::nz);
}
template<class Z> k::Exit test_noise_pattern_1(Z& z) {
    z.test_bit(0x8735,1,z.r.a);
    return z.final_relative_if(0x8737,k::Condition::nz);
}
template<class Z> k::Exit test_noise_pattern_0(Z& z) {
    z.test_bit(0x8739,0,z.r.a);
    return z.final_relative_if(0x873B,k::Condition::nz);
}
template<class Z> k::Exit test_noise_pattern_2(Z& z) {
    z.test_bit(0x873D,2,z.r.a);
    return z.final_relative_if(0x873F,k::Condition::nz);
}
// A note without a noise pattern marks the envelope idle and quiets the chip.
template<class Z> k::Exit quiet_noise_note(Z& z) {
    z.store_immediate_indexed(0x8741,ix);
    z.load_immediate(0x8745,z.r.a);
    z.output(0x8747);
    return z.final_return(0x8749);
}
template<class Z> k::Exit select_noise_pattern_two(Z& z) {
    z.load_immediate(0x874B,z.r.a);
    z.load_immediate(0x874D,z.r.b);
    return k::fall_through(0x874D,0x874F);
}
template<class Z> k::Exit choose_noise_variant(Z& z) {
    z.load_immediate(0x8751,z.r.c);
    z.test_bit(0x8753,0,z.r.a);
    return z.final_relative_if(0x8755,k::Condition::nz);
}
template<class Z> k::Exit select_noise_pattern_one(Z& z) {
    z.load_immediate(0x8760,z.r.a);
    z.load_immediate(0x8762,z.r.b);
    return k::fall_through(0x8762,0x8764);
}
template<class Z> k::Exit test_noise_level_modifier(Z& z) {
    z.test_bit(0x8768,2,z.r.a);
    return z.final_relative_if(0x876A,k::Condition::z);
}
template<class Z> k::Exit store_noise_level(Z& z) {
    z.store_to_indexed(0x876E,ix,z.r.b);
    return z.final_return(0x8771);
}
template<class Z> k::Exit lower_noise_level(Z& z) {
    z.decrement(0x876C,z.r.b);
    z.decrement(0x876D,z.r.b);
    return store_noise_level(z);
}
// The pattern selector exchanges AF in the original code. These pieces
// set up or save its chosen pattern on either side of that exchange.
template<class Z> k::Exit choose_noise_pattern_three(Z& z) {
    z.load_immediate(0x8757,z.r.c);
    z.exchange_af(0x8759);
    return k::fall_through(0x8759,0x875A);
}
template<class Z> k::Exit save_noise_pattern(Z& z) {
    z.store_to_indexed(0x8764,ix,z.r.a);
    z.exchange_af(0x8767);
    return k::fall_through(0x8767,0x8768);
}
template<class Z> k::Exit choose_saved_noise_pattern(Z& z) {
    z.copy(0x875A,z.r.a,z.r.c);
    z.load_immediate(0x875B,z.r.b);
    z.relative(0x875D);
    return save_noise_pattern(z);
}
// Voice writes are allowed only where the original's record resides in work
// RAM (other IX values reach the mapper). The guard reads registers only: it
// holds at entry and stays valid after a delivery.
template<class Logic> void add_voice_tick(RoutineRegistry& registry,RoutineEntry entry,SegmentSpec spec,Logic logic) {
    k::add_segment(registry,entry,std::move(spec),
        k::Guard{"IX points into the voice records in work RAM",[](const Machine& machine) {
            const auto address=machine.cpu().ix_iy[0].uint16_value;
            return address>=0xc000 && address<=0xdfe8;
        }},logic);
}
// One joining instruction between the sound routines' segments, as a segment
// of its own: a plain instruction that falls through, a call, a jump or a
// return, according to `body`.
template<class Body> void add_joining(RoutineRegistry& registry,std::uint16_t at,RoutineEntry unit,SegmentExit exit,k::Permissions permissions,Body body) {
    auto spec=k::segment(unit,{exit},permissions);
    spec.single_instruction=true;
    k::add_segment(registry,{12,at},std::move(spec),body);
}
}
void register_sound_voice(RoutineRegistry& registry) {
    const RoutineEntry noise_pattern{12,0x872D};
    k::add_segment(registry,noise_pattern,k::segment(noise_pattern,{k::conditional_at(0x872F)}),[](auto& z) { return test_noise_pattern_3(z); });
    k::add_segment(registry,{12,0x8731},k::segment(noise_pattern,{k::conditional_at(0x8733)}),[](auto& z) { return test_noise_pattern_5(z); });
    k::add_segment(registry,{12,0x8735},k::segment(noise_pattern,{k::conditional_at(0x8737)}),[](auto& z) { return test_noise_pattern_1(z); });
    k::add_segment(registry,{12,0x8739},k::segment(noise_pattern,{k::conditional_at(0x873B)}),[](auto& z) { return test_noise_pattern_0(z); });
    k::add_segment(registry,{12,0x873D},k::segment(noise_pattern,{k::conditional_at(0x873F)}),[](auto& z) { return test_noise_pattern_2(z); });
    add_voice_tick(registry,{12,0x8741},k::segment(noise_pattern,{k::returned_at(0x8749)}),[](auto& z) { return quiet_noise_note(z); });
    k::add_segment(registry,{12,0x874B},k::segment(noise_pattern,{k::core_final_at(0x874D)}),[](auto& z) { return select_noise_pattern_two(z); });
    k::add_segment(registry,{12,0x8751},k::segment(noise_pattern,{k::conditional_at(0x8755)}),[](auto& z) { return choose_noise_variant(z); });
    k::add_segment(registry,{12,0x8760},k::segment(noise_pattern,{k::core_final_at(0x8762)}),[](auto& z) { return select_noise_pattern_one(z); });
    k::add_segment(registry,{12,0x8768},k::segment(noise_pattern,{k::conditional_at(0x876A)}),[](auto& z) { return test_noise_level_modifier(z); });
    add_voice_tick(registry,{12,0x876C},k::segment(noise_pattern,{k::returned_at(0x8771)}),[](auto& z) { return lower_noise_level(z); });
    add_voice_tick(registry,{12,0x876E},k::segment(noise_pattern,{k::returned_at(0x8771)}),[](auto& z) { return store_noise_level(z); });
    k::add_segment(registry,{12,0x8757},k::segment(noise_pattern,{k::core_final_at(0x8759)}),[](auto& z) { return choose_noise_pattern_three(z); });
    add_voice_tick(registry,{12,0x875A},k::segment(noise_pattern,{k::core_final_at(0x8767)}),[](auto& z) { return choose_saved_noise_pattern(z); });
    add_voice_tick(registry,{12,0x8764},k::segment(noise_pattern,{k::core_final_at(0x8767)}),[](auto& z) { return save_noise_pattern(z); });
    const RoutineEntry tone{12,0x8772};
    // Admitted only while HL points at the driver byte at C00C, the pointer
    // this entry was verified with. Registers only: valid across a delivery.
    k::add_segment(registry,{12,0x878E},k::segment(tone,{k::core_final_at(0x8790)}),
        k::Guard{"HL points at the driver byte C00C",[](const Machine& machine) { return machine.cpu().hl.uint16_value==0xc00c; }},
        [](auto& z) { return mark_tone_update(z); });
    add_voice_tick(registry,tone,k::segment(tone,{k::core_final_at(0x877B)},{.scratch_stack=2}),[](auto& z) { return tick_tone_voice(z); });
    k::add_segment(registry,{12,0x877E},k::segment(tone,{k::conditional_at(0x8783)}),[](auto& z) { return tone_note_result(z); });
    k::add_segment(registry,{12,0x8786},k::segment(tone,{k::conditional_at(0x878A)}),[](auto& z) { return tone_pitch_enabled(z); });
    add_voice_tick(registry,{12,0x8791},k::segment(tone,{k::returned_at(0x87B1),k::returned_at(0x87B7),k::conditional_at(0x87B8)}),[](auto& z) { return tone_drift(z); });
    k::add_segment(registry,{12,0x87AD},k::segment(tone,{k::returned_at(0x87B1),k::returned_at(0x87B7),k::conditional_at(0x87B8)}),[](auto& z) { return tone_voice_active(z); });
    k::add_segment(registry,{12,0x87BA},k::segment(tone,{k::conditional_at(0x87BF)}),[](auto& z) { return tone_envelope_marker(z); });
    k::add_segment(registry,{12,0x87C2},k::segment(tone,{k::conditional_at(0x87C6)}),[](auto& z) { return tone_envelope_index(z); });
    k::add_segment(registry,{12,0x87C9},k::segment(tone,{k::core_final_at(0x87CC)},{.scratch_stack=2}),[](auto& z) { return tone_envelope_lookup(z); });
    k::add_segment(registry,{12,0x87D2},k::segment(tone,{k::core_final_at(0x87D9)}),[](auto& z) { return tone_envelope_output(z); });
    k::add_segment(registry,{12,0x87DC},k::segment(tone,{k::conditional_at(0x87DD)}),[](auto& z) { return tone_note_sign(z); });
    k::add_segment(registry,{12,0x87E0},k::segment(tone,{k::returned_at(0x87E4),k::conditional_at(0x87E9)}),[](auto& z) { return tone_pitch_index(z); });
    k::add_segment(registry,{12,0x87ED},k::segment(tone,{k::conditional_at(0x87F7)}),[](auto& z) { return tone_pitch_from_note(z); });
    k::add_segment(registry,{12,0x87F1},k::segment(tone,{k::conditional_at(0x87F7)}),[](auto& z) { return tone_pitch_value(z); });
    k::add_segment(registry,{12,0x87FA},k::segment(tone,{k::core_final_at(0x87FE)},{.scratch_stack=2}),[](auto& z) { return tone_apply_pitch_lookup(z); });
    k::add_segment(registry,{12,0x8804},k::segment(tone,{k::returned_at(0x8808),k::conditional_at(0x880E)}),[](auto& z) { return tone_choose_channel(z); });
    const RoutineEntry voice{12,0x86D0};
    add_voice_tick(registry,voice,k::segment(voice,{k::core_final_at(0x86D9)},{.scratch_stack=2}),[](auto& z) { return tick_noise_voice(z); });
    k::add_segment(registry,{12,0x86DC},k::segment(voice,{k::returned_at(0x86E0),k::returned_at(0x86E5),k::returned_at(0x86EA),k::core_final_at(0x86EE)},{.scratch_stack=2}),[](auto& z) { return noise_voice_envelope(z); });
    k::add_segment(registry,{12,0x86F4},k::segment(voice,{k::returned_at(0x86F8)}),[](auto& z) { return output_noise_level(z); });
    k::add_segment(registry,{12,0x86F9},k::segment(voice,{k::core_final_at(0x86FF)}),[](auto& z) { return read_score_position(z); });
    k::add_segment(registry,{12,0x8700},k::segment(voice,{k::conditional_at(0x8703)}),[](auto& z) { return classify_score_byte(z); });
    k::add_segment(registry,{12,0x8706},k::segment(voice,{k::conditional_at(0x8708)}),[](auto& z) { return classify_note_byte(z); });
    k::add_segment(registry,{12,0x870E},k::segment(voice,{k::conditional_at(0x8712)}),[](auto& z) { return classify_following_byte(z); });
    add_voice_tick(registry,{12,0x8715},k::segment(voice,{k::core_final_at(0x871C)}),[](auto& z) { return keep_current_note(z); });
    k::add_segment(registry,{12,0x871F},k::segment(voice,{k::core_final_at(0x8720)}),[](auto& z) { return keep_note_from_previous_byte(z); });
    k::add_segment(registry,{12,0x8723},k::segment(voice,{k::core_final_at(0x8726)}),[](auto& z) { return dispatch_noise_command(z); });
    k::add_segment(registry,{12,0x8729},k::segment(voice,{k::core_final_at(0x872A)}),[](auto& z) { return advance_noise_command(z); });
    // Joining instructions the original runs between the segments above: the
    // calls to the envelope reader (8827), the noise pattern selector (872D)
    // and the pitch envelope (843C), the register-set and level exchanges
    // around the noise pattern, and the reads and return of the tone voice.
    const k::Permissions call{.scratch_stack=2};
    add_joining(registry,0x86F1,voice,k::core_final_at(0x86F1),call,[](auto& z) { return z.final_call(0x86F1); });
    add_joining(registry,0x86FF,voice,k::core_final_at(0x86FF),{},[](auto& z) { z.load_a_from_pair(0x86FF,z.de()); return k::fall_through(0x86FF,0x8700); });
    add_joining(registry,0x870B,voice,k::core_final_at(0x870B),call,[](auto& z) { return z.final_call(0x870B); });
    add_joining(registry,0x874A,noise_pattern,k::core_final_at(0x874A),{},[](auto& z) { z.exchange_af(0x874A); return k::fall_through(0x874A,0x874B); });
    add_joining(registry,0x874F,noise_pattern,k::core_final_at(0x874F),{},[](auto& z) { return z.final_relative(0x874F); });
    add_joining(registry,0x8759,noise_pattern,k::core_final_at(0x8759),{},[](auto& z) { z.exchange_af(0x8759); return k::fall_through(0x8759,0x875A); });
    add_joining(registry,0x875F,noise_pattern,k::core_final_at(0x875F),{},[](auto& z) { z.exchange_af(0x875F); return k::fall_through(0x875F,0x8760); });
    add_joining(registry,0x878D,tone,k::core_final_at(0x878D),{},[](auto& z) { z.exchange_alternates(0x878D); return k::fall_through(0x878D,0x878E); });
    add_joining(registry,0x87CF,tone,k::core_final_at(0x87CF),call,[](auto& z) { return z.final_call(0x87CF); });
    add_joining(registry,0x87D9,tone,k::core_final_at(0x87D9),{},[](auto& z) { z.load_a_direct(0x87D9); return k::fall_through(0x87D9,0x87DC); });
    add_joining(registry,0x87EC,tone,k::returned_at(0x87EC),{},[](auto& z) { return z.final_return(0x87EC); });
    add_joining(registry,0x8801,tone,k::core_final_at(0x8801),call,[](auto& z) { return z.final_call(0x8801); });
}
}
