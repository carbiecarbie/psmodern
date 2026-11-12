// SPDX-License-Identifier: GPL-3.0-or-later
// The dungeon handler 01:65EE with its pitfall, forward and back steps and
// turns (milestone 7, part R2): the 26 segments of dungeon::movement_segments,
// rendered with dungeon_kit.h. Every segment renders the original instructions
// from its entry for any machine state and ends with one instruction executed
// by the core: a return, or a call or jump into code that stays native (the
// 3D view and its animations, the fades, the video copies of the pitfall, the
// object check, the exit lookup and the door opening, which another part
// rewrites).
//
// What the handler does, in the order it does it. It reads the cell under the
// party. A value of 8 there is an object cell or a pitfall: the handler scans
// the object records of bank 3 (seven bytes each) for this floor and cell. A
// record found, or a cell that is not 8, goes on to the directions; the end of
// the table without a record is the pitfall, which plays its fall and rise and
// ends by checking the object cell it lands on. The scan can be long (a party
// standing on an object cell runs it once per frame), so it is cut into
// pieces: a piece ends at the step to the next record once 128 cycles have
// passed, and the next piece starts at the jump back to the loop. The
// directions look at the pad's held bits: Up steps forward, or takes the
// stairs, passes an opened door or a fake wall, according to the cell ahead;
// Down steps back when the cell behind is open floor; Left and Right turn;
// with no direction held the buttons act on the cell ahead (a closed door
// opens, anything else is left to the field menu). Every step and turn ends
// by checking the object cell the party now faces or stands on.
#include "game/dungeon_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
using kit::Exit;
using kit::call_cycles;
using kit::continues;
using kit::jump_cycles;
using kit::returns;

// ---- The cell readers, rendered inline ----------------------------------------------------------
// kit::inline_reader renders a bare call to 01:6BCA (the kind) or 01:6BE9 (the
// value) with its guard: a state whose stack does not give back the pushed
// return address marks the estimate uncertain, and the maximum of every
// segment here (`refuse`, above one frame) makes it fall back.

// ---- Exit lists and bounds ---------------------------------------------------------
constexpr std::uint16_t scan_after=128;           // cycles before a chunk may end at 01:6618
constexpr std::uint16_t scan_exit=0x6618;         // the step to the next record
// A state the estimate cannot follow is refused: a maximum above one frame
// always falls back (the frame window test). Used by every segment that
// renders a cell reader inline.
constexpr std::uint64_t refuse=2*Machine::frame_cycles;

std::vector<SegmentExit> handler_exits() {
    return {kit::returned_at(0x6807),kit::returned_at(0x672B),kit::returned_at(0x67D9),
            kit::loop_at(scan_exit,scan_after),kit::core_final_at(0x6622),
            kit::conditional_at(0x6712),kit::conditional_at(0x672E),
            kit::core_final_at(0x6755),kit::core_final_at(0x675A),kit::core_final_at(0x6785),
            kit::core_final_at(0x67AF),kit::core_final_at(0x67DA),kit::core_final_at(0x6813),kit::core_final_at(0x6817)};
}

// ---- Buttons and turns (01:6802-6812, 01:677A-67DA) ----------------------------------

