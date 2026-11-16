// SPDX-License-Identifier: GPL-3.0-or-later
// Resolve a sound-effect request and prepare the shared channels it uses.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
constexpr RoutineEntry unit{12,0x82EF};

// After the reset helpers, restore the saved track selector and select the
// voice group. Both the conditional transfer and following CALL remain native.
template<class Z> k::Exit choose_track_group(Z& z) {
    z.pop(0x836F,z.r.a,z.r.f);
    z.load_de(0x8370);
    z.or_a(0x8373);
    return k::continues(0x8374,k::jump_cycles);
}
// The saved selector identifies a pointer in the track table. The original
// CALL to the table reader follows after popping the selector.
template<class Z> k::Exit load_track_pointer(Z& z) {
    z.pop(0x8382,z.r.a,z.r.f);
    return k::continues(0x8383,k::call_cycles);
}
template<class Z> k::Exit choose_track_table(Z& z) {
    z.load_hl(0x837D);
    z.relative(0x8380);
    return load_track_pointer(z);
}
// Initialization has returned; restore the selected sound pointer and
// prepare the first voice group before the original jump.
template<class Z> k::Exit resume_after_effect_init(Z& z) {
    z.pop(0x83A6,z.r.b,z.r.c);
    return k::continues(0x83A7,10);
}
// A request selects a pointer from the ROM table; the core executes its
// CALL so the shared pointer reader keeps the original stack behavior.
template<class Z> k::Exit select_effect(Z& z) {
    z.subtract_immediate(0x8389);
    z.load_hl(0x838B);
    return k::continues(0x838E,k::call_cycles);
}
template<class Z> k::Exit classify_first_effect(Z& z) {
    z.copy(0x8391,z.r.h,z.r.b);
    z.copy(0x8392,z.r.l,z.r.c);
    z.increment_hl(0x8393);
    z.increment_hl(0x8394);
    z.load_from_hl(0x8395,z.r.a);
    z.compare_immediate(0x8396);
    return k::relative_if_exit(0x8398,z.zero());
}
template<class Z> k::Exit classify_second_effect(Z& z) {
    z.compare_immediate(0x839A);
    return k::relative_if_exit(0x839C,z.zero());
}
template<class Z> k::Exit classify_third_effect(Z& z) {
    z.compare_immediate(0x839E);
    return k::relative_if_exit(0x83A0,z.zero());
}
// Save the table pointer while initialization runs as an ordinary call.
template<class Z> k::Exit initialize_effect_driver(Z& z) {
    z.push(0x83A2,z.r.b,z.r.c);
    return k::continues(0x83A3,k::call_cycles);
}
// A shared noise effect temporarily quiets a tone and configures the noise
// channel; its PSG writes keep the original order and cycle positions.
template<class Z> k::Exit prepare_noise_effect(Z& z) {
    z.load_immediate(0x83AC,z.r.a);
    z.output(0x83AE);
    z.load_hl(0x83B0);
    z.set_bit_at_hl(0x83B3,2);
    z.load_immediate(0x83B5,z.r.a);
    z.output(0x83B7);
    return k::continues(0x83B9,10);
}
template<class Z> k::Exit inspect_shared_effect(Z& z) {
    z.load_de(0x83BE);
    z.add_to_hl(0x83C1,z.de());
    z.load_from_hl(0x83C2,z.r.a);
    z.compare_immediate(0x83C3);
    return k::relative_if_exit(0x83C5,!z.zero());
}
// Set the shared-channel markers and let the final immediate load run in
// the core before its next chip write.
template<class Z> k::Exit mark_noise_effect(Z& z) {
    z.load_immediate(0x83C7,z.r.a);
    z.output(0x83C9);
    z.load_hl(0x83CB);
    z.set_bit_at_hl(0x83CE,2);
    z.load_hl(0x83D0);
    z.set_bit_at_hl(0x83D3,2);
    return k::continues(0x83D5,7);
}
template<class Z> k::Exit mark_shared_effect(Z& z) {
    z.load_de(0x83D9);
    z.load_hl(0x83DC);
    z.set_bit_at_hl(0x83DF,2);
    return k::continues(0x83E1,10);
}
template<class Z> k::Exit finish_noise_command(Z& z) {
    z.output(0x83D7);
    return mark_shared_effect(z);
}
template<class Z> k::Exit prepare_shared_output(Z& z) {
    z.load_immediate(0x83E6,z.r.a);
    z.output(0x83E8);
    z.load_hl(0x83EA);
    z.set_bit_at_hl(0x83ED,2);
    return k::continues(0x83EF,10);
}
template<class Z> k::Exit set_last_shared_marker(Z& z) {
    z.set_bit_at_hl(0x83E4,2);
    return prepare_shared_output(z);
}

}
void register_sound_request(RoutineRegistry& registry) {
    k::add_segment(registry,{12,0x836F},k::segment(unit,{k::conditional_at(0x8374,2)},{.inherited_stack=2}),[](auto& z) { return choose_track_group(z); });
    k::add_segment(registry,{12,0x837D},k::segment(unit,{k::core_final_at(0x8383,2)},{.inherited_stack=2}),[](auto& z) { return choose_track_table(z); });
    k::add_segment(registry,{12,0x8382},k::segment(unit,{k::core_final_at(0x8383,2)},{.inherited_stack=2}),[](auto& z) { return load_track_pointer(z); });
    k::add_segment(registry,{12,0x83A6},k::segment(unit,{k::core_final_at(0x83A7,2)},{.inherited_stack=2}),[](auto& z) { return resume_after_effect_init(z); });
    k::add_segment(registry,{12,0x83D7},k::segment(unit,{k::core_final_at(0x83E1)}),[](auto& z) { return finish_noise_command(z); });
    const auto last_marker=[](auto& z) { return set_last_shared_marker(z); };
    // Admitted only while HL points at the voice record at C18E, the pointer
    // this entry was verified with; any other HL runs the original
    // instructions, so the C++ body never writes through an unverified address.
    registry.add({12,0x83E4},k::segment(unit,{k::core_final_at(0x83EF)}),
        [last_marker](const Machine& machine) { return machine.cpu().hl.uint16_value==0xc18e ? k::cost(machine,last_marker,k::uncertain_maximum) : k::uncertain_maximum; },
        [last_marker](RoutineContext& context) { k::run(context,last_marker); });
    k::add_segment(registry,{12,0x8389},k::segment(unit,{k::core_final_at(0x838E)},{.scratch_stack=2}),[](auto& z) { return select_effect(z); });
    k::add_segment(registry,{12,0x8391},k::segment(unit,{k::conditional_at(0x8398)}),[](auto& z) { return classify_first_effect(z); });
    k::add_segment(registry,{12,0x839A},k::segment(unit,{k::conditional_at(0x839C)}),[](auto& z) { return classify_second_effect(z); });
    k::add_segment(registry,{12,0x839E},k::segment(unit,{k::conditional_at(0x83A0)}),[](auto& z) { return classify_third_effect(z); });
    k::add_segment(registry,{12,0x83A2},k::segment(unit,{k::core_final_at(0x83A3,-2)},{.scratch_stack=4}),[](auto& z) { return initialize_effect_driver(z); });
    k::add_segment(registry,{12,0x83AC},k::segment(unit,{k::core_final_at(0x83B9)}),[](auto& z) { return prepare_noise_effect(z); });
    k::add_segment(registry,{12,0x83BE},k::segment(unit,{k::conditional_at(0x83C5)}),[](auto& z) { return inspect_shared_effect(z); });
    k::add_segment(registry,{12,0x83C7},k::segment(unit,{k::core_final_at(0x83D5)}),[](auto& z) { return mark_noise_effect(z); });
    k::add_segment(registry,{12,0x83D9},k::segment(unit,{k::core_final_at(0x83E1)}),[](auto& z) { return mark_shared_effect(z); });
    k::add_segment(registry,{12,0x83E6},k::segment(unit,{k::core_final_at(0x83EF)}),[](auto& z) { return prepare_shared_output(z); });
}
}
