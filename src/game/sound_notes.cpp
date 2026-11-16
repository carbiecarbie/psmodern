// SPDX-License-Identifier: GPL-3.0-or-later
// Decode a voice's score note and initialize its playback state.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
constexpr auto ix=k::Index::ix;
constexpr RoutineEntry unit{12,0x846D};

// The voice record keeps the current score pointer; the core owns the first
// command/note branch so the command dispatcher sees its original entry.
template<class Z> k::Exit read_note_position(Z& z) {
    z.load_from_indexed(0x846D,ix,z.r.e);
    z.load_from_indexed(0x8470,ix,z.r.d);
    return k::continues(0x8473,7);
}
template<class Z> k::Exit read_next_note(Z& z) {
    z.load_a_from_pair(0x8473,z.de());
    return k::continues(0x8474,6);
}
template<class Z> k::Exit advance_score_position(Z& z) {
    z.increment_pair(0x8474,z.r.d,z.r.e);
    return k::continues(0x8475,7);
}
template<class Z> k::Exit classify_score_byte(Z& z) {
    z.compare_immediate(0x8475);
    return k::jump_if_exit(0x8477);
}
template<class Z> k::Exit note_has_explicit_pitch(Z& z) {
    z.test_bit_indexed(0x847A,ix,3);
    return k::continues(0x847E,k::jump_cycles);
}
template<class Z> k::Exit note_uses_period_table(Z& z) {
    z.compare_immediate(0x8481);
    return k::continues(0x8483,k::jump_cycles);
}
// The branch has a zero displacement, but it still has conditional timing.
// The following CALL to the period table remains a native boundary.
template<class Z> k::Exit enter_period_lookup(Z& z) {
    z.relative_if(0x8486,!z.zero());
    return k::continues(0x8488,k::call_cycles);
}
// Once the period lookup returns, store both bytes into the voice and test
// whether the score supplies a pitch-drift parameter.
template<class Z> k::Exit save_note_period(Z& z) {
    z.load_from_hl(0x848B,z.r.a);
    z.store_to_indexed(0x848C,ix,z.r.a);
    z.increment_hl(0x848F);
    z.load_from_hl(0x8490,z.r.a);
    z.store_to_indexed(0x8491,ix,z.r.a);
    z.test_bit_indexed(0x8494,ix,5);
    return k::continues(0x8498,k::jump_cycles);
}
template<class Z> k::Exit note_has_drift(Z& z) {
    z.test_bit_indexed(0x8494,ix,5);
    return k::continues(0x8498,k::jump_cycles);
}
// Clear per-note progress and preserve the score pointer for the next tick.
template<class Z> k::Exit clear_note_progress(Z& z) {
    z.store_to_indexed(0x84D0,ix,z.r.a);
    z.store_to_indexed(0x84D3,ix,z.r.a);
    z.store_to_indexed(0x84D6,ix,z.r.a);
    z.store_to_indexed(0x84D9,ix,z.r.a);
    z.store_to_indexed(0x84DC,ix,z.r.a);
    z.store_to_indexed(0x84DF,ix,z.r.e);
    z.store_to_indexed(0x84E2,ix,z.r.d);
    z.load_immediate(0x84E5,z.r.a);
    return k::returns(0x84E7,k::return_cycles);
}
template<class Z> k::Exit reset_note_progress(Z& z) {
    z.xor_a(0x84CF);
    return clear_note_progress(z);
}
template<class Z> k::Exit save_note_duration(Z& z) {
    z.store_to_indexed(0x84C9,ix,z.r.a);
    z.store_to_indexed(0x84CC,ix,z.r.a);
    return k::continues(0x84CF,4);
}
// Select the multiplier; the original branch and loop remain interruptible.
template<class Z> k::Exit calculate_note_duration(Z& z) {
    z.load_from_indexed(0x84BF,ix,z.r.b);
    z.decrement(0x84C2,z.r.b);
    return k::relative_if_exit(0x84C3,z.zero());
}
template<class Z> k::Exit add_note_duration(Z& z) {
    z.add_register(0x84C6,z.r.c);
    return k::decrement_jump_exit(z,0x84C7);
}
// An explicit pitch is shifted by the voice's signed offset before it is
// stored; the original jump then joins the shared note setup.
template<class Z> k::Exit set_explicit_pitch(Z& z) {
    z.copy(0x84E8,z.r.h,z.r.a);
    z.load_a_from_pair(0x84E9,z.de());
    z.increment_pair(0x84EA,z.r.d,z.r.e);
    z.copy(0x84EB,z.r.l,z.r.a);
    z.load_from_indexed(0x84EC,ix,z.r.a);
    z.or_a(0x84EF);
    if (!z.relative_if(0x84F0,z.zero())) {
        if (!z.jump_if(0x84F2,!z.sign())) {
            z.add_register(0x84F5,z.r.l);
            if (!z.relative_if(0x84F6,z.carry())) {
                z.decrement(0x84F8,z.r.h);
                z.relative(0x84F9);
            }
        } else {
            z.add_register(0x84FB,z.r.l);
            if (!z.relative_if(0x84FC,!z.carry())) z.increment(0x84FE,z.r.h);
        }
        z.copy(0x84FF,z.r.l,z.r.a);
    }
    z.store_to_indexed(0x8500,ix,z.r.l);
    z.store_to_indexed(0x8503,ix,z.r.h);
    return k::continues(0x8506,k::jump_cycles);
}
// Read the drift parameters and decide whether the following byte belongs to
// the duration or to the explicit pitch-shift state.
template<class Z> k::Exit read_note_drift(Z& z) {
    z.load_a_from_pair(0x849B,z.de());
    z.increment_pair(0x849C,z.r.d,z.r.e);
    z.store_to_indexed(0x849D,ix,z.r.a);
    z.store_to_indexed(0x84A0,ix,z.r.a);
    z.test_bit_indexed(0x84A3,ix,3);
    z.load_a_from_pair(0x84A7,z.de());
    return k::relative_if_exit(0x84A8,!z.zero());
}
// The duration reader is shared after an explicit drift operand.
template<class Z> k::Exit read_duration_after_drift(Z& z) {
    z.store_to_indexed(0x84AA,ix,z.r.a);
    z.increment_pair(0x84AD,z.r.d,z.r.e);
    z.load_a_from_pair(0x84AE,z.de());
    return k::continues(0x84AF,12);
}
template<class Z> k::Exit choose_note_duration(Z& z) {
    z.load_a_from_pair(0x84B1,z.de());
    z.or_a(0x84B2);
    return k::continues(0x84B3,k::jump_cycles);
}
// A negative duration operand reuses the previous note duration.
template<class Z> k::Exit reuse_note_duration(Z& z) {
    z.load_from_indexed(0x84B6,ix,z.r.a);
    z.store_to_indexed(0x84B9,ix,z.r.a);
    return k::continues(0x84BC,12);
}
template<class Z> k::Exit advance_to_note_duration(Z& z) {
    z.increment_pair(0x84BE,z.r.d,z.r.e);
    return calculate_note_duration(z);
}
template<class Logic> void add_voice_note(RoutineRegistry& registry,RoutineEntry entry,SegmentSpec spec,Logic logic) {
    registry.add(entry,std::move(spec),[logic](const Machine& machine) {
        const auto address=machine.cpu().ix_iy[0].uint16_value;
        return address>=0xc000 && address<=0xdfe8 ? k::cost(machine,logic,k::uncertain_maximum) : k::uncertain_maximum;
    },[logic](RoutineContext& context) { k::run(context,logic); });
}
}
void register_sound_notes(RoutineRegistry& registry) {
    k::add_segment(registry,unit,k::segment(unit,{k::core_final_at(0x8473)}),[](auto& z) { return read_note_position(z); });
    k::add_segment(registry,{12,0x8473},k::segment(unit,{k::core_final_at(0x8474)}),[](auto& z) { return read_next_note(z); }
    );
    k::add_segment(registry,{12,0x8474},k::segment(unit,{k::core_final_at(0x8475)}),[](auto& z) { return advance_score_position(z); });
    k::add_segment(registry,{12,0x8475},k::segment(unit,{k::conditional_at(0x8477)}),[](auto& z) { return classify_score_byte(z); });
    k::add_segment(registry,{12,0x847A},k::segment(unit,{k::conditional_at(0x847E)}),[](auto& z) { return note_has_explicit_pitch(z); });
    k::add_segment(registry,{12,0x8481},k::segment(unit,{k::conditional_at(0x8483)}),[](auto& z) { return note_uses_period_table(z); });
    k::add_segment(registry,{12,0x8486},k::segment(unit,{k::core_final_at(0x8488)},{.scratch_stack=2}),[](auto& z) { return enter_period_lookup(z); });
    add_voice_note(registry,{12,0x848B},k::segment(unit,{k::conditional_at(0x8498)}),[](auto& z) { return save_note_period(z); });
    k::add_segment(registry,{12,0x8494},k::segment(unit,{k::conditional_at(0x8498)}),[](auto& z) { return note_has_drift(z); });
    add_voice_note(registry,{12,0x849B},k::segment(unit,{k::conditional_at(0x84A8)}),[](auto& z) { return read_note_drift(z); });
    add_voice_note(registry,{12,0x84AA},k::segment(unit,{k::core_final_at(0x84AF)}),[](auto& z) { return read_duration_after_drift(z); });
    k::add_segment(registry,{12,0x84B1},k::segment(unit,{k::conditional_at(0x84B3)}),[](auto& z) { return choose_note_duration(z); });
    add_voice_note(registry,{12,0x84B6},k::segment(unit,{k::core_final_at(0x84BC)}),[](auto& z) { return reuse_note_duration(z); });
    add_voice_note(registry,{12,0x84BE},k::segment(unit,{k::conditional_at(0x84C3)}),[](auto& z) { return advance_to_note_duration(z); });
    add_voice_note(registry,{12,0x84BF},k::segment(unit,{k::conditional_at(0x84C3)}),[](auto& z) { return calculate_note_duration(z); });
    add_voice_note(registry,{12,0x84C9},k::segment(unit,{k::core_final_at(0x84CF)}),[](auto& z) { return save_note_duration(z); });
    k::add_segment(registry,{12,0x84C5},k::segment(unit,{k::conditional_at(0x84C7)}),[](auto& z) { z.copy(0x84C5,z.r.c,z.r.a); return add_note_duration(z); });
    k::add_segment(registry,{12,0x84C6},k::segment(unit,{k::conditional_at(0x84C7)}),[](auto& z) { return add_note_duration(z); });
    add_voice_note(registry,{12,0x84D0},k::segment(unit,{k::returned_at(0x84E7)}),[](auto& z) { return clear_note_progress(z); });
    add_voice_note(registry,{12,0x84CF},k::segment(unit,{k::returned_at(0x84E7)}),[](auto& z) { return reset_note_progress(z); });
    add_voice_note(registry,{12,0x84E8},k::segment(unit,{k::core_final_at(0x8506)}),[](auto& z) { return set_explicit_pitch(z); });
}
}