// 01:6802: nothing held. Button 1 or 2 looks at the cell ahead; a closed door
// goes to the door opening with C = 2 (button 1 and 2 alike), anything else to
// the field menu.
template<class Z> Exit buttons(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x6802);                    // the pad's pressed bits
    z.and_immediate(0x6805);
    if (z.zero()) return returns(0x6807,kit::taken_conditional_return_cycles);
    z.return_not_taken(0x6807);
    z.load_immediate(0x6808,r.b);
    (void)kit::inline_reader(z,true,0x680A);
    z.compare_immediate(0x680D);
    if (z.relative_if(0x680F,!z.zero())) return continues(0x6817,call_cycles);
    z.load_immediate(0x6811,r.c);
    return continues(0x6813,call_cycles);
}
// 01:67D7: Right.
template<class Z> Exit turn_right_branch(Z& z) {
    z.test_bit(0x67D7,3,z.r.c);
    if (z.zero()) return returns(0x67D9,kit::taken_conditional_return_cycles);
    z.return_not_taken(0x67D9);
    return continues(0x67DA,call_cycles);
}
// 01:67AB: Left, then Right.
template<class Z> Exit turn_left_branch(Z& z) {
    z.test_bit(0x67AB,2,z.r.c);
    if (z.relative_if(0x67AD,z.zero())) return turn_right_branch(z);
    return continues(0x67AF,call_cycles);
}
// 01:677A: Down steps back only when the cell behind is open floor (kind 0);
// every other cell, stairs included, leaves the button to the turns.
template<class Z> Exit back_branch(Z& z) {
    z.test_bit(0x677A,1,z.r.c);
    if (z.relative_if(0x677C,z.zero())) return turn_left_branch(z);
    z.load_immediate(0x677E,z.r.b);
    (void)kit::inline_reader(z,true,0x6780);
    if (z.relative_if(0x6783,!z.zero())) return turn_left_branch(z);
    return continues(0x6785,call_cycles);
}

// ---- The directions (01:66E1-6801) ----------------------------------------------------
template<class Z> Exit directions(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x66E1);                    // the pad's held bits
    z.and_immediate(0x66E4);                    // the four directions
    if (z.jump_if(0x66E6,z.zero())) return buttons(z);
    z.copy(0x66E9,r.c,r.a);
    z.test_bit(0x66EA,0,r.c);                   // Up
    if (z.jump_if(0x66EC,z.zero())) return back_branch(z);
    z.load_immediate(0x66EF,r.b);
    (void)kit::inline_reader(z,false,0x66F1);             // the cell ahead, HL on it
    z.copy(0x66F4,r.b,r.a);
    z.and_immediate(0x66F5);
    if (z.jump_if(0x66F7,z.zero())) {           // open floor: the forward step
        z.load_immediate(0x6758,r.a);
        return continues(0x675A,call_cycles);
    }
    z.subtract_immediate(0x66FA);
    if (z.jump_if(0x66FC,z.carry())) return back_branch(z);   // a wall
    z.compare_immediate(0x66FF);
    if (z.jump_if(0x6701,z.zero())) return continues(0x6755,call_cycles);   // a fake wall
    z.compare_immediate(0x6704);
    if (z.jump_if(0x6706,!z.carry())) {         // a door
        z.test_bit_at_hl(0x6729,7);
        if (z.zero()) return returns(0x672B,kit::taken_conditional_return_cycles);   // still closed
        z.return_not_taken(0x672B);
        z.test_bit(0x672C,3,r.b);
        return kit::jump_if_exit(0x672E);       // JP NZ to the exit lookup, else through the door
    }
    z.copy(0x6709,r.c,r.a);                     // stairs
    z.load_immediate(0x670A,r.a);
    z.store_a_direct(0x670C);
    z.copy(0x670F,r.a,r.c);
    z.test_bit(0x6710,3,r.b);
    return kit::jump_if_exit(0x6712);           // JP NZ to the exit lookup, else the floor changes
}

// ---- The object scan (01:660A-6619) and the pitfall's start ---------------------------
// The scan loop. At the step to the next record, once scan_after cycles have
// passed since the entry, the segment ends there with the step executed by the
// core (a loop exit); otherwise it takes the step and the jump back itself.
template<class Z> Exit scan(Z& z) {
    auto& r=z.r;
    for (;;) {
        z.load_from_hl(0x660A,r.a);
        z.compare_immediate(0x660B);
        if (z.relative_if(0x660D,z.zero())) {   // end of the table: no record, the pitfall
            z.load_de(0x661C);
            z.load_hl(0x661F);
            return continues(0x6622,10);
        }
        z.increment_hl(0x660F);
        z.compare_register(0x6610,r.b);
        if (!z.relative_if(0x6611,!z.zero())) {
            z.load_from_hl(0x6613,r.a);
            z.compare_register(0x6614,r.c);
            if (z.jump_if(0x6615,z.zero())) return directions(z);   // this floor and cell
        }
        if (z.loop_exit(scan_after)) return continues(scan_exit,11);
        z.add_to_hl(scan_exit,z.de());
        (void)z.jump(0x6619);
    }
}
template<class Z> Exit handler(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x65EE);                    // the party's cell
    z.copy(0x65F1,r.l,r.a);
    z.load_immediate(0x65F2,r.h);
    z.load_from_hl(0x65F4,r.a);
    z.compare_immediate(0x65F5);
    if (z.jump_if(0x65F7,!z.zero())) return directions(z);
    z.copy(0x65FA,r.c,r.l);
    z.load_a_direct(0x65FB);                    // the floor
    z.copy(0x65FE,r.b,r.a);
    z.load_hl(0x65FF);
    z.store_to_hl(0x6602);                      // slot 2 to the object bank
    z.load_hl(0x6604);
    z.load_de(0x6607);
    return scan(z);
}
template<class Z> Exit continuation(Z& z) {
    (void)z.jump(0x6619);
    return scan(z);
}

