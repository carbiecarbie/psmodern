// SPDX-License-Identifier: GPL-3.0-or-later
// The dungeon handler 01:65EE with its pitfall, forward and back steps and
// turns (milestone 7, part R2): the 26 segments of dungeon::movement_segments,
// rendered with dungeon_kit.h. Every segment renders the original instructions
// from its entry for any machine state. The final return,
// call or jump is left to the segment that begins at its target. The drawing, fades, pitfall transfers and object checks then
// dispatch to their own registered game routines.
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
#include "game/joins.h"
#include "game/dungeon_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
using kit::Exit;

// ---- The cell readers, rendered inline ----------------------------------------------------------
// kit::inline_reader renders a bare call to 01:6BCA (the kind) or 01:6BE9 (the
// value) with its guard: a state whose stack does not give back the pushed
// return address marks the estimate uncertain, and the maximum of every
// segment here (`refuse`, above one frame) makes the segment refuse the state at its entry, which stops the run.

// ---- Exit lists and bounds ---------------------------------------------------------
constexpr std::uint16_t scan_after=128;           // cycles before a chunk may end at 01:6618
constexpr std::uint16_t scan_exit=0x6618;         // the step to the next record
// A state the estimate cannot follow is refused: a maximum above one frame
// is always refused at the segment's entry (the frame window test). Used by every segment that
// renders a cell reader inline. The stop is deliberate: the estimate returns
// this maximum, above one frame, when a read hits an open-bus (unmapped)
// address, so the segment refuses the state at its entry and the run stops.
// That is reachable only when work RAM or cartridge RAM is switched off
// through port 3E, which the game never does.
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
    if (auto exit=z.final_return_if(0x6807,kit::Condition::z)) return *exit;
    z.load_immediate(0x6808,r.b);
    (void)kit::inline_reader(z,true,0x680A);
    z.compare_immediate(0x680D);
    if (z.relative_if(0x680F,!z.zero())) return z.final_call(0x6817);
    z.load_immediate(0x6811,r.c);
    return z.final_call(0x6813);
}
// 01:67D7: Right.
template<class Z> Exit turn_right_branch(Z& z) {
    z.test_bit(0x67D7,3,z.r.c);
    if (auto exit=z.final_return_if(0x67D9,kit::Condition::z)) return *exit;
    return z.final_call(0x67DA);
}
// 01:67AB: Left, then Right.
template<class Z> Exit turn_left_branch(Z& z) {
    z.test_bit(0x67AB,2,z.r.c);
    if (z.relative_if(0x67AD,z.zero())) return turn_right_branch(z);
    return z.final_call(0x67AF);
}
// 01:677A: Down steps back only when the cell behind is open floor (kind 0);
// every other cell, stairs included, leaves the button to the turns.
template<class Z> Exit back_branch(Z& z) {
    z.test_bit(0x677A,1,z.r.c);
    if (z.relative_if(0x677C,z.zero())) return turn_left_branch(z);
    z.load_immediate(0x677E,z.r.b);
    (void)kit::inline_reader(z,true,0x6780);
    if (z.relative_if(0x6783,!z.zero())) return turn_left_branch(z);
    return z.final_call(0x6785);
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
        return z.final_call(0x675A);
    }
    z.subtract_immediate(0x66FA);
    if (z.jump_if(0x66FC,z.carry())) return back_branch(z);   // a wall
    z.compare_immediate(0x66FF);
    if (z.jump_if(0x6701,z.zero())) return z.final_call(0x6755);   // a fake wall
    z.compare_immediate(0x6704);
    if (z.jump_if(0x6706,!z.carry())) {         // a door
        z.test_bit_at_hl(0x6729,7);
        if (auto exit=z.final_return_if(0x672B,kit::Condition::z)) return *exit;   // still closed
        z.test_bit(0x672C,3,r.b);
        return z.final_jump_if(0x672E,kit::Condition::nz);   // to the exit lookup, else through the door
    }
    z.copy(0x6709,r.c,r.a);                     // stairs
    z.load_immediate(0x670A,r.a);
    z.store_a_direct(0x670C);
    z.copy(0x670F,r.a,r.c);
    z.test_bit(0x6710,3,r.b);
    return z.final_jump_if(0x6712,kit::Condition::nz);   // to the exit lookup, else the floor changes
}

