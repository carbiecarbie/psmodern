// SPDX-License-Identifier: GPL-3.0-or-later
// Scene visuals (milestone 13): the visual records and transparent overlays
// shared with battles, and the scene-object loader of the interactions. The
// records, palettes and graphics stay in the cartridge.
//
// The routines of this file:
//   01:60FD  a visual record. A record number selects a 24-byte record in
//            bank 3. Its first bytes fill parts of the fifth object slot
//            (C880: its start, one byte after a gap, nine bytes from C894),
//            with C88E copied from C898 and C88D set to 1. Six bytes then describe an optional
//            overlay on the screen copy (source, destination, rows, cells per
//            row; bank 24); the next bytes fill the sixth slot (C8A0) and the
//            last one is stored in C2F1.
//   01:615A  the overlay: rows of two-byte cells copied into the screen
//            copy, a cell whose first byte is zero being transparent (the old
//            cell stays). Each destination row starts 64 bytes after the
//            previous one (one screen row); the source is consecutive.
//   01:617D  the scene object of an interaction (00:3BAF calls it with the
//            number from C2DC; zero means none). The eight object slots
//            (C800) and the eight enemy work records (C440) are cleared; a
//            cartridge record then gives eight colours, copied to both the
//            target palette (C258) and its working copy (C238), the first
//            object slot's fields, and the graphics, loaded to VRAM.
//
// How the code is cut. The clears and buffer copies are the original's
// LDIRs; each one is the shared resumable block transfer, registered at its
// own address, so a copy can stop between bytes at a frame or interrupt edge
// and resume with its count and both pointers. The zero byte that seeds each
// clear (01:618D, 01:619A) stays on the core (game/scenes.h,
// visual_scene_native_seeds). The segments here are the stretches between
// those copies. The addresses passed to the kit are the original's, because
// the kit reads each instruction's operands and cycle cost from the player's
// ROM; no ROM value is written here.
#include "game/scenes.h"
#include "game/routines.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace mk=menu_kit;
namespace {
using mk::Exit;

// The ways a segment ends. Each names the original's final instruction and
// charges its cycles; the core runs that instruction.
Exit call_at(std::uint16_t at) { return mk::continues(at,mk::call_cycles); }
Exit call_if_at(std::uint16_t at, bool taken) { return mk::continues(at,taken ? mk::call_cycles : mk::untaken_call_cycles); }
Exit jump_at(std::uint16_t at) { return mk::continues(at,mk::jump_cycles); }
Exit return_at(std::uint16_t at) { return mk::returns(at,mk::return_cycles); }
Exit return_taken_at(std::uint16_t at) { return mk::returns(at,mk::taken_conditional_return_cycles); }
// The instructions the core runs before a copy, by cost.
Exit load_pair_at(std::uint16_t at) { return mk::continues(at,10); }       // LD BC,nn (the copy's count)
Exit increment_pair_at(std::uint16_t at) { return mk::continues(at,6); }   // INC HL

// ---- A visual record: 01:60FD-6159 ---------------------------------------------------------------
// 01:60FD: the record number in A, times 24, into the record table. Three
// bytes go to C880, one is skipped, one more follows (LDI); nine go to C894.
// C898 is copied to C88E and C88D becomes 1. The overlay's six bytes are
// then read and shuffled: HL = the source, DE = the destination, B = cells
// per row, C = rows; the record pointer waits on the stack. Bank 24 goes
// into slot 2 (a mapper store), and the overlay is called only if B or C is
// nonzero (a conditional call, charged as taken or not).
template<class Z> Exit visual_record(Z& z) {
    z.copy(0x60FD,z.r.l,z.r.a);
    z.load_immediate(0x60FE,z.r.h);
    z.add_to_hl(0x6100,z.hl());                                       // number * 8
    z.add_to_hl(0x6101,z.hl());
    z.add_to_hl(0x6102,z.hl());
    z.copy(0x6103,z.r.e,z.r.l);
    z.copy(0x6104,z.r.d,z.r.h);
    z.add_to_hl(0x6105,z.hl());                                       // * 16 + * 8 = * 24
    z.add_to_hl(0x6106,z.de());
    z.load_de(0x6107);                                                // the record table
    z.add_to_hl(0x610A,z.de());
    z.load_de(0x610B);                                                // C880
    z.load_bc(0x610E);
    z.block_copy(0x6111);
    z.increment_pair(0x6113,z.r.d,z.r.e);                             // one byte skipped
    z.load_increment(0x6114);
    z.load_de(0x6116);                                                // C894
    z.load_bc(0x6119);
    z.block_copy(0x611C);
    z.load_a_direct(0x611E);                                          // C898
    z.store_a_direct(0x6121);                                         // C88E
    z.load_immediate(0x6124,z.r.a);
    z.store_a_direct(0x6126);                                         // C88D = 1
    z.load_from_hl(0x6129,z.r.c);                                     // source, low
    z.increment_hl(0x612A);
    z.load_from_hl(0x612B,z.r.b);                                     // source, high
    z.increment_hl(0x612C);
    z.load_from_hl(0x612D,z.r.e);                                     // destination
    z.increment_hl(0x612E);
    z.load_from_hl(0x612F,z.r.d);
    z.increment_hl(0x6130);
    z.load_from_hl(0x6131,z.r.a);                                     // rows
    z.increment_hl(0x6132);
    z.push(0x6133,z.r.h,z.r.l);                                       // the record pointer
    z.load_from_hl(0x6134,z.r.h);                                     // cells per row
    z.copy(0x6135,z.r.l,z.r.c);
    z.copy(0x6136,z.r.c,z.r.a);
    z.copy(0x6137,z.r.a,z.r.h);
    z.copy(0x6138,z.r.h,z.r.b);
    z.copy(0x6139,z.r.b,z.r.a);
    z.or_register(0x613A,z.r.c);                                      // zero: no overlay
    z.load_immediate(0x613B,z.r.a);
    z.store_a_direct(0x613D);                                         // bank 24, into slot 2
    return call_if_at(0x6140,!z.zero());                              // the overlay, 01:615A
}
// 01:6143: after the overlay. The record pointer comes back past the overlay
// bytes, bank 3 returns to slot 2, three bytes go to C8A0, one is skipped,
// one more follows, and the last byte goes to C2F1.
template<class Z> Exit visual_record_tail(Z& z) {
    z.pop(0x6143,z.r.h,z.r.l);
    z.increment_hl(0x6144);
    z.load_immediate(0x6145,z.r.a);
    z.store_a_direct(0x6147);                                         // bank 3, into slot 2
    z.load_de(0x614A);                                                // C8A0
    z.load_bc(0x614D);
    z.block_copy(0x6150);
    z.increment_pair(0x6152,z.r.d,z.r.e);
    z.load_increment(0x6153);
    z.load_from_hl(0x6155,z.r.a);
    z.store_a_direct(0x6156);                                         // C2F1
    return return_at(0x6159);
}

// ---- The overlay: 01:615A-617C --------------------------------------------------------------------
// One row: B cells. A cell whose first byte is zero is skipped on both sides
// (the old cell shows through); any other cell is copied with two LDIs. C was
// set to FFh, so the LDIs' count (BC) never borrows from B, the cell count.
// At the row's end the saved row start (DE) moves on by 64 bytes, the saved
// B and C come back and C counts the rows; the jump back to 01:615A for the
// next row ends the segment, so each row is a segment of its own.
template<class Z> Exit overlay_row(Z& z) {
    do {
        z.load_from_hl(0x615E,z.r.a);
        z.or_a(0x615F);
        if(z.jump_if(0x6160,z.zero())) {                              // transparent: skip it
            z.increment_hl(0x6176);
            z.increment_pair(0x6177,z.r.d,z.r.e);
            z.increment_hl(0x6178);
            z.increment_pair(0x6179,z.r.d,z.r.e);
            z.jump(0x617A);
        } else {                                                      // opaque: copy it
            z.load_increment(0x6163);
            z.load_increment(0x6165);
        }
    } while(z.decrement_jump(0x6167));
    z.pop(0x6169,z.r.d,z.r.e);                                        // the row start
    z.exchange_de_hl(0x616A);
    z.load_bc(0x616B);                                                // one screen row
    z.add_to_hl(0x616E,z.bc());
    z.exchange_de_hl(0x616F);
    z.pop(0x6170,z.r.b,z.r.c);                                        // cells per row, rows left
    z.decrement(0x6171,z.r.c);
    return mk::jump_if_exit(0x6172);
}
// 01:615A: a row starts. The cell count and row count, then the row start,
// are saved on the stack, and C = FFh.
template<class Z> Exit overlay_start(Z& z) {
    z.push(0x615A,z.r.b,z.r.c);
    z.push(0x615B,z.r.d,z.r.e);
    z.load_immediate(0x615C,z.r.c);
    return overlay_row(z);
}

// ---- The scene object: 01:617D-61F4 --------------------------------------------------------------
// 01:617D: no object (A zero) returns at once. Otherwise bank 3 goes into
// slot 2 and the clear of the object slots is set up; the core runs the
// count's load, the seed and the copy (01:618F).
template<class Z> Exit scene_object_start(Z& z) {
    z.or_a(0x617D);
    if(z.zero()) return return_taken_at(0x617E);
    z.return_not_taken(0x617E);
    z.load_hl(0x617F);                                                // the slot-2 bank register
    z.store_to_hl(0x6182);                                            // bank 3
    z.load_hl(0x6184);                                                // C800, the object slots
    z.load_de(0x6187);
    return load_pair_at(0x618A);
}
// 01:6191: after that copy, the clear of the enemy work records (C440); the
// copy is 01:619C.
template<class Z> Exit scene_object_clear_enemies(Z& z) {
    z.load_hl(0x6191);
    z.load_de(0x6194);
    return load_pair_at(0x6197);
}
// 01:619E: the object number, times 2, selects the object's entry; its first
// byte, times 8, selects eight colours. Both pointers wait on the stack: the
// colours are copied twice (01:61B9 to the target palette C258, 01:61C2 to
// the working copy C238).
template<class Z> Exit scene_object_colours(Z& z) {
    z.copy(0x619E,z.r.l,z.r.a);
    z.load_immediate(0x619F,z.r.h);
    z.add_to_hl(0x61A1,z.hl());                                       // number * 2
    z.load_de(0x61A2);                                                // the object table
    z.add_to_hl(0x61A5,z.de());
    z.load_from_hl(0x61A6,z.r.a);                                     // the colour set
    z.push(0x61A7,z.r.h,z.r.l);                                       // the entry, for later
    z.copy(0x61A8,z.r.l,z.r.a);
    z.load_immediate(0x61A9,z.r.h);
    z.add_to_hl(0x61AB,z.hl());                                       // set * 8
    z.add_to_hl(0x61AC,z.hl());
    z.add_to_hl(0x61AD,z.hl());
    z.load_de(0x61AE);                                                // the colour table
    z.add_to_hl(0x61B1,z.de());
    z.push(0x61B2,z.r.h,z.r.l);                                       // the colours, for the second copy
    z.load_de(0x61B3);                                                // C258
    return load_pair_at(0x61B6);
}
// 01:61BB: the same colours again, to the working copy.
template<class Z> Exit scene_object_colours_again(Z& z) {
    z.pop(0x61BB,z.r.h,z.r.l);
    z.load_de(0x61BC);                                                // C238
    return load_pair_at(0x61BF);
}
// 01:61C4: the entry's second byte, times 8, selects the first object
// slot's record; three bytes go to C800 (the copy 01:61D7).
template<class Z> Exit scene_object_slot(Z& z) {
    z.pop(0x61C4,z.r.h,z.r.l);                                        // the entry
    z.increment_hl(0x61C5);
    z.load_from_hl(0x61C6,z.r.a);
    z.copy(0x61C7,z.r.l,z.r.a);
    z.load_immediate(0x61C8,z.r.h);
    z.add_to_hl(0x61CA,z.hl());                                       // * 8
    z.add_to_hl(0x61CB,z.hl());
    z.add_to_hl(0x61CC,z.hl());
    z.load_de(0x61CD);                                                // the slot records
    z.add_to_hl(0x61D0,z.de());
    z.load_de(0x61D1);                                                // C800
    return load_pair_at(0x61D4);
}
// 01:61D9: one slot byte is skipped and one more copied (LDI). The INC HL at
// 01:61DC, which steps over the record's next byte, ends the segment; it is
// also an entry of its own (below).
template<class Z> Exit scene_object_slot_last(Z& z) {
    z.increment_pair(0x61D9,z.r.d,z.r.e);
    z.load_increment(0x61DA);
    return increment_pair_at(0x61DC);
}
// 01:61DD: the graphics: a bank byte (into slot 2, a mapper store, through
// B and A) and a pointer, loaded to their VRAM address by the graphics
// loader 00:03FA.
template<class Z> Exit scene_object_graphics(Z& z) {
    z.load_from_hl(0x61DD,z.r.b);                                     // the bank
    z.increment_hl(0x61DE);
    z.load_from_hl(0x61DF,z.r.a);                                     // the pointer
    z.increment_hl(0x61E0);
    z.load_from_hl(0x61E1,z.r.h);
    z.copy(0x61E2,z.r.l,z.r.a);
    z.copy(0x61E3,z.r.a,z.r.b);
    z.store_a_direct(0x61E4);                                         // into slot 2
    z.load_de(0x61E7);
    return call_at(0x61EA);
}
// 01:61DC: the same, from the INC HL that steps over the record's byte.
template<class Z> Exit scene_object_skip_and_graphics(Z& z) {
    z.increment_hl(0x61DC);
    return scene_object_graphics(z);
}
// 01:61F0: after the objects are updated, the frame wait with its request
// number in A (a tail jump).
template<class Z> Exit scene_object_frame_wait(Z& z) {
    z.load_immediate(0x61F0,z.r.a);
    return jump_at(0x61F2);
}
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, with its unit, exits and permissions. The
// stack an entry holds or inherits is the record pointer (60FD), the
// overlay's two saved words (615A) and the scene object's saved pointers.
void register_scene_visuals(RoutineRegistry& registry) {
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return mk::returned_at(at,stack); };
    auto C=[](std::uint16_t at, std::int8_t stack=0) { return mk::conditional_at(at,stack); };
    auto add=[&](std::uint16_t entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,RoutineEntry{1,entry},mk::segment(unit,std::move(exits),permissions),logic);
    };
    const RoutineEntry record{1,0x60FD}, overlay{1,0x615A}, scene{1,0x617D};

    // ---- a visual record and its overlay ----
    add(0x60FD,record,{F(0x6140,-2)},{.slot2_mapper=true,.scratch_stack=4},[](auto& z) -> Exit { return visual_record(z); });
    add(0x6143,record,{R(0x6159,2)},{.slot2_mapper=true,.inherited_stack=2},[](auto& z) -> Exit { return visual_record_tail(z); });  // after the overlay
    add(0x615A,overlay,{C(0x6172)},{.scratch_stack=4},[](auto& z) -> Exit { return overlay_start(z); });
    add(0x615E,overlay,{C(0x6172,4)},{.inherited_stack=4},[](auto& z) -> Exit { return overlay_row(z); });                    // a row resumed

    // ---- the scene object ----
    add(0x617D,scene,{R(0x617E),F(0x618A)},{.slot2_mapper=true},[](auto& z) -> Exit { return scene_object_start(z); });
    add(0x6191,scene,{F(0x6197)},{},[](auto& z) -> Exit { return scene_object_clear_enemies(z); });                         // after the slots' clear
    add(0x619E,scene,{F(0x61B6,-4)},{.scratch_stack=4},[](auto& z) -> Exit { return scene_object_colours(z); });            // after the records' clear
    add(0x61BB,scene,{F(0x61BF,2)},{.inherited_stack=4},[](auto& z) -> Exit { return scene_object_colours_again(z); });      // after the first colour copy
    add(0x61C4,scene,{F(0x61D4,2)},{.inherited_stack=2},[](auto& z) -> Exit { return scene_object_slot(z); });               // after the second colour copy
    add(0x61D9,scene,{F(0x61DC)},{},[](auto& z) -> Exit { return scene_object_slot_last(z); });                              // after the slot copy
    add(0x61DC,scene,{F(0x61EA)},{.slot2_mapper=true},[](auto& z) -> Exit { return scene_object_skip_and_graphics(z); });
    add(0x61DD,scene,{F(0x61EA)},{.slot2_mapper=true},[](auto& z) -> Exit { return scene_object_graphics(z); });
    // The five copies of the scene object, each the shared block transfer.
    // The two colour copies (01:61B9, 01:61C2) may hand their last stretch to
    // a native transfer that finishes across the frame edge, after a C++
    // part; that is the third argument. The other three are ordinary.
    register_block_transfer(registry,{1,0x618F},false);                                                                      // the object slots
    register_block_transfer(registry,{1,0x619C},false);                                                                      // the enemy work records
    register_block_transfer(registry,{1,0x61B9},true);                                                                       // the target palette
    register_block_transfer(registry,{1,0x61C2},true);                                                                       // the working palette
    register_block_transfer(registry,{1,0x61D7},false);                                                                      // the first object slot
    add(0x61F0,scene,{F(0x61F2)},{},[](auto& z) -> Exit { return scene_object_frame_wait(z); });                            // after the object update
}
}
