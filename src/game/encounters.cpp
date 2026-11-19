// SPDX-License-Identifier: GPL-3.0-or-later
// The random-encounter checks, rewritten as segments (milestone 4, part C):
// the map check (01:5F63) with its shared monster-group choice (01:5FD8) and
// failure ending, and the dungeon game-mode handler (00:0F3C). Every segment
// renders the original instructions from its entry for any machine state, its
// final instruction included (milestone 14): a return, or a call into original
// code (which includes the random-number routine, a segment of its own).
//
// Each segment is written once, as a sequence of instruction renderings of the
// shared menu kit; the kit runs it on the live bus and, to learn the cost of
// the path this machine will take (the segment's bound), on an estimating bus.
#include "game/encounters.h"
#include "game/menu_kit.h"
#include <algorithm>
#include <utility>

namespace psmodern::game {
namespace {
namespace k=menu_kit;
// ---- Map check -------------------------------------------------------------

// 01:5FF9-5FFD, the failure ending: no battle (A = 0) and the pending flag
// cleared.
template<class Z> k::Exit map_failure(Z& z) {
    z.xor_a(0x5FF9);
    z.store_a_direct(0x5FFA);
    return z.final_return(0x5FFD);
}

// 01:5F63-5FAD: select the region record from the map selector and the
// position, reduce the threshold when the vehicle modifier is set, then call
// the random-number routine.
template<class Z> k::Exit map_check_start(Z& z) {
    auto& r=z.r;
    z.load_hl(0x5F63);
    z.store_to_hl(0x5F66);                 // slot 2 bank register
    z.load_a_direct(0x5F68);               // selector
    z.compare_immediate(0x5F6B);
    if (z.jump_if(0x5F6D,k::Condition::nc)) return map_failure(z);
    z.copy(0x5F70,r.h,r.a);
    z.load_immediate(0x5F71,r.l);
    z.shift_right_logical(0x5F73,r.h);
    z.rotate_right_through_carry(0x5F75,r.l);
    z.load_de(0x5F77);
    z.add_to_hl(0x5F7A,z.de());
    z.load_pair_direct(0x5F7B,z.r.d,z.r.e);
    z.copy(0x5F7F,r.a,r.e);
    z.add_immediate(0x5F80);
    if (!z.relative_if(0x5F82,k::Condition::c)) {
        z.compare_immediate(0x5F84);
        z.complement_carry(0x5F86);
    }
    z.load_immediate(0x5F87,r.a);
    z.add_with_carry(0x5F89,r.d);
    z.and_immediate(0x5F8A);
    z.add_register(0x5F8C,r.a); z.add_register(0x5F8D,r.a); z.add_register(0x5F8E,r.a);
    z.copy(0x5F8F,r.c,r.a);
    z.load_pair_direct(0x5F90,z.r.d,z.r.e);
    z.copy(0x5F94,r.a,r.e);
    z.add_immediate(0x5F95);
    z.load_immediate(0x5F97,r.a);
    z.add_with_carry(0x5F99,r.d);
    z.and_immediate(0x5F9A);
    z.add_register(0x5F9C,r.c);
    z.add_register(0x5F9D,r.a);
    z.load_immediate(0x5F9E,r.d);
    z.copy(0x5FA0,r.e,r.a);
    z.add_to_hl(0x5FA1,z.de());
    z.load_from_hl(0x5FA2,r.b);            // the region's threshold
    z.load_a_direct(0x5FA3);               // vehicle modifier
    z.or_a(0x5FA6);
    if (!z.relative_if(0x5FA7,k::Condition::z)) {
        z.shift_right_logical(0x5FA9,r.b);
        z.shift_right_logical(0x5FAB,r.b);
    }
    return z.final_call(0x5FAD);           // call the random-number routine
}

// 01:5FB1-5FD7 after the comparison, then the group choice below.
template<class Z> k::Exit group_choice(Z& z);
template<class Z> k::Exit map_pick_pool(Z& z) {
    auto& r=z.r;
    if (z.jump_if(0x5FB1,k::Condition::nc)) return map_failure(z);
    z.increment_hl(0x5FB4);
    z.load_immediate(0x5FB5,r.b);
    z.load_from_hl(0x5FB7,r.c);
    z.load_a_direct(0x5FB8);
    z.copy(0x5FBB,r.l,r.a);
    z.load_immediate(0x5FBC,r.h);
    z.load_de(0x5FBE);
    z.add_to_hl(0x5FC1,z.de());
    z.load_a_direct(0x5FC2);
    z.or_a(0x5FC5);
    if (!z.relative_if(0x5FC6,k::Condition::z)) z.load_immediate(0x5FC8,r.a);
    z.add_from_hl(0x5FCA);
    z.copy(0x5FCB,r.l,r.a);
    z.load_immediate(0x5FCC,r.h);
    for (std::uint16_t at=0x5FCE;at<=0x5FD1;++at) z.add_to_hl(at,z.hl());
    z.load_de(0x5FD2);
    z.add_to_hl(0x5FD5,z.de());
    z.add_to_hl(0x5FD6,z.bc());
    z.load_from_hl(0x5FD7,r.a);
    return group_choice(z);
}

// ---- Monster-group choice 01:5FD8-5FE9 (also called by the dungeon) --------

template<class Z> k::Exit group_choice(Z& z) {
    auto& r=z.r;
    z.or_a(0x5FD8);
    if (auto exit=z.final_return_if(0x5FD9,k::Condition::z)) return *exit; // no pool
    z.load_hl(0x5FDA);
    z.store_to_hl(0x5FDD);                 // slot 2 bank register
    z.copy(0x5FDF,r.l,r.a);
    z.load_immediate(0x5FE0,r.h);
    for (std::uint16_t at=0x5FE2;at<=0x5FE4;++at) z.add_to_hl(at,z.hl());
    z.load_de(0x5FE5);
    z.add_to_hl(0x5FE8,z.de());
    return z.final_call(0x5FE9);           // call the random-number routine
}
// 01:5FEC-5FF8: the draw picks one of eight pool entries.
template<class Z> k::Exit group_pick(Z& z) {
    auto& r=z.r;
    z.and_immediate(0x5FEC);
    z.copy(0x5FEE,r.e,r.a);
    z.load_immediate(0x5FEF,r.d);
    z.add_to_hl(0x5FF1,z.de());
    z.load_from_hl(0x5FF2,r.a);
    z.store_a_direct(0x5FF3);
    z.load_immediate(0x5FF6,r.a);
    return z.final_return(0x5FF8);
}

// ---- Dungeon game-mode handler ---------------------------------------------

template<class Z> k::Exit dungeon_pause(Z& z) {   // 00:0F3C
    z.load_a_direct(0x0F3C);
    z.or_a(0x0F3F);
    return z.final_call_if(0x0F40,k::Condition::nz);
}
template<class Z> k::Exit dungeon_wait(Z& z) {    // 00:0F43
    z.load_immediate(0x0F43,z.r.a);
    return z.final_call(0x0F45);
}
template<class Z> k::Exit dungeon_draw(Z& z) {    // 00:0F4B
    auto& r=z.r;
    z.load_a_direct(0x0F4B);
    z.or_a(0x0F4E);
    if (auto exit=z.final_return_if(0x0F4F,k::Condition::z)) return *exit;
    z.xor_a(0x0F50);
    z.store_a_direct(0x0F51);
    z.load_a_direct(0x0F54);
    z.copy(0x0F57,r.b,r.a);
    return z.final_call(0x0F58);
}
template<class Z> k::Exit dungeon_after_compare(Z& z) { // 00:0F5C
    if (auto exit=z.final_return_if(0x0F5C,k::Condition::nc)) return *exit;
    z.load_immediate(0x0F5D,z.r.b);
    return z.final_call(0x0F5F);
}
template<class Z> k::Exit dungeon_after_cell_test(Z& z) { // 00:0F62
    if (auto exit=z.final_return_if(0x0F62,k::Condition::nz)) return *exit;
    z.xor_a(0x0F63);
    z.store_a_direct(0x0F64);
    z.load_a_direct(0x0F67);
    return z.final_call(0x0F6A);
}
template<class Z> k::Exit dungeon_after_group(Z& z) { // 00:0F6D
    z.or_a(0x0F6D);
    if (auto exit=z.final_return_if(0x0F6E,k::Condition::z)) return *exit;
    return z.final_call(0x0F6F);
}
template<class Z> k::Exit dungeon_after_setup(Z& z) { // 00:0F75
    z.load_a_direct(0x0F75);
    z.or_a(0x0F78);
    return z.final_call_if(0x0F79,k::Condition::nz);
}

// ---- Registration ----------------------------------------------------------

// The comparison of the draw with the threshold, in the register B. With the
// option at its default the segment is the original instruction. Otherwise the
// same compare is rendered with B holding the scaled threshold for that one
// instruction and its original value afterwards (the option changes the
// value, not the instruction).
void add_compare(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, unsigned percent) {
    const bool scaled=percent!=encounter_percent_choices.original;
    auto spec=k::segment(unit,{k::core_final_at(entry.address)});
    spec.single_instruction=true;
    k::add_segment(registry,entry,std::move(spec),[at=entry.address,percent,scaled](auto& z) {
        const auto original=z.r.b;
        if (scaled)
            z.r.b=static_cast<std::uint8_t>(std::min<unsigned long>(255,static_cast<unsigned long>(original)*percent/100));
        z.compare_register(at,z.r.b);
        z.r.b=original;
        return k::fall_through(at,static_cast<std::uint16_t>(at+1));
    });
}
// The maxima apply only if the path cannot be estimated (a read of an unmapped
// address, whose value is the open bus); the live body renders such a read
// exactly. Each is the cost of the longest path of its segment.
void add_segment(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits,
                 bool slot2_mapper, std::uint64_t maximum, auto logic) {
    k::add_segment(registry,entry,k::segment(unit,std::move(exits),{.slot2_mapper=slot2_mapper}),maximum,logic);
}
}

void register_encounters(RoutineRegistry& registry, const Options& options) {
    using E=SegmentExit;
    const auto ret=RoutineExit::returned, fin=RoutineExit::core_final;
    // Map check unit. The maxima apply only if the path cannot be known (an
    // unmapped read) and are the cost of the longest path of each segment.
    add_segment(registry,map_check_unit,map_check_unit,{E{ret,0x5FFD},E{fin,0x5FAD}},true,324,
        [](auto& z) { return map_check_start(z); });
    add_compare(registry,map_compare_entry,map_check_unit,options.encounter_percent);
    add_segment(registry,map_after_compare_entry,map_check_unit,{E{ret,0x5FFD},E{ret,0x5FD9},E{fin,0x5FE9}},true,318,
        [](auto& z) { return map_pick_pool(z); });
    add_segment(registry,group_choice_unit,group_choice_unit,{E{ret,0x5FD9},E{fin,0x5FE9}},true,111,
        [](auto& z) { return group_choice(z); });
    add_segment(registry,group_choice_after_draw_entry,group_choice_unit,{E{ret,0x5FF8}},false,66,
        [](auto& z) { return group_pick(z); });
    // The dungeon loop also has two calls and a return registered by dungeon_view::register_load.
    add_segment(registry,dungeon_check_unit,dungeon_check_unit,{E{fin,0x0F40}},false,34,
        [](auto& z) { return dungeon_pause(z); });
    add_segment(registry,{0,0x0F43},dungeon_check_unit,{E{fin,0x0F45}},false,24,
        [](auto& z) { return dungeon_wait(z); });
    add_segment(registry,{0,0x0F4B},dungeon_check_unit,{E{ret,0x0F4F},E{fin,0x0F58}},false,73,
        [](auto& z) { return dungeon_draw(z); });
    add_compare(registry,dungeon_compare_entry,dungeon_check_unit,options.encounter_percent);
    add_segment(registry,{0,0x0F5C},dungeon_check_unit,{E{ret,0x0F5C},E{fin,0x0F5F}},false,29,
        [](auto& z) { return dungeon_after_compare(z); });
    add_segment(registry,{0,0x0F62},dungeon_check_unit,{E{ret,0x0F62},E{fin,0x0F6A}},false,52,
        [](auto& z) { return dungeon_after_cell_test(z); });
    add_segment(registry,{0,0x0F6D},dungeon_check_unit,{E{ret,0x0F6E},E{fin,0x0F6F}},false,26,
        [](auto& z) { return dungeon_after_group(z); });
    add_segment(registry,{0,0x0F75},dungeon_check_unit,{E{fin,0x0F79}},false,34,
        [](auto& z) { return dungeon_after_setup(z); });
}
}