// ---- The object scan (01:660A-6619) and the pitfall's start ---------------------------
// The scan loop. At the step to the next record, once scan_after cycles have
// passed since the entry, the segment ends there with the step left to the
// next segment (a loop exit); otherwise it takes the step and the jump back itself.
template<class Z> Exit scan(Z& z) {
    auto& r=z.r;
    for (;;) {
        z.load_from_hl(0x660A,r.a);
        z.compare_immediate(0x660B);
        if (z.relative_if(0x660D,z.zero())) {   // end of the table: no record, the pitfall
            z.load_de(0x661C);
            z.load_hl(0x661F);
            z.load_bc(0x6622);
            return kit::fall_through(0x6622,0x6625);
        }
        z.increment_hl(0x660F);
        z.compare_register(0x6610,r.b);
        if (!z.relative_if(0x6611,!z.zero())) {
            z.load_from_hl(0x6613,r.a);
            z.compare_register(0x6614,r.c);
            if (z.jump_if(0x6615,z.zero())) return directions(z);   // this floor and cell
        }
        if (z.loop_exit(scan_after)) {
            z.add_to_hl(scan_exit,z.de());
            return kit::fall_through(scan_exit,0x6619);
        }
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
    z.load_bc(0x6622);
    return kit::fall_through(0x6622,0x6625);    // then the video copy between DI and EI
}
// A drop step (01:6635) and a rise step (01:668B) each push the counter and wait.
template<class Z> Exit drop_step(Z& z) {
    auto& r=z.r;
    z.push(0x6635,r.b,r.c);
    z.load_a_direct(0x6636);
    z.add_immediate(0x6639);
    z.store_a_direct(0x663B);
    z.load_immediate(0x663E,r.a);
    return z.final_call(0x6640);                // the wait for the next frame, the counter held
}
template<class Z> Exit rise_step(Z& z) {
    auto& r=z.r;
    z.push(0x668B,r.b,r.c);
    z.load_a_direct(0x668C);
    z.add_immediate(0x668F);
    z.store_a_direct(0x6691);
    z.load_immediate(0x6694,r.a);
    return z.final_call(0x6696);
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
    return z.final_call(0x66C9);
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
    return z.final_jump(at+29);
}
template<class Z> Exit b_one_then(Z& z, std::uint16_t at, bool calls) {
    z.load_immediate(at,z.r.b);
    return calls ? z.final_call(at+2) : z.final_jump(at+2);
}
}

