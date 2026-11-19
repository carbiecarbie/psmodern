// SPDX-License-Identifier: GPL-3.0-or-later
// Letter interaction: dungeon transfer, party check and item handoffs.
#include "game/interaction_letter.h"
#include "game/routines.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit return_to_dungeon(Z& z) {
    z.pop(0x4C7F,z.r.h,z.r.l);
    z.load_hl(0x4C80);
    z.store_hl_direct(0x4C83);
    z.xor_a(0x4C86);
    z.store_a_direct(0x4C87);
    z.load_hl(0x4C8A);
    z.store_to_hl(0x4C8D);
    return z.final_call(0x4C8F);
}
template<class Z> k::Exit finish_dungeon_transfer(Z& z) {
    z.load_immediate(0x4C92,z.r.a);
    return z.final_jump(0x4C94);
}
template<class Z> k::Exit set_scene_actor(Z& z) {
    z.load_immediate(0x4C97,z.r.a);
    return z.final_call(0x4C99);
}
template<class Z> k::Exit test_party_size(Z& z) {
    z.load_a_direct(0x4C9F);
    z.compare_immediate(0x4CA2);
    return z.final_relative_if(0x4CA4,k::Condition::nc);
}
template<class Z> k::Exit find_letter(Z& z) {
    z.load_immediate(0x4CA6,z.r.a);
    return z.final_call(0x4CA8);
}
template<class Z> k::Exit give_letter(Z& z) {
    z.push(0x4CBF,z.r.b,z.r.c);
    z.load_immediate(0x4CC0,z.r.a);
    z.store_a_direct(0x4CC2);
    return z.final_call(0x4CC5);
}
template<class Z> k::Exit verify_letter(Z& z) {
    z.pop(0x4CC8,z.r.b,z.r.c);
    z.load_immediate(0x4CC9,z.r.a);
    return z.final_call(0x4CCB);
}
template<class Z> k::Exit set_letter_scene(Z& z) {
    z.load_immediate(0x4CDB,z.r.a);
    z.store_a_direct(0x4CDD);
    return z.final_call(0x4CE0);
}
template<class Z> k::Exit load_letter_palette(Z& z) {
    z.load_hl(0x4CED);
    z.load_de(0x4CF0);
    z.load_bc(0x4CF3);
    z.block_copy(0x4CF6);
    return z.final_call(0x4CF8);
}
template<class Z> k::Exit wait_for_scene_frame(Z& z) {
    z.load_immediate(0x4CE3,z.r.a);
    z.store_a_direct(0x4CE5);
    z.load_immediate(0x4CE8,z.r.a);
    return z.final_call(0x4CEA);
}
template<class Z> k::Exit prepare_scene_call(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(at+2);
    return z.final_call(at+5);
}
template<class Z> k::Exit call_scene_helper(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    return z.final_call(at+2);
}
template<class Z> k::Exit preserve_letter_party(Z& z) {
    z.load_a_direct(0x4D14);
    z.push(0x4D17,z.r.a,z.r.f);
    z.load_a_direct(0x4D18);
    z.push(0x4D1B,z.r.a,z.r.f);
    z.load_a_direct(0x4D1C);
    z.push(0x4D1F,z.r.a,z.r.f);
    return z.final_call(0x4D20);
}
template<class Z> k::Exit restore_letter_party(Z& z) {
    z.pop(0x4D23,z.r.a,z.r.f);
    z.store_a_direct(0x4D24);
    z.pop(0x4D27,z.r.a,z.r.f);
    z.store_a_direct(0x4D28);
    z.pop(0x4D2B,z.r.a,z.r.f);
    z.store_a_direct(0x4D2C);
    return z.final_call(0x4D2F);
}
// Joining stretches (milestone 14), each the instruction that runs when a
// call of the letter scene returns: a call into the next helper, the branch
// on the party size, and the return that leaves when the letter was not
// found (the stretch goes on to the dialogue that follows).
template<class Z> k::Exit after_scene_call(Z& z,std::uint16_t at) { return z.final_call(at); }
template<class Z> k::Exit after_party_size(Z& z) { return z.final_relative_if(0x4CAB,k::Condition::nz); }
template<class Z> k::Exit leave_unless_verified(Z& z) {
    if (auto exit=z.final_return_if(0x4CCE,k::Condition::nz)) return *exit;
    z.load_hl(0x4CCF);
    return z.final_call(0x4CD2);
}
template<class Z> k::Exit prepare_frame_wait(Z& z) {
    z.load_immediate(0x4D35,z.r.a);
    z.store_a_direct(0x4D37);
    z.load_immediate(0x4D3A,z.r.a);
    return z.final_call(0x4D3C);
}
}
void register_interaction_letter(RoutineRegistry& registry) {
    register_block_transfer(registry,interaction_letter_copy);
    using k::core_final_at; using k::segment; const RoutineEntry unit{1,0x4C70};
    for (std::uint16_t at:{0x4D04,0x4D0C,0x4D4B})
        k::add_segment(registry,{1,at},segment(unit,{core_final_at(static_cast<std::uint16_t>(at+5))},{.scratch_stack=2}),[at](auto& z) { return prepare_scene_call(z,at); });
    for (std::uint16_t at:{0x4D56,0x4D5E})
        k::add_segment(registry,{1,at},segment(unit,{core_final_at(static_cast<std::uint16_t>(at+2))},{.scratch_stack=2}),[at](auto& z) { return call_scene_helper(z,at); });
    k::add_segment(registry,{1,0x4D14},segment(unit,{core_final_at(0x4D20,-6)},{.scratch_stack=8}),[](auto& z) { return preserve_letter_party(z); });
    k::add_segment(registry,{1,0x4D23},segment(unit,{core_final_at(0x4D2F,6)},{.inherited_stack=6}),[](auto& z) { return restore_letter_party(z); });
    k::add_segment(registry,{1,0x4D35},segment(unit,{core_final_at(0x4D3C)},{.scratch_stack=2}),[](auto& z) { return prepare_frame_wait(z); });
    k::add_segment(registry,{1,0x4C7F},segment(unit,{core_final_at(0x4C8F,2)},{.inherited_stack=2}),[](auto& z) { return return_to_dungeon(z); });
    k::add_segment(registry,{1,0x4C92},segment(unit,{core_final_at(0x4C94)}),[](auto& z) { return finish_dungeon_transfer(z); });
    k::add_segment(registry,{1,0x4C97},segment(unit,{core_final_at(0x4C99)},{.scratch_stack=2}),[](auto& z) { return set_scene_actor(z); });
    k::add_segment(registry,{1,0x4C9F},segment(unit,{k::conditional_at(0x4CA4)}),[](auto& z) { return test_party_size(z); });
    k::add_segment(registry,{1,0x4CA6},segment(unit,{core_final_at(0x4CA8)},{.scratch_stack=2}),[](auto& z) { return find_letter(z); });
    k::add_segment(registry,{1,0x4CBF},segment(unit,{core_final_at(0x4CC5,-2)},{.scratch_stack=4}),[](auto& z) { return give_letter(z); });
    k::add_segment(registry,{1,0x4CC8},segment(unit,{core_final_at(0x4CCB,2)},{.inherited_stack=2}),[](auto& z) { return verify_letter(z); });
    k::add_segment(registry,{1,0x4CDB},segment(unit,{core_final_at(0x4CE0)},{.scratch_stack=2}),[](auto& z) { return set_letter_scene(z); });
    for (std::uint16_t at:{0x4C9C,0x4D32,0x4D3F,0x4D53,0x4D5B,0x4D63}) {
        auto spec=segment(unit,{core_final_at(at)},{.scratch_stack=2});
        spec.single_instruction=true;
        k::add_segment(registry,{1,at},std::move(spec),[at](auto& z) { return after_scene_call(z,at); });
    }
    {
        auto spec=segment(unit,{k::conditional_at(0x4CAB)});
        spec.single_instruction=true;
        k::add_segment(registry,{1,0x4CAB},std::move(spec),[](auto& z) { return after_party_size(z); });
    }
    k::add_segment(registry,{1,0x4CCE},segment(unit,{k::returned_at(0x4CCE),core_final_at(0x4CD2)},{.scratch_stack=2}),[](auto& z) { return leave_unless_verified(z); });
    k::add_segment(registry,{1,0x4CED},segment(unit,{core_final_at(0x4CF8)},{.scratch_stack=2}),[](auto& z) { return load_letter_palette(z); });
    k::add_segment(registry,{1,0x4CE3},segment(unit,{core_final_at(0x4CEA)},{.scratch_stack=2}),[](auto& z) { return wait_for_scene_frame(z); });
}
}
