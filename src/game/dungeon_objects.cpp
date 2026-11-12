// SPDX-License-Identifier: GPL-3.0-or-later
// Opening a door (01:681B), the exit lookup (01:68BC) and the object check
// with its event part (01:6963), milestone 7 part R3: the 25 segments of
// dungeon::object_segments, rendered with dungeon_kit.h.
//
// Each segment is written once as a sequence of original instructions over a
// bus; its bound is that sequence run on the estimating bus, so it is the exact
// cost of the path this machine state takes. A segment ends where the routine
// calls, jumps or returns into code that stays native, and the core executes
// that last instruction.
//
// Three things in these routines need the milestone's mechanism extensions:
//  - words held on the stack across a native call (the door's loop counter and
//    row, the object check's saved cell and its fight or event words), popped
//    by the segment that the call returns to;
//  - a conditional jump as the last instruction (01:68F4 to the world-map
//    transfer or on to the exit kinds);
//  - the two table scans, split at their record step (01:68E1, 01:6988): a
//    scan under the party standing on an object cell can run for thousands of
//    cycles, so a chunk ends at the step to the next record once enough
//    cycles have passed, and the continuation entry starts the next chunk.
// Both scans read their tables through slot 2 after the segment that starts
// them has switched it to bank 3; the continuations never switch it, so they
// read whatever is mapped there, as the original does.
#include "game/dungeon_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace d=dungeon;
using kit::Exit;
using kit::continues;
using kit::returns;
inline constexpr unsigned taken_return=kit::taken_conditional_return_cycles;

// The cell reader 01:6BE9 reached by a bare call, rendered inline by
// kit::inline_reader with the shared guard: a stack the pushed return address
// cannot be read back from (ROM, for one) makes the original return somewhere
// else, which a segment cannot follow. The estimate then marks itself
// uncertain and the three segments that render the reader have a maximum
// above a frame, so dispatch falls back (reason frame) and the core runs the
// original; the states are outside any play, and a reader that did return
// here is exact. The segment stops rendering at once in that case.
inline constexpr std::uint64_t unpredictable_cycles=1u<<20;
inline Exit unpredictable() { return Exit{{RoutineExit::returned,0},static_cast<unsigned>(unpredictable_cycles)}; }

// ---- 01:681B: the door -----------------------------------------------------------
// The cell ahead; one already carrying the opened mark returns at once. Else
// the mark is set, a sound byte is stored, and the overlay is drawn in three
// rounds of rows: BC (the round counter and the row base copied from C) and HL
// (the row) are pushed and stay held across each native call that draws one.
template<class Z> Exit door_open(Z& z) {                    // 01:681B-6832
    auto& r=z.r;
    z.load_immediate(0x681B,r.b);
    if (!kit::inline_reader(z,false,0x681D)) return unpredictable();
    z.test_bit_at_hl(0x6820,7);
    if (!z.zero()) return returns(0x6822,taken_return);
    z.return_not_taken(0x6822);
    z.set_bit_at_hl(0x6823,7);
    z.load_immediate(0x6825,r.a);
    z.store_a_direct(0x6827);
    z.copy(0x682A,r.h,r.c);
    z.load_immediate(0x682B,r.l);
    z.load_immediate(0x682D,r.b);
    z.push(0x682F,r.b,r.c);
    z.push(0x6830,r.h,r.l);
    z.copy(0x6831,r.a,r.h);
    return continues(0x6832,kit::call_cycles);
}
template<class Z> Exit door_wait(Z& z) {                    // 01:6835-6837: a wait of 0Ch frames
    z.load_immediate(0x6835,z.r.a);
    return continues(0x6837,kit::call_cycles);
}
// After a wait: the next four-row step of the overlay; when the high byte of
// the row no longer changes the same round continues, else the round counter
// (held below the row) counts down and the caller gets control back.
template<class Z> Exit door_rows(Z& z) {                    // 01:683A-6849
    auto& r=z.r;
    z.pop(0x683A,r.h,r.l);
    z.copy(0x683B,r.a,r.h);
    z.load_bc(0x683C);
    z.add_to_hl(0x683F,z.bc());
    z.compare_register(0x6840,r.h);
    if (z.relative_if(0x6841,z.zero())) {                   // same round: draw the row at 6830
        z.push(0x6830,r.h,r.l);
        z.copy(0x6831,r.a,r.h);
        return continues(0x6832,kit::call_cycles);
    }
    z.increment(0x6843,r.h);
    z.increment(0x6844,r.h);
    z.pop(0x6845,r.b,r.c);
    if (z.decrement_jump(0x6846)) {                         // the next round at 682F
        z.push(0x682F,r.b,r.c);
        z.push(0x6830,r.h,r.l);
        z.copy(0x6831,r.a,r.h);
        return continues(0x6832,kit::call_cycles);
    }
    z.xor_a(0x6848);
    return returns(0x6849,kit::return_cycles);
}