void register_pitfall_video(RoutineRegistry& registry) {
    // Clear the pictured rows before the drop, then erase and redraw rows
    // as the camera moves. Each transfer runs between the original DI/EI.
    for (auto start:pitfall_video_copies) {
        for (const unsigned offset:{0,1,4}) {
            const auto pc=static_cast<std::uint16_t>(start+offset);
            auto exit=kit::core_final_at(pc);
            if (offset!=1) exit.interrupt_mask=offset==0 ? InterruptMask::disable : InterruptMask::enable;
            auto spec=kit::segment(dungeon::pitfall_unit,{exit},{.scratch_stack=offset==1 ? 2u : 0u});
            spec.single_instruction=true;
            kit::add_segment(registry,{1,pc},std::move(spec),
                [offset](const Machine&) -> std::uint64_t { return offset==1 ? kit::call_cycles : 4; },
                [offset,pc](auto& z) {
                    if (offset==0) return z.final_disable_interrupts(pc);
                    if (offset==1) return z.final_call(pc);
                    return z.final_enable_interrupts(pc);
                });
        }
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
    // Stairs: the floor changes by +1 (kind 2) or -1; the floor load follows.
    add(0x6715,handler_unit,{core_final_at(0x6723)},{},120,[](auto& z) {
        auto& r=z.r;
        z.or_a(0x6715);
        z.load_immediate(0x6716,r.b);
        if (!z.relative_if(0x6718,z.zero())) z.load_immediate(0x671A,r.b);
        z.load_a_direct(0x671C);
        z.add_register(0x671F,r.b);
        z.store_a_direct(0x6720);
        return z.final_call(0x6723);
    });
    // Through a door or a fake wall's far side: two cells forward (position + 2 x delta).
    add(0x6734,handler_unit,{core_final_at(0x674A)},{},160,[](auto& z) {
        facing_table(z,0x6734);
        z.add_from_hl(0x6745);                  // the second addition: two cells
        z.store_a_direct(0x6746);
        z.xor_a(0x6749);
        return z.final_call(0x674A);
    });
    add(0x6750,handler_unit,{core_final_at(0x6752)},{},17,[](auto& z) { return b_one_then(z,0x6750,false); });
    add(0x6788,handler_unit,{core_final_at(0x678A)},{},24,[](auto& z) { return b_one_then(z,0x6788,true); });
    add(0x678D,handler_unit,{core_final_at(0x678F)},{},17,[](auto& z) { return b_one_then(z,0x678D,false); });   // B = 0Bh
    add(0x67B2,handler_unit,{core_final_at(0x67B4)},{},17,[](auto& z) { return b_one_then(z,0x67B2,false); });
    add(0x67DD,handler_unit,{core_final_at(0x67DF)},{},17,[](auto& z) { return b_one_then(z,0x67DD,false); });

    // ---- Pitfall unit (its three interrupt-off copies are segments of their own).
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
        z.load_bc(0x6657);
        return kit::fall_through(0x6657,0x665A);
    });
    add(0x6660,pitfall_unit,{core_final_at(0x6640,-2),core_final_at(0x666D)},{.scratch_stack=2},160,[](auto& z) {
        if (z.decrement_jump(0x6660)) return drop_step(z);
        z.load_a_direct(0x6662);
        z.subtract_immediate(0x6665);
        if (!z.relative_if(0x6667,!z.carry())) z.xor_a(0x6669);   // the floor never goes below 0
        z.store_a_direct(0x666A);
        return z.final_call(0x666D);
    });
    add(0x6670,pitfall_unit,{core_final_at(0x6671)},{},21,[](auto& z) {
        z.xor_a(0x6670);
        return z.final_call(0x6671);
    });
    add(0x6674,pitfall_unit,{core_final_at(0x6681)},{},800,[](auto& z) {
        z.load_hl(0x6674);
        z.load_de(0x6677);
        z.load_bc(0x667A);
        z.block_copy(0x667D);                   // the 32-byte palette
        z.load_immediate(0x667F,z.r.a);
        return z.final_call(0x6681);
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
        z.load_bc(0x66AE);
        return kit::fall_through(0x66AE,0x66B1);
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
        return z.final_call(0x66D9);
    });
    add(0x66DC,pitfall_unit,{core_final_at(0x66DE)},{},17,[](auto& z) { return b_one_then(z,0x66DC,false); });

    // ---- The forward step (01:6758) and the other units.
    add(0x6758,dungeon::forward_step_unit,{core_final_at(0x675A)},{},24,[](auto& z) {
        z.load_immediate(0x6758,z.r.a);
        return z.final_call(0x675A);
    });
    add(0x675D,dungeon::forward_step_unit,{core_final_at(0x6772)},{},120,[](auto& z) {
        facing_table(z,0x675D);
        z.store_a_direct(0x676E);
        z.xor_a(0x6771);
        return z.final_call(0x6772);
    });
    add(0x6775,dungeon::forward_step_unit,{core_final_at(0x6777)},{},17,[](auto& z) { return b_one_then(z,0x6775,false); });
    add(0x6792,dungeon::back_step_unit,{core_final_at(0x67A8)},{},120,[](auto& z) {
        facing_table(z,0x6792);
        z.store_a_direct(0x67A3);
        z.load_immediate(0x67A6,z.r.a);
        return z.final_jump(0x67A8);
    });
    add(0x67B7,dungeon::turn_left_unit,{core_final_at(0x67D4)},{.scratch_stack=4},refuse,
        [](auto& z) { return turn(z,0x67B7,true); });
    add(0x67E2,dungeon::turn_right_unit,{core_final_at(0x67FF)},{.scratch_stack=4},refuse,
        [](auto& z) { return turn(z,0x67E2,false); });

    // ---- Joining stretches: where the original resumes after a call or a
    // cell reader came back to the handler's code.
    auto one=[&](std::uint16_t at,RoutineEntry unit,SegmentExit exit,kit::Permissions permissions,auto logic) {
        auto spec=kit::segment(unit,{exit},permissions);
        spec.single_instruction=true;
        kit::add_segment(registry,{1,at},std::move(spec),std::uint64_t{24},logic);
    };
    // The floor loader returned for a staircase: on to the fade-out.
    one(0x6726,handler_unit,core_final_at(0x6726),{},[](auto& z) { return z.final_jump(0x6726); });
    one(0x6731,handler_unit,core_final_at(0x6731),{.scratch_stack=2},[](auto& z) { return z.final_call(0x6731); });
    // The redraw returned: fade the screen back in.
    one(0x674D,handler_unit,core_final_at(0x674D),{.scratch_stack=2},[](auto& z) { return z.final_call(0x674D); });
    // The door opening and the field menu return to the handler's caller.
    one(0x681A,handler_unit,kit::returned_at(0x681A),{},[](auto& z) { return z.final_return(0x681A); });
    add(0x6816,handler_unit,{kit::returned_at(0x6816),core_final_at(0x6817)},{.scratch_stack=2},48,[](auto& z) {
        if (auto exit=z.final_return_if(0x6816,kit::Condition::z)) return *exit;
        return z.final_call(0x6817);
    });
    // The cell ahead was read: the open-floor test of the Up direction.
    add(0x66F4,handler_unit,{kit::conditional_at(0x66F7)},{},32,[](auto& z) {
        z.copy(0x66F4,z.r.b,z.r.a);
        z.and_immediate(0x66F5);
        return z.final_jump_if(0x66F7,kit::Condition::z);
    });
    // The right turn's second cell was read: pick the animation frame.
    add(0x67FB,dungeon::turn_right_unit,{core_final_at(0x67FF)},{},48,[](auto& z) {
        if (!z.relative_if(0x67FB,z.zero())) z.increment(0x67FD,z.r.h);
        z.copy(0x67FE,z.r.a,z.r.h);
        return z.final_jump(0x67FF);
    });
    // The pitfall's video copies resume here: the next constant is loaded,
    // or the saved counter is restored before the wait loop.
    one(0x662A,pitfall_unit,core_final_at(0x662A),{},[](auto& z) {
        z.load_immediate(0x662A,z.r.a);
        return kit::fall_through(0x662A,0x662C);
    });
    for (const std::uint16_t at:{0x665F,0x66B6}) {
        auto spec=kit::segment(pitfall_unit,{core_final_at(at)});
        spec.single_instruction=true;
        kit::add_segment(registry,{1,at},std::move(spec),std::uint64_t{24},[at](auto& z) -> Exit {
            z.pop(at,z.r.b,z.r.c);
            return kit::fall_through(at,at+1);
        });
    }
}

