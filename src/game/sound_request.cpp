// SPDX-License-Identifier: GPL-3.0-or-later
// Resolve a sound-effect request and prepare the shared channels it uses.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
constexpr RoutineEntry unit{12,0x82EF};

// After the reset helpers, restore the saved track selector and select the
// voice group. Both the conditional transfer and following CALL end a segment.
template<class Z> k::Exit choose_track_group(Z& z) {
    z.pop(0x836F,z.r.a,z.r.f);
    z.load_de(0x8370);
    z.or_a(0x8373);
    return z.final_jump_if(0x8374,k::Condition::m);
}
// The saved selector identifies a pointer in the track table. The original
// CALL to the table reader follows after popping the selector.
template<class Z> k::Exit load_track_pointer(Z& z) {
    z.pop(0x8382,z.r.a,z.r.f);
    return z.final_call(0x8383);
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
    z.load_de(0x83A7);
    return k::fall_through(0x83A7,0x83AA);
}
// A request selects a pointer from the ROM table; the segment ends before its
// CALL so the shared pointer reader keeps the original stack behavior.
template<class Z> k::Exit select_effect(Z& z) {
    z.subtract_immediate(0x8389);
    z.load_hl(0x838B);
    return z.final_call(0x838E);
}
template<class Z> k::Exit classify_first_effect(Z& z) {
    z.copy(0x8391,z.r.h,z.r.b);
    z.copy(0x8392,z.r.l,z.r.c);
    z.increment_hl(0x8393);
    z.increment_hl(0x8394);
    z.load_from_hl(0x8395,z.r.a);
    z.compare_immediate(0x8396);
    return z.final_relative_if(0x8398,k::Condition::z);
}
template<class Z> k::Exit classify_second_effect(Z& z) {
    z.compare_immediate(0x839A);
    return z.final_relative_if(0x839C,k::Condition::z);
}
template<class Z> k::Exit classify_third_effect(Z& z) {
    z.compare_immediate(0x839E);
    return z.final_relative_if(0x83A0,k::Condition::z);
}
// Save the table pointer while initialization runs as an ordinary call.
template<class Z> k::Exit initialize_effect_driver(Z& z) {
    z.push(0x83A2,z.r.b,z.r.c);
    return z.final_call(0x83A3);
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
    z.load_de(0x83B9);
    return k::fall_through(0x83B9,0x83BC);
}
template<class Z> k::Exit inspect_shared_effect(Z& z) {
    z.load_de(0x83BE);
    z.add_to_hl(0x83C1,z.de());
    z.load_from_hl(0x83C2,z.r.a);
    z.compare_immediate(0x83C3);
    return z.final_relative_if(0x83C5,k::Condition::nz);
}
// Set the shared-channel markers and let the final immediate load run in
// its own segment before its next chip write.
template<class Z> k::Exit mark_noise_effect(Z& z) {
    z.load_immediate(0x83C7,z.r.a);
    z.output(0x83C9);
    z.load_hl(0x83CB);
    z.set_bit_at_hl(0x83CE,2);
    z.load_hl(0x83D0);
    z.set_bit_at_hl(0x83D3,2);
    z.load_immediate(0x83D5,z.r.a);
    return k::fall_through(0x83D5,0x83D7);
}
template<class Z> k::Exit mark_shared_effect(Z& z) {
    z.load_de(0x83D9);
    z.load_hl(0x83DC);
    z.set_bit_at_hl(0x83DF,2);
    z.load_hl(0x83E1);
    return k::fall_through(0x83E1,0x83E4);
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
    z.load_hl(0x83EF);
    return k::fall_through(0x83EF,0x83F2);
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
    // Admitted only while HL points at the voice record at C18E, the pointer
    // this entry was verified with. The guard reads registers only, so it
    // holds at entry and after any delivery (handlers restore registers).
    k::add_segment(registry,{12,0x83E4},k::segment(unit,{k::core_final_at(0x83EF)}),
        k::Guard{"HL points at the shared voice record C18E",[](const Machine& machine) { return machine.cpu().hl.uint16_value==0xc18e; }},
        [](auto& z) { return set_last_shared_marker(z); });
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
    // Joining instructions of the request handler. After the shared noise
    // effect init returns, the handler calls the driver reset (8377); the
    // shared-track path jumps to the voice setup (8386, 83AA); the noise
    // effect loads its record pointer (83B9) and moves on to the shared
    // output (83BC).
    {
        auto call_init=k::segment(unit,{k::core_final_at(0x8377)},{.scratch_stack=2});
        call_init.single_instruction=true;
        k::add_segment(registry,{12,0x8377},std::move(call_init),[](auto& z) { return z.final_call(0x8377); });
        auto setup_jump=k::segment(unit,{k::core_final_at(0x8386)});
        setup_jump.single_instruction=true;
        k::add_segment(registry,{12,0x8386},std::move(setup_jump),[](auto& z) { return z.final_jump(0x8386); });
        auto setup_relative=k::segment(unit,{k::core_final_at(0x83AA)});
        setup_relative.single_instruction=true;
        k::add_segment(registry,{12,0x83AA},std::move(setup_relative),[](auto& z) { return z.final_relative(0x83AA); });
        auto load_pointer=k::segment(unit,{k::core_final_at(0x83B9)});
        load_pointer.single_instruction=true;
        k::add_segment(registry,{12,0x83B9},std::move(load_pointer),[](auto& z) { z.load_de(0x83B9); return k::fall_through(0x83B9,0x83BC); });
        auto to_shared=k::segment(unit,{k::core_final_at(0x83BC)});
        to_shared.single_instruction=true;
        k::add_segment(registry,{12,0x83BC},std::move(to_shared),[](auto& z) { return z.final_relative(0x83BC); });
    }
}
}
