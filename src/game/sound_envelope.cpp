// SPDX-License-Identifier: GPL-3.0-or-later
// Envelope reading and its score-control markers.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
constexpr auto ix=k::Index::ix;
constexpr RoutineEntry unit{12,0x8827};

// Save the envelope base, add this voice's index, restore the base and read
// the selected byte. The first branch tells a level from a control marker.
template<class Z> k::Exit read_envelope(Z& z) {
    z.push(0x8827,z.r.h,z.r.l);
    z.load_from_indexed(0x8828,ix,z.r.c);
    z.load_immediate(0x882B,z.r.b);
    z.add_to_hl(0x882D,z.bc());
    z.copy(0x882E,z.r.c,z.r.l);
    z.copy(0x882F,z.r.b,z.r.h);
    z.pop(0x8830,z.r.h,z.r.l);
    z.load_a_from_pair(0x8831,z.bc());
    z.test_bit(0x8832,7,z.r.a);
    return z.final_relative_if(0x8834,k::Condition::z);
}
template<class Z> k::Exit envelope_end_marker(Z& z) {
    z.compare_immediate(0x8836);
    return z.final_relative_if(0x8838,k::Condition::z);
}
template<class Z> k::Exit envelope_stop_marker(Z& z) {
    z.compare_immediate(0x883A);
    return z.final_relative_if(0x883C,k::Condition::z);
}
template<class Z> k::Exit envelope_restart_marker(Z& z) {
    z.compare_immediate(0x883E);
    return z.final_relative_if(0x8840,k::Condition::z);
}
// Other high-bit values become a new envelope index, then the driver re-reads.
template<class Z> k::Exit next_envelope_byte(Z& z) {
    z.increment_pair(0x8842,z.r.b,z.r.c);
    z.load_a_from_pair(0x8843,z.bc());
    z.relative(0x8844);
    z.store_to_indexed(0x8824,ix,z.r.a);
    return read_envelope(z);
}
// Marker 82 ends this voice: discard the caller's saved word, record the
// sentinel and send the channel's quiet command to the PSG.
template<class Z> k::Exit finish_envelope(Z& z) {
    z.pop(0x8846,z.r.h,z.r.l);
    z.load_immediate(0x8847,z.r.a);
    z.store_to_indexed(0x8849,ix,z.r.a);
    z.add_from_indexed(0x884C,ix);
    z.output(0x884F);
    return z.final_return(0x8851);
}
// Marker 80 resets the index and re-reads the first byte.
template<class Z> k::Exit restart_envelope(Z& z) {
    z.xor_a(0x8852);
    z.relative(0x8853);
    z.store_to_indexed(0x8824,ix,z.r.a);
    return read_envelope(z);
}
// Marker 81 marks the envelope inactive and discards the saved caller word.
template<class Z> k::Exit stop_envelope(Z& z) {
    z.store_immediate_indexed(0x8855,ix);
    z.pop(0x8859,z.r.h,z.r.l);
    return z.final_return(0x885A);
}
// A level advances the index and is clamped to the chip's attenuation range.
template<class Z> k::Exit envelope_level(Z& z) {
    z.increment_indexed(0x885B,ix);
    z.add_from_indexed(0x885E,ix);
    z.test_bit(0x8861,4,z.r.a);
    if (auto exit=z.final_return_if(0x8863,k::Condition::z)) return *exit;
    z.load_immediate(0x8864,z.r.a);
    return z.final_return(0x8866);
}
// The pitch envelope uses its own per-voice index and returns an offset to
// add to the current period. Its markers can restart or terminate the voice.
template<class Z> k::Exit read_pitch_envelope(Z& z) {
    z.push(0x843C,z.r.h,z.r.l);
    z.load_from_indexed(0x843D,ix,z.r.c);
    z.load_immediate(0x8440,z.r.b);
    z.add_to_hl(0x8442,z.bc());
    z.copy(0x8443,z.r.c,z.r.l);
    z.copy(0x8444,z.r.b,z.r.h);
    z.pop(0x8445,z.r.h,z.r.l);
    z.load_a_from_pair(0x8446,z.bc());
    z.test_bit(0x8447,7,z.r.a);
    return z.final_jump_if(0x8449,k::Condition::z);
}
template<class Z> k::Exit pitch_skip_marker(Z& z) {
    z.compare_immediate(0x844C);
    return z.final_relative_if(0x844E,k::Condition::z);
}
template<class Z> k::Exit pitch_restart_marker(Z& z) {
    z.compare_immediate(0x8450);
    return z.final_relative_if(0x8452,k::Condition::z);
}
template<class Z> k::Exit stop_pitch_envelope(Z& z) {
    z.load_immediate(0x8454,z.r.a);
    z.store_to_indexed(0x8456,ix,z.r.a);
    z.pop(0x8459,z.r.h,z.r.l);
    return z.final_return(0x845A);
}
template<class Z> k::Exit next_pitch_envelope(Z& z) {
    z.increment_pair(0x845B,z.r.b,z.r.c);
    z.load_a_from_pair(0x845C,z.bc());
    z.relative(0x845D);
    z.store_to_indexed(0x8439,ix,z.r.a);
    return read_pitch_envelope(z);
}
template<class Z> k::Exit restart_pitch_envelope(Z& z) {
    z.xor_a(0x845F);
    z.relative(0x8460);
    z.store_to_indexed(0x8439,ix,z.r.a);
    return read_pitch_envelope(z);
}
template<class Z> k::Exit pitch_envelope_value(Z& z) {
    z.increment_indexed(0x8462,ix);
    z.copy(0x8465,z.r.l,z.r.a);
    z.load_immediate(0x8466,z.r.h);
    z.add_to_hl(0x8468,z.de());
    z.store_to_indexed(0x8469,ix,z.r.a);
    return z.final_return(0x846C);
}
// A voice write needs IX inside the voice records in work RAM (other values
// reach the mapper). The guard reads registers only: it holds at entry and
// stays valid after a delivery, since handlers restore the registers.
template<class Logic> void add_envelope(RoutineRegistry& registry,RoutineEntry entry,SegmentSpec spec,Logic logic,bool writes_voice=false) {
    if (!writes_voice) { k::add_segment(registry,entry,std::move(spec),logic); return; }
    k::add_segment(registry,entry,std::move(spec),
        k::Guard{"IX points into the voice records in work RAM",[](const Machine& machine) {
            const auto address=machine.cpu().ix_iy[0].uint16_value;
            return address>=0xc000 && address<=0xdfe8;
        }},logic);
}
}
void register_sound_envelope(RoutineRegistry& registry) {
    const RoutineEntry pitch{12,0x843C};
    add_envelope(registry,pitch,k::segment(pitch,{k::conditional_at(0x8449)},{.scratch_stack=2}),[](auto& z) { return read_pitch_envelope(z); });
    k::add_segment(registry,{12,0x844C},k::segment(pitch,{k::conditional_at(0x844E)}),[](auto& z) { return pitch_skip_marker(z); });
    k::add_segment(registry,{12,0x8450},k::segment(pitch,{k::conditional_at(0x8452)}),[](auto& z) { return pitch_restart_marker(z); });
    add_envelope(registry,{12,0x8454},k::segment(pitch,{k::returned_at(0x845A,2)},{.inherited_stack=2}),[](auto& z) { return stop_pitch_envelope(z); },true);
    add_envelope(registry,{12,0x845B},k::segment(pitch,{k::conditional_at(0x8449)},{.scratch_stack=2}),[](auto& z) { return next_pitch_envelope(z); },true);
    add_envelope(registry,{12,0x845F},k::segment(pitch,{k::conditional_at(0x8449)},{.scratch_stack=2}),[](auto& z) { return restart_pitch_envelope(z); },true);
    add_envelope(registry,{12,0x8462},k::segment(pitch,{k::returned_at(0x846C)}),[](auto& z) { return pitch_envelope_value(z); },true);
    add_envelope(registry,unit,k::segment(unit,{k::conditional_at(0x8834)},{.scratch_stack=2}),[](auto& z) { return read_envelope(z); });
    k::add_segment(registry,{12,0x8836},k::segment(unit,{k::conditional_at(0x8838)}),[](auto& z) { return envelope_end_marker(z); });
    k::add_segment(registry,{12,0x883A},k::segment(unit,{k::conditional_at(0x883C)}),[](auto& z) { return envelope_stop_marker(z); });
    k::add_segment(registry,{12,0x883E},k::segment(unit,{k::conditional_at(0x8840)}),[](auto& z) { return envelope_restart_marker(z); });
    add_envelope(registry,{12,0x8842},k::segment(unit,{k::conditional_at(0x8834)},{.scratch_stack=2}),[](auto& z) { return next_envelope_byte(z); },true);
    add_envelope(registry,{12,0x8846},k::segment(unit,{k::returned_at(0x8851,2)},{.inherited_stack=2}),[](auto& z) { return finish_envelope(z); },true);
    add_envelope(registry,{12,0x8852},k::segment(unit,{k::conditional_at(0x8834)},{.scratch_stack=2}),[](auto& z) { return restart_envelope(z); },true);
    add_envelope(registry,{12,0x8855},k::segment(unit,{k::returned_at(0x885A,2)},{.inherited_stack=2}),[](auto& z) { return stop_envelope(z); },true);
    add_envelope(registry,{12,0x885B},k::segment(unit,{k::returned_at(0x8863),k::returned_at(0x8866)}),[](auto& z) { return envelope_level(z); },true);
}
}