// ---- 01:68BC: the exit lookup ---------------------------------------------------
// A cell with the exit mark (bit 3) is looked up by floor and cell in the exit
// records, five bytes each, in bank 3. No record: the game mode becomes 8 (to
// the world map). A record: a kind below 80h goes to the world-map transfer
// through the conditional jump at 01:68F4; the other kinds are handled by the
// later segments.
template<class Z> Exit exit_match(Z& z) {                    // 01:68EC-68F4
    auto& r=z.r;
    z.increment_hl(0x68EC);
    z.load_from_hl(0x68ED,r.a);
    z.copy(0x68EE,r.d,r.a);
    z.decrement_hl(0x68EF);
    z.compare_immediate(0x68F0);
    z.load_immediate(0x68F2,r.a);
    return kit::jump_if_exit(0x68F4);
}
// The scan from the record test at 01:68D4. A chunk is one record: at the step
// to the next one (68E1) the segment ends with the addition, and the entry at
// 68E2 resumes at its jump back.
template<class Z> Exit exit_scan(Z& z) {                     // 01:68D4-68EB
    auto& r=z.r;
    for (;;) {
        z.load_from_hl(0x68D4,r.a);
        z.compare_immediate(0x68D5);
        if (z.relative_if(0x68D7,z.zero())) {               // the end of the table: no record
            z.load_hl(0x68E6);
            z.store_to_hl(0x68E9);
            return returns(0x68EB,kit::return_cycles);
        }
        z.increment_hl(0x68D9);
        z.compare_register(0x68DA,r.b);
        if (!z.relative_if(0x68DB,!z.zero())) {
            z.load_from_hl(0x68DD,r.a);
            z.compare_register(0x68DE,r.c);
            if (z.relative_if(0x68DF,z.zero())) return exit_match(z);
        }
        if (z.loop_exit(0)) return continues(0x68E1,11);
        z.add_to_hl(0x68E1,z.de());
        (void)z.jump(0x68E2);
    }
}
template<class Z> Exit exit_lookup(Z& z) {                   // 01:68BC-68D4 and the scan
    auto& r=z.r;
    z.load_immediate(0x68BC,r.b);
    if (!kit::inline_reader(z,false,0x68BE)) return unpredictable();
    z.and_immediate(0x68C1);
    if (z.zero()) return returns(0x68C3,taken_return);
    z.return_not_taken(0x68C3);
    z.copy(0x68C4,r.c,r.l);
    z.load_a_direct(0x68C5);
    z.copy(0x68C8,r.b,r.a);
    z.load_hl(0x68C9);
    z.store_to_hl(0x68CC);
    z.load_hl(0x68CE);
    z.load_de(0x68D1);
    return exit_scan(z);
}
template<class Z> Exit exit_continue(Z& z) {                 // 01:68E2: the jump back to the scan
    (void)z.jump(0x68E2);
    return exit_scan(z);
}
// Kinds that do not go to the world map. FFh: the scene, with HL held; the
// others: HL and AF held, then FEh and FDh start an interaction and any other
// kind joins the event path at 6925.
template<class Z> Exit exit_kind(Z& z) {                     // 01:68F7-6949
    auto& r=z.r;
    z.copy(0x68F7,r.a,r.d);
    z.compare_immediate(0x68F8);
    if (!z.relative_if(0x68FA,!z.zero())) {
        z.push(0x68FC,r.h,r.l);
        return continues(0x68FD,kit::call_cycles);
    }
    z.push(0x6947,r.h,r.l);
    z.push(0x6948,r.a,r.f);
    return continues(0x6949,kit::call_cycles);
}
template<class Z> Exit exit_scene_graphics(Z& z) {           // 01:6900-690B
    z.load_hl(0x6900);
    z.store_to_hl(0x6903);
    z.load_hl(0x6905);
    z.load_de(0x6908);
    return continues(0x690B,kit::call_cycles);
}
template<class Z> Exit exit_scene_map(Z& z) {               // 01:690E-6911
    z.load_hl(0x690E);
    return continues(0x6911,kit::call_cycles);
}
// The interaction is set; the shared wait at 691D follows.
template<class Z> Exit exit_interaction(Z& z) {             // 01:6914-691F
    auto& r=z.r;
    z.load_immediate(0x6914,r.a);
    z.store_a_direct(0x6916);
    z.xor_a(0x6919);
    z.store_a_direct(0x691A);
    z.load_immediate(0x691D,r.a);
    return continues(0x691F,kit::call_cycles);
}
template<class Z> Exit exit_wait_jump(Z& z) {              // 01:6960-691F
    (void)z.jump(0x6960);
    z.load_immediate(0x691D,z.r.a);
    return continues(0x691F,kit::call_cycles);
}
// The event of the exit: bank 3, the held HL, two steps on to the event record.
template<class Z> Exit exit_event(Z& z) {                   // 01:6925-692D
    auto& r=z.r;
    z.load_hl(0x6925);
    z.store_to_hl(0x6928);
    z.pop(0x692A,r.h,r.l);
    z.increment_hl(0x692B);
    z.increment_hl(0x692C);
    return continues(0x692D,kit::call_cycles);
}
template<class Z> Exit exit_after_event(Z& z) {            // 01:6930-6936
    z.load_a_direct(0x6930);
    z.compare_immediate(0x6933);
    if (!z.zero()) return returns(0x6935,taken_return);
    z.return_not_taken(0x6935);
    return continues(0x6936,kit::call_cycles);
}
template<class Z> Exit exit_redraw(Z& z) {                  // 01:6939-693A
    z.xor_a(0x6939);
    return continues(0x693A,kit::call_cycles);
}
// FEh and FDh start an interaction (0Dh, 1Eh); any other kind is an event.
template<class Z> Exit exit_pick(Z& z) {                    // 01:694C-695D
    auto& r=z.r;
    z.pop(0x694C,r.a,r.f);
    z.load_immediate(0x694D,r.c);
    z.compare_immediate(0x694F);
    bool interaction=z.relative_if(0x6951,z.zero());
    if (!interaction) {
        z.load_immediate(0x6953,r.c);
        z.compare_immediate(0x6955);
        if (z.relative_if(0x6957,!z.zero())) return exit_event(z);
    }
    z.copy(0x6959,r.a,r.c);
    z.store_a_direct(0x695A);
    return continues(0x695D,kit::call_cycles);
}