// Joining instructions no input recording reaches (milestone 14, Part I; see
// game/joins.h): after the forward-step test, the step command less two
// selects the step back.
void register_dungeon_movement_walk_joins(RoutineRegistry& registry) {
    kit::add_segment(registry,{1,0x66FA},kit::segment({1,0x65EE},{kit::conditional_at(0x66FC)}),17,[](auto& z) {
        z.subtract_immediate(0x66FA);
        return z.final_jump_if(0x66FC,kit::Condition::c);
    });
    // The other step and turn commands: step back, the fake wall, passing a
    // door, the exit lookup, turning right and left.
    kit::add_segment(registry,{1,0x677A},kit::segment({1,0x65EE},{kit::conditional_at(0x677C)}),20,[](auto& z) {
        z.test_bit(0x677A,1,z.r.c);
        return z.final_relative_if(0x677C,kit::Condition::z);
    });
    kit::add_segment(registry,{1,0x677E},kit::segment({1,0x65EE},{kit::core_final_at(0x6780)},{.scratch_stack=2}),24,[](auto& z) {
        z.load_immediate(0x677E,z.r.b);
        return z.final_call(0x6780);
    });
    kit::add_joining(registry,{1,0x6783},{1,0x65EE},{kit::conditional_at(0x6783)},12,0,[](auto& z) { return z.final_relative_if(0x6783,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x6785},{1,0x65EE},{kit::core_final_at(0x6785)},17,2,[](auto& z) { return z.final_call(0x6785); });
    kit::add_segment(registry,{1,0x67AB},kit::segment({1,0x65EE},{kit::conditional_at(0x67AD)}),20,[](auto& z) {
        z.test_bit(0x67AB,2,z.r.c);
        return z.final_relative_if(0x67AD,kit::Condition::z);
    });
    kit::add_joining(registry,{1,0x67AF},{1,0x65EE},{kit::core_final_at(0x67AF)},17,2,[](auto& z) { return z.final_call(0x67AF); });
    kit::add_segment(registry,{1,0x66FF},kit::segment({1,0x65EE},{kit::conditional_at(0x6701)}),17,[](auto& z) {
        z.compare_immediate(0x66FF);
        return z.final_jump_if(0x6701,kit::Condition::z);
    });
    kit::add_segment(registry,{1,0x6704},kit::segment({1,0x65EE},{kit::conditional_at(0x6706)}),17,[](auto& z) {
        z.compare_immediate(0x6704);
        return z.final_jump_if(0x6706,kit::Condition::nc);
    });
    kit::add_segment(registry,{1,0x6709},kit::segment({1,0x65EE},{kit::conditional_at(0x6712)}),46,[](auto& z) {
        z.copy(0x6709,z.r.c,z.r.a);
        z.load_immediate(0x670A,z.r.a);
        z.store_a_direct(0x670C);
        z.copy(0x670F,z.r.a,z.r.c);
        z.test_bit(0x6710,3,z.r.b);
        return z.final_jump_if(0x6712,kit::Condition::nz);
    });
    kit::add_joining(registry,{1,0x6755},{1,0x65EE},{kit::core_final_at(0x6755)},17,2,[](auto& z) { return z.final_call(0x6755); });
    // The two turn and look-around choices that end the command early.
    kit::add_segment(registry,{1,0x6729},kit::segment({1,0x65EE},{kit::returned_at(0x672B),kit::conditional_at(0x672E)}),40,[](auto& z) {
        z.test_bit_at_hl(0x6729,7);
        if (auto exit=z.final_return_if(0x672B,kit::Condition::z)) return *exit;
        z.test_bit(0x672C,3,z.r.b);
        return z.final_jump_if(0x672E,kit::Condition::nz);
    });
    kit::add_segment(registry,{1,0x67D7},kit::segment({1,0x65EE},{kit::returned_at(0x67D9),kit::core_final_at(0x67DA)},{.scratch_stack=2}),36,[](auto& z) {
        z.test_bit(0x67D7,3,z.r.c);
        if (auto exit=z.final_return_if(0x67D9,kit::Condition::z)) return *exit;
        return z.final_call(0x67DA);
    });
}
}