// ---- The pitfall (01:661C-66DE) ---------------------------------------------------------
template<class Z> Exit pitfall_start(Z& z) {
    z.load_de(0x661C);
    z.load_hl(0x661F);
    return continues(0x6622,10);                // LD BC, then the native video copy
}
// A drop step (01:6635) and a rise step (01:668B) each push the counter and wait.
template<class Z> Exit drop_step(Z& z) {
    auto& r=z.r;
    z.push(0x6635,r.b,r.c);
    z.load_a_direct(0x6636);
    z.add_immediate(0x6639);
    z.store_a_direct(0x663B);
    z.load_immediate(0x663E,r.a);
    return continues(0x6640,call_cycles);       // the wait for the next frame, the counter held
}
template<class Z> Exit rise_step(Z& z) {
    auto& r=z.r;
    z.push(0x668B,r.b,r.c);
    z.load_a_direct(0x668C);
    z.add_immediate(0x668F);
    z.store_a_direct(0x6691);
    z.load_immediate(0x6694,r.a);
    return continues(0x6696,call_cycles);
}
// The final flash (01:66BB): the scroll byte alternates between D8h and 0.
template<class Z> Exit flash(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x66BB);
    z.or_a(0x66BE);
    z.load_immediate(0x66BF,r.a);
    if (!z.relative_if(0x66C1,z.zero())) z.xor_a(0x66C3);
    z.store_a_direct(0x66C4);
    z.load_immediate(0x66C7,r.a);
    return continues(0x66C9,call_cycles);
}
// Both copies take B (the countdown) to a VRAM and a source offset.
template<class Z> void copy_offset(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.copy(at,r.a,r.b);
    z.subtract_immediate(at+1);
    z.negate(at+3);
    z.load_immediate(at+5,r.c);
    z.copy(at+7,r.b,r.a);
    z.shift_right_logical(at+8,r.b);
    z.rotate_right_through_carry(at+10,r.c);
    z.load_hl(at+12);
    z.add_to_hl(at+15,z.bc());
    z.exchange_de_hl(at+16);
    z.load_hl(at+17);
}

// ---- Steps and turns -----------------------------------------------------------------------
// HL = the delta table + (facing & 3), A = the party's position + that delta.
template<class Z> void facing_table(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.load_a_direct(at);
    z.and_immediate(at+3);
    z.load_hl(at+5);
    z.add_register(at+8,r.l);
    z.copy(at+9,r.l,r.a);
    z.add_with_carry(at+10,r.h);
    z.subtract_register(at+11,r.l);
    z.copy(at+12,r.h,r.a);
    z.load_a_direct(at+13);                     // the party's position
    z.add_from_hl(at+16);
}
// A turn (01:67B7 left, 01:67E2 right): the facing changes by one, then the
// two cells it now faces choose the animation frame.
template<class Z> Exit turn(Z& z, std::uint16_t at, bool left) {
    auto& r=z.r;
    z.load_a_direct(at);
    if (left) z.decrement(at+3,r.a); else z.increment(at+3,r.a);
    z.and_immediate(at+4);
    z.store_a_direct(at+6);
    z.load_immediate(at+9,r.h);
    z.load_immediate(at+11,r.b);
    (void)kit::inline_reader(z,true,at+13);
    if (!z.relative_if(at+16,z.zero())) { z.increment(at+18,r.h); z.increment(at+19,r.h); }
    z.load_immediate(at+20,r.b);
    (void)kit::inline_reader(z,true,at+22);
    if (!z.relative_if(at+25,z.zero())) z.increment(at+27,r.h);
    z.copy(at+28,r.a,r.h);
    return continues(at+29,jump_cycles);
}
template<class Z> Exit b_one_then(Z& z, std::uint16_t at, unsigned cycles) {
    z.load_immediate(at,z.r.b);
    return continues(at+2,cycles);
}
}