// ---- 01:6963: the object check --------------------------------------------------
// A cell of value 8 (an object or a pitfall) is looked up by floor and cell in
// the object records, seven bytes each, in bank 3. BC (the view index and the
// cell) is held on the stack through the scan. A match: its done flag, then the
// reward and the pending bytes, then by type the item chest (0), money chest
// (1), fixed fight (2) or scripted event (3). A chest seen from behind, B = 0Bh,
// does nothing; a fight or event behind turns the party around first.
template<class Z> Exit object_event_pointer(Z& z) {         // 01:6A2F-6A39
    auto& r=z.r;
    z.load_from_hl(0x6A2F,r.a);
    z.increment_hl(0x6A30);
    z.load_from_hl(0x6A31,r.h);
    z.copy(0x6A32,r.l,r.a);
    z.store_hl_direct(0x6A33);
    z.load_a_direct(0x6A36);
    return continues(0x6A39,kit::call_cycles);
}
template<class Z> Exit object_fight_record(Z& z) {          // 01:69F7-6A04
    auto& r=z.r;
    z.load_from_hl(0x69F7,r.a);
    z.store_a_direct(0x69F8);
    z.or_a(0x69FB);
    if (z.zero()) return returns(0x69FC,taken_return);
    z.return_not_taken(0x69FC);
    z.increment_hl(0x69FD);
    z.load_from_hl(0x69FE,r.a);
    z.push(0x69FF,r.a,r.f);
    z.load_hl_direct(0x6A00);
    z.push(0x6A03,r.h,r.l);
    return continues(0x6A04,kit::call_cycles);
}
template<class Z> Exit object_reward_done(Z& z) {           // 01:69C7-69CE: a chest, from ahead only
    auto& r=z.r;
    z.copy(0x69C7,r.a,r.b);
    z.compare_immediate(0x69C8);
    if (!z.zero()) return returns(0x69CA,taken_return);
    z.return_not_taken(0x69CA);
    z.load_hl(0x69CB);
    return continues(0x69CE,kit::call_cycles);
}
template<class Z> Exit object_fight_or_event(Z& z) {        // 01:69E1-6A23
    auto& r=z.r;
    z.compare_immediate(0x69E1);
    if (z.relative_if(0x69E3,!z.zero())) {                  // not a fight: 6A1A
        z.compare_immediate(0x6A1A);
        if (!z.zero()) return returns(0x6A1C,taken_return);
        z.return_not_taken(0x6A1C);
        z.copy(0x6A1D,r.a,r.b);
        z.compare_immediate(0x6A1E);
        if (z.relative_if(0x6A20,z.zero())) return object_event_pointer(z);
        z.push(0x6A22,r.h,r.l);
        return continues(0x6A23,kit::call_cycles);
    }
    z.copy(0x69E5,r.a,r.b);
    z.compare_immediate(0x69E6);
    if (z.relative_if(0x69E8,z.zero())) return object_fight_record(z);
    z.push(0x69EA,r.h,r.l);
    return continues(0x69EB,kit::call_cycles);
}
template<class Z> Exit object_match(Z& z) {                 // 01:698E-6A3E: a record matched
    auto& r=z.r;
    z.pop(0x698E,r.b,r.c);
    z.increment_hl(0x698F);
    z.load_from_hl(0x6990,r.e);
    z.increment_hl(0x6991);
    z.load_from_hl(0x6992,r.d);
    z.load_a_from_pair(0x6993,z.de());
    z.compare_immediate(0x6994);
    if (z.zero()) return returns(0x6996,taken_return);      // already done
    z.return_not_taken(0x6996);
    z.store_pair_direct(0x6997,r.d,r.e);
    z.load_immediate(0x699B,r.a);
    z.store_a_direct(0x699D);
    z.increment_hl(0x69A0);
    z.load_from_hl(0x69A1,r.a);
    z.increment_hl(0x69A2);
    z.or_a(0x69A3);
    if (z.relative_if(0x69A4,!z.zero())) {
        z.compare_immediate(0x69B8);
        if (z.relative_if(0x69BA,!z.zero())) return object_fight_or_event(z);
        z.xor_a(0x69BC);                                    // money chest
        z.store_a_direct(0x69BD);
        z.load_from_hl(0x69C0,r.a);
        z.increment_hl(0x69C1);
        z.load_from_hl(0x69C2,r.h);
        z.copy(0x69C3,r.l,r.a);
        z.store_hl_direct(0x69C4);
        return object_reward_done(z);
    }
    z.load_from_hl(0x69A6,r.a);                             // item chest
    z.store_a_direct(0x69A7);
    z.increment_hl(0x69AA);
    z.load_from_hl(0x69AB,r.a);
    z.store_a_direct(0x69AC);
    z.load_hl(0x69AF);
    z.store_hl_direct(0x69B2);
    (void)z.jump(0x69B5);
    return object_reward_done(z);
}
// The scan from the record test at 01:697A; a chunk ends at the step to the
// next record once 128 cycles have passed, with BC still held.
template<class Z> Exit object_scan(Z& z) {                  // 01:697A-698D
    auto& r=z.r;
    for (;;) {
        z.load_from_hl(0x697A,r.a);
        z.compare_immediate(0x697B);
        if (z.jump_if(0x697D,z.zero())) {                   // the end of the table: nothing here
            z.pop(0x698C,r.b,r.c);
            return returns(0x698D,kit::return_cycles);
        }
        z.increment_hl(0x6980);
        z.compare_register(0x6981,r.b);
        if (!z.relative_if(0x6982,!z.zero())) {
            z.load_from_hl(0x6984,r.a);
            z.compare_register(0x6985,r.c);
            if (z.relative_if(0x6986,z.zero())) return object_match(z);
        }
        if (z.loop_exit(128)) return continues(0x6988,11);
        z.add_to_hl(0x6988,z.de());
        (void)z.jump(0x6989);
    }
}
template<class Z> Exit object_check(Z& z) {                 // 01:6963-697A and the scan
    auto& r=z.r;
    if (!kit::inline_reader(z,false,0x6963)) return unpredictable();
    z.compare_immediate(0x6966);
    if (!z.zero()) return returns(0x6968,taken_return);
    z.return_not_taken(0x6968);
    z.copy(0x6969,r.c,r.l);
    z.push(0x696A,r.b,r.c);
    z.load_a_direct(0x696B);
    z.copy(0x696E,r.b,r.a);
    z.load_hl(0x696F);
    z.store_to_hl(0x6972);
    z.load_hl(0x6974);
    z.load_de(0x6977);
    return object_scan(z);
}
template<class Z> Exit object_continue(Z& z) {              // 01:6989: the jump back to the scan
    (void)z.jump(0x6989);
    return object_scan(z);
}
// After the native calls of the chest and fight paths.
template<class Z> Exit object_dialogue_return(Z& z) {       // 01:69D1: hold BC over the next call
    z.push(0x69D1,z.r.b,z.r.c);
    return continues(0x69D2,kit::call_cycles);
}
template<class Z> Exit object_pop_bc(Z& z) {                // 01:69D5
    z.pop(0x69D5,z.r.b,z.r.c);
    return continues(0x69D6,kit::call_cycles);
}
template<class Z> Exit object_menu_flag(Z& z, std::uint16_t at) {   // 01:69D9 and 01:6A12
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    const auto ret=static_cast<std::uint16_t>(at+4);
    if (z.zero()) return returns(ret,taken_return);
    z.return_not_taken(ret);
    return continues(static_cast<std::uint16_t>(ret+1),kit::jump_cycles);
}
template<class Z> Exit object_fight_after_turns(Z& z) {     // 01:69F1: the turns are done
    auto& r=z.r;
    z.load_hl(0x69F1);
    z.store_to_hl(0x69F4);
    z.pop(0x69F6,r.h,r.l);
    return object_fight_record(z);
}
template<class Z> Exit object_fight_after_setup(Z& z) {     // 01:6A07: the fight's words come back
    auto& r=z.r;
    z.pop(0x6A07,r.h,r.l);
    z.store_hl_direct(0x6A08);
    z.pop(0x6A0B,r.a,r.f);
    z.store_a_direct(0x6A0C);
    return continues(0x6A0F,kit::call_cycles);
}
template<class Z> Exit object_event_after_turns(Z& z) {     // 01:6A29: the turns are done
    auto& r=z.r;
    z.load_hl(0x6A29);
    z.store_to_hl(0x6A2C);
    z.pop(0x6A2E,r.h,r.l);
    return object_event_pointer(z);
}
template<class Z> Exit object_event_done(Z& z) {            // 01:6A3F-6A57: the event state is cleared
    auto& r=z.r;
    z.load_immediate(0x6A3F,r.a);
    z.store_a_direct(0x6A41);
    z.xor_a(0x6A44);
    z.store_a_direct(0x6A45);
    z.store_a_direct(0x6A48);
    z.store_a_direct(0x6A4B);
    z.store_a_direct(0x6A4E);
    z.load_hl(0x6A51);
    z.store_hl_direct(0x6A54);
    return returns(0x6A57,kit::return_cycles);
}

