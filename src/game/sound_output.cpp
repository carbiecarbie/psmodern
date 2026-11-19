// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/menu_kit.h"
#include "game/routines.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
// Select a tone-period entry after adding the voice's pitch shift.
template<class Z> k::Exit tone_table(Z& z) {
    z.subtract_immediate(0x8509);
    if (!z.relative_if(0x850B,z.zero())) z.add_from_indexed(0x850D,k::Index::ix);
    z.load_hl(0x8510);
    z.copy(0x8513,z.r.c,z.r.a);
    z.load_immediate(0x8514,z.r.b);
    z.add_to_hl(0x8516,z.bc());
    z.add_to_hl(0x8517,z.bc());
    return z.final_return(0x8518);
}
// Format a tone period into its two ordered PSG writes.
template<class Z> k::Exit output_tone(Z& z) {
    z.copy(0x8812,z.r.c,z.r.a);
    z.copy(0x8813,z.r.a,z.r.l);
    z.and_immediate(0x8814);
    z.or_register(0x8816,z.r.c);
    z.output(0x8817);
    z.copy(0x8819,z.r.a,z.r.l);
    z.and_immediate(0x881A);
    z.or_register(0x881C,z.r.h);
    z.rotate_right_circular_a(0x881D);
    z.rotate_right_circular_a(0x881E);
    z.rotate_right_circular_a(0x881F);
    z.rotate_right_circular_a(0x8820);
    z.output(0x8821);
    return z.final_return(0x8823);
}
// The noise-channel marker selects channel C0 before sharing the tone output.
template<class Z> k::Exit output_channel_c0(Z& z) {
    z.load_immediate(0x8810,z.r.a);
    return output_tone(z);
}
// Combine the voice's volume with its channel marker for one PSG write.
template<class Z> k::Exit output_level(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_from_indexed(0x8867,ix,z.r.a);
    z.and_immediate(0x886A);
    z.bus().fetch(0x886C);
    z.bus().fetch(0x886D);
    const auto d=static_cast<std::int8_t>(z.bus().read(0x886E));
    z.r.wz=static_cast<std::uint16_t>(z.r.ix+d);
    z.r.a=static_cast<std::uint8_t>(z.r.a|z.bus().read(z.r.wz));
    kit::logic_flags(z.r,false);
    z.bus().advance(19);
    z.add_immediate(0x886F);
    z.output(0x8871);
    return z.final_return(0x8873);
}
}
void register_sound_output(RoutineRegistry& registry) {
    k::add_segment(registry,{12,0x8509},k::segment({12,0x8509},{k::returned_at(0x8518)}),[](auto& z) { return tone_table(z); });
    k::add_segment(registry,{12,0x8810},k::segment({12,0x8812},{k::returned_at(0x8823)}),[](auto& z) { return output_channel_c0(z); });
    k::add_segment(registry,{12,0x8812},k::segment({12,0x8812},{k::returned_at(0x8823)}),[](auto& z) { return output_tone(z); });
    k::add_segment(registry,{12,0x8867},k::segment({12,0x8867},{k::returned_at(0x8873)}),[](auto& z) { return output_level(z); });
}
}