void register_dungeon_movement(RoutineRegistry& registry) {
    using dungeon::handler_unit;
    using dungeon::pitfall_unit;
    auto add=[&](std::uint16_t at, RoutineEntry unit, std::vector<SegmentExit> exits, kit::Permissions permissions,
                 std::uint64_t maximum, auto logic) {
        kit::add_segment(registry,{1,at},kit::segment(unit,std::move(exits),permissions),maximum,logic);
    };
    using kit::core_final_at;
    // ---- Handler unit: the handler, its scan continuation, stairs, door pass and the step glue.
    add(0x65EE,handler_unit,handler_exits(),{.slot2_mapper=true,.scratch_stack=4},refuse,
        [](auto& z) { return handler(z); });
    add(0x6619,handler_unit,handler_exits(),{.scratch_stack=4},refuse,
        [](auto& z) { return continuation(z); });
    // Stairs: the floor changes by +1 (kind 2) or -1; the native floor load follows.
    add(0x6715,handler_unit,{core_final_at(0x6723)},{},120,[](auto& z) {
        auto& r=z.r;
        z.or_a(0x6715);
        z.load_immediate(0x6716,r.b);
        if (!z.relative_if(0x6718,z.zero())) z.load_immediate(0x671A,r.b);
        z.load_a_direct(0x671C);
        z.add_register(0x671F,r.b);
        z.store_a_direct(0x6720);
        return continues(0x6723,call_cycles);
    });
    // Through a door or a fake wall's far side: two cells forward (position + 2 x delta).
    add(0x6734,handler_unit,{core_final_at(0x674A)},{},160,[](auto& z) {
        facing_table(z,0x6734);
        z.add_from_hl(0x6745);                  // the second addition: two cells
        z.store_a_direct(0x6746);
        z.xor_a(0x6749);
        return continues(0x674A,call_cycles);
    });
    add(0x6750,handler_unit,{core_final_at(0x6752)},{},17,[](auto& z) { return b_one_then(z,0x6750,jump_cycles); });
    add(0x6788,handler_unit,{core_final_at(0x678A)},{},24,[](auto& z) { return b_one_then(z,0x6788,call_cycles); });
    add(0x678D,handler_unit,{core_final_at(0x678F)},{},17,[](auto& z) { return b_one_then(z,0x678D,jump_cycles); });   // B = 0Bh
    add(0x67B2,handler_unit,{core_final_at(0x67B4)},{},17,[](auto& z) { return b_one_then(z,0x67B2,jump_cycles); });
    add(0x67DD,handler_unit,{core_final_at(0x67DF)},{},17,[](auto& z) { return b_one_then(z,0x67DD,jump_cycles); });

    // ---- Pitfall unit (its three interrupt-off copies stay native).
    add(0x661C,pitfall_unit,{core_final_at(0x6622)},{},30,[](auto& z) { return pitfall_start(z); });
    add(0x662C,pitfall_unit,{core_final_at(0x6640,-2)},{.scratch_stack=2},120,[](auto& z) {
        z.store_a_direct(0x662C);
        z.xor_a(0x662F);
        z.store_a_direct(0x6630);
        z.load_immediate(0x6633,z.r.b);
        return drop_step(z);
    });
    add(0x6643,pitfall_unit,{core_final_at(0x6657)},{},140,[](auto& z) {
        copy_offset(z,0x6643);                  // ... LD HL,00C0 ...
        return continues(0x6657,10);
    });
    add(0x6660,pitfall_unit,{core_final_at(0x6640,-2),core_final_at(0x666D)},{.scratch_stack=2},160,[](auto& z) {
        if (z.decrement_jump(0x6660)) return drop_step(z);
        z.load_a_direct(0x6662);
        z.subtract_immediate(0x6665);
        if (!z.relative_if(0x6667,!z.carry())) z.xor_a(0x6669);   // the floor never goes below 0
        z.store_a_direct(0x666A);
        return continues(0x666D,call_cycles);
    });
    add(0x6670,pitfall_unit,{core_final_at(0x6671)},{},21,[](auto& z) {
        z.xor_a(0x6670);
        return continues(0x6671,call_cycles);
    });
    add(0x6674,pitfall_unit,{core_final_at(0x6681)},{},800,[](auto& z) {
        z.load_hl(0x6674);
        z.load_de(0x6677);
        z.load_bc(0x667A);
        z.block_copy(0x667D);                   // the 32-byte palette
        z.load_immediate(0x667F,z.r.a);
        return continues(0x6681,call_cycles);
    });
    add(0x6684,pitfall_unit,{core_final_at(0x6696,-2)},{.scratch_stack=2},120,[](auto& z) {
        z.load_immediate(0x6684,z.r.a);
        z.store_a_direct(0x6686);
        z.load_immediate(0x6689,z.r.b);
        return rise_step(z);
    });
    add(0x6699,pitfall_unit,{core_final_at(0x66AE)},{},150,[](auto& z) {
        copy_offset(z,0x6699);                  // ... LD HL,D000 ...
        z.add_to_hl(0x66AD,z.bc());
        return continues(0x66AE,10);
    });
    add(0x66B7,pitfall_unit,{core_final_at(0x6696,-2),core_final_at(0x66C9)},{.scratch_stack=2},160,[](auto& z) {
        if (z.decrement_jump(0x66B7)) return rise_step(z);
        z.load_immediate(0x66B9,z.r.b);
        return flash(z);
    });
    add(0x66CC,pitfall_unit,{core_final_at(0x66C9),core_final_at(0x66D9)},{.slot2_mapper=true},200,[](auto& z) {
        if (z.decrement_jump(0x66CC)) return flash(z);
        z.load_hl(0x66CE);
        z.store_to_hl(0x66D1);                  // slot 2 to bank 16
        z.load_hl(0x66D3);
        z.load_de(0x66D6);
        return continues(0x66D9,call_cycles);
    });
    add(0x66DC,pitfall_unit,{core_final_at(0x66DE)},{},17,[](auto& z) { return b_one_then(z,0x66DC,jump_cycles); });

    // ---- The forward step (01:6758) and the other units.
    add(0x6758,dungeon::forward_step_unit,{core_final_at(0x675A)},{},24,[](auto& z) {
        z.load_immediate(0x6758,z.r.a);
        return continues(0x675A,call_cycles);
    });
    add(0x675D,dungeon::forward_step_unit,{core_final_at(0x6772)},{},120,[](auto& z) {
        facing_table(z,0x675D);
        z.store_a_direct(0x676E);
        z.xor_a(0x6771);
        return continues(0x6772,call_cycles);
    });
    add(0x6775,dungeon::forward_step_unit,{core_final_at(0x6777)},{},17,[](auto& z) { return b_one_then(z,0x6775,jump_cycles); });
    add(0x6792,dungeon::back_step_unit,{core_final_at(0x67A8)},{},120,[](auto& z) {
        facing_table(z,0x6792);
        z.store_a_direct(0x67A3);
        z.load_immediate(0x67A6,z.r.a);
        return continues(0x67A8,jump_cycles);
    });
    add(0x67B7,dungeon::turn_left_unit,{core_final_at(0x67D4)},{.scratch_stack=4},refuse,
        [](auto& z) { return turn(z,0x67B7,true); });
    add(0x67E2,dungeon::turn_right_unit,{core_final_at(0x67FF)},{.scratch_stack=4},refuse,
        [](auto& z) { return turn(z,0x67E2,false); });
}
}