// A segment on its unit with its exits and permissions. The maximum is only
// used when the estimate read an unmapped address (RAM switched off): it covers
// the longest path, a match through the longest type path being under 600
// cycles, and for the three segments that render the cell reader inline it is
// above a frame, so that state falls back (see unpredictable_cycles).
template<class Logic> void add(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits,
                               kit::Permissions permissions, std::uint64_t maximum, Logic logic) {
    kit::add_segment(registry,entry,kit::segment(unit,std::move(exits),permissions),maximum,logic);
}
}

void register_dungeon_objects(RoutineRegistry& registry) {
    using kit::returned_at;
    using kit::core_final_at;
    using kit::conditional_at;
    using kit::loop_at;
    const auto at=[](std::uint16_t address) { return RoutineEntry{1,address}; };
    constexpr std::uint64_t scan_maximum=1500;
    // The door: both loops end at 6832 with the same stack, the done path
    // returns the caller's address one word and the held BC above the entry.
    add(registry,d::door_unit,d::door_unit,{returned_at(0x6822),core_final_at(0x6832,-4)},{.scratch_stack=4},unpredictable_cycles,
        [](auto& z) -> Exit { return door_open(z); });
    add(registry,at(0x6835),d::door_unit,{core_final_at(0x6837)},{},24,
        [](auto& z) -> Exit { return door_wait(z); });
    add(registry,at(0x683A),d::door_unit,{core_final_at(0x6832),returned_at(0x6849,4)},{.inherited_stack=4},200,
        [](auto& z) -> Exit { return door_rows(z); });
    // The exit lookup.
    const auto exit_unit=d::exit_lookup_unit;
    add(registry,exit_unit,exit_unit,{returned_at(0x68C3),returned_at(0x68EB),loop_at(0x68E1,0),conditional_at(0x68F4)},
        {.slot2_mapper=true,.scratch_stack=2},unpredictable_cycles,[](auto& z) -> Exit { return exit_lookup(z); });
    add(registry,at(0x68E2),exit_unit,{loop_at(0x68E1,0),returned_at(0x68EB),conditional_at(0x68F4)},{},scan_maximum,
        [](auto& z) -> Exit { return exit_continue(z); });
    add(registry,at(0x68F7),exit_unit,{core_final_at(0x68FD,-2),core_final_at(0x6949,-4)},{.scratch_stack=4},200,
        [](auto& z) -> Exit { return exit_kind(z); });
    add(registry,at(0x6900),exit_unit,{core_final_at(0x690B)},{.slot2_mapper=true},100,
        [](auto& z) -> Exit { return exit_scene_graphics(z); });
    add(registry,at(0x690E),exit_unit,{core_final_at(0x6911)},{},40,
        [](auto& z) -> Exit { return exit_scene_map(z); });
    add(registry,at(0x6914),exit_unit,{core_final_at(0x691F)},{},100,
        [](auto& z) -> Exit { return exit_interaction(z); });
    add(registry,at(0x6925),exit_unit,{core_final_at(0x692D,2)},{.slot2_mapper=true,.inherited_stack=2},100,
        [](auto& z) -> Exit { return exit_event(z); });
    add(registry,at(0x6930),exit_unit,{returned_at(0x6935),core_final_at(0x6936)},{},80,
        [](auto& z) -> Exit { return exit_after_event(z); });
    add(registry,at(0x6939),exit_unit,{core_final_at(0x693A)},{},40,
        [](auto& z) -> Exit { return exit_redraw(z); });
    add(registry,at(0x694C),exit_unit,{core_final_at(0x695D,2),core_final_at(0x692D,4)},{.slot2_mapper=true,.inherited_stack=4},200,
        [](auto& z) -> Exit { return exit_pick(z); });
    add(registry,at(0x6960),exit_unit,{core_final_at(0x691F)},{},60,
        [](auto& z) -> Exit { return exit_wait_jump(z); });
    // The object check.
    const auto object_unit=d::object_check_unit;
    add(registry,object_unit,object_unit,
        {returned_at(0x6968),returned_at(0x698D),returned_at(0x6996),returned_at(0x69CA),returned_at(0x69FC),returned_at(0x6A1C),
         loop_at(0x6988,128,-2),core_final_at(0x69CE),core_final_at(0x69EB,-2),core_final_at(0x6A04,-4),core_final_at(0x6A23,-2),
         core_final_at(0x6A39)},
        {.slot2_mapper=true,.scratch_stack=4},unpredictable_cycles,[](auto& z) -> Exit { return object_check(z); });
    add(registry,at(0x6989),object_unit,
        {returned_at(0x698D,2),returned_at(0x6996,2),returned_at(0x69CA,2),returned_at(0x69FC,2),returned_at(0x6A1C,2),
         loop_at(0x6988,128),core_final_at(0x69CE,2),core_final_at(0x69EB),core_final_at(0x6A04,-2),core_final_at(0x6A23),
         core_final_at(0x6A39,2)},
        {.scratch_stack=2,.inherited_stack=2},scan_maximum,[](auto& z) -> Exit { return object_continue(z); });
    add(registry,at(0x69D1),object_unit,{core_final_at(0x69D2,-2)},{.scratch_stack=2},40,
        [](auto& z) -> Exit { return object_dialogue_return(z); });
    add(registry,at(0x69D5),object_unit,{core_final_at(0x69D6,2)},{.inherited_stack=2},40,
        [](auto& z) -> Exit { return object_pop_bc(z); });
    add(registry,at(0x69D9),object_unit,{returned_at(0x69DD),core_final_at(0x69DE)},{},60,
        [](auto& z) -> Exit { return object_menu_flag(z,0x69D9); });
    add(registry,at(0x69F1),object_unit,{returned_at(0x69FC,2),core_final_at(0x6A04,-2)},
        {.slot2_mapper=true,.scratch_stack=2,.inherited_stack=2},300,[](auto& z) -> Exit { return object_fight_after_turns(z); });
    add(registry,at(0x6A07),object_unit,{core_final_at(0x6A0F,4)},{.inherited_stack=4},100,
        [](auto& z) -> Exit { return object_fight_after_setup(z); });
    add(registry,at(0x6A12),object_unit,{returned_at(0x6A16),core_final_at(0x6A17)},{},60,
        [](auto& z) -> Exit { return object_menu_flag(z,0x6A12); });
    add(registry,at(0x6A29),object_unit,{core_final_at(0x6A39,2)},{.slot2_mapper=true,.inherited_stack=2},200,
        [](auto& z) -> Exit { return object_event_after_turns(z); });
    add(registry,at(0x6A2F),object_unit,{core_final_at(0x6A39)},{},120,
        [](auto& z) -> Exit { return object_event_pointer(z); });
    add(registry,at(0x6A3F),object_unit,{returned_at(0x6A57)},{},200,
        [](auto& z) -> Exit { return object_event_done(z); });
}
}
