// SPDX-License-Identifier: GPL-3.0-or-later
// World map and towns (milestone 11), part entrances:
// Entrances, transfers and special ground: the tile interaction check
// 01:77AC, the transfers 01:7877 and 01:787B, the reset after a transfer
// 01:7908, the special ground 01:792A and its damage step 01:7A0C.
// Planned segments: map::entrance_segments in game/map.h, rendered with game/map_kit.h.
//
// What the code does, in the order the party meets it. Once per frame the map
// loop calls 01:77AC. It clears a one-shot guard while a fight or scene flag
// is up; otherwise, if the guard (C2D5) is clear, it sets it and asks the
// terrain part (01:7764) for the attributes of the tile under the party's
// feet. The upper nibble of that attribute is kept as the interaction type,
// bit 3 says "an entrance record may be here" and bit 2 "special ground".
// With a vehicle (C30E non-zero) three more tiles around the party are
// probed for the entrance bit; on foot with neither bit the camera is only
// remembered (C311, C313) for a later return.
//
// An entrance hit scans the area's entrance records (bank 3, six bytes each,
// ended by FFh) for the cell the probe tile lies in; a record with kind 08h
// enters a map (01:7877 then 01:787B: new area and location, camera, on
// foot), 0Ah a dungeon (the dungeon position and facing come from the record,
// the saved camera comes back), 0Ch a scene (mode 0Ch with the scene number
// and the saved camera). Every transfer ends in 01:7908, which clears the
// party's per-step block and all object slots, and then `pop hl; ret`: it
// throws away its caller's return address and returns one level higher, so
// the map loop's call of 01:77AC never continues after a transfer. For that
// reason every path that reaches 7908 is rendered inline here and ends with a
// return that consumes two inherited bytes ("R 7929 [+2]"): the segments end
// before the long object-table clearing (F 7921) and 01:7928, which pops and
// returns, resumes after it.
//
// Special ground (01:792A) is chosen by the tile's byte C2E5: 4Ch (a fixed
// spot of location 5 that enters a dungeon once a story flag is set), 5Eh (a
// scene on foot, skipped when the party already carries item 35h), 5Fh-63h
// (damage of 2 for each living character, except with transport value 8, after
// a short palette flash), ADh (a map change whose destination is found by the
// location number), C3h-C6h (a scene on foot) and C7h-D1h (damage of 30 after
// the flash, skipped when the party carries item 3Bh). A character brought to
// zero is marked in C2F0 and starts scene 00AEh. 01:7A0C is the damage step of
// one character.
#include "game/map.h"
#include "game/map_kit.h"
#include "game/routines.h"
#include <utility>

namespace psmodern::game {
namespace {
namespace mk=map_kit;
using mk::Exit;

constexpr std::uint16_t scan_after=512;     // cycles before a chunk of the record scan may end at 01:7844
constexpr std::uint16_t scan_head=0x7844;

// ---- The reset after a transfer: 01:7908-7929 --------------------------------------------------
// Remembers the transfer mark, clears the 24-byte step block and the 256-byte
// object table, then `pop hl; ret`. The second clearing (255 bytes copied, about
// 5,400 cycles) is not part of this segment: a call that included it always held a frame
// boundary on the recordings. The segment ends before it, at 01:7921 (`ld
// bc,nn`, which the next segment renders: a memory store is not allowed as a final
// instruction), and 01:7928 resumes after the copy.
constexpr std::uint16_t reset_fill=0x7921;
template<class Z> Exit reset_after_transfer(Z& z) {
    z.load_a_direct(0x7908);
    z.store_a_direct(0x790B);
    z.load_hl(0x790E);
    z.load_de(0x7911);
    z.load_bc(0x7914);
    z.store_to_hl(0x7917);
    z.block_copy(0x7919);
    z.load_hl(0x791B);
    z.load_de(0x791E);
    z.load_bc(reset_fill);
    return mk::fall_through(reset_fill,0x7924);  // the fill and the copy follow
}
// 01:7928: after the copy, the caller's return address is discarded and the
// return goes one level higher.
template<class Z> Exit reset_return(Z& z) {
    z.pop(0x7928,z.r.h,z.r.l);
    return z.final_return(0x7929);
}

// ---- The transfers: 01:7877-78BA, 78BD-78E5, 78E5-7905 ------------------------------------------
// 01:787B: HL points at an entrance record's target (area/location, then the
// camera cell). The camera Y and X are derived from it and the party is put on foot.
template<class Z> Exit transfer_to_map(Z& z) {
    auto& r=z.r;
    z.load_from_hl(0x787B,r.a);
    z.store_a_direct(0x787C);
    z.store_a_direct(0x787F);
    z.increment_hl(0x7882);
    z.load_from_hl(0x7883,r.e);
    z.load_immediate(0x7884,r.d);
    z.exchange_de_hl(0x7886);
    for (std::uint16_t i=0;i<4;++i) z.add_to_hl(static_cast<std::uint16_t>(0x7887+i),z.hl());
    z.copy(0x788B,r.a,r.l);
    z.subtract_immediate(0x788C);
    bool adjust=true;
    if (!z.relative_if(0x788E,z.carry())) {
        z.compare_immediate(0x7890);
        adjust=!z.relative_if(0x7892,z.carry());
    }
    if (adjust) {
        z.subtract_immediate(0x7894);
        z.decrement(0x7896,r.h);
    }
    z.copy(0x7897,r.l,r.a);
    z.copy(0x7898,r.a,r.h);
    z.and_immediate(0x7899);
    z.copy(0x789B,r.h,r.a);
    z.store_hl_direct(0x789C);
    z.store_hl_direct(0x789F);
    z.exchange_de_hl(0x78A2);
    z.increment_hl(0x78A3);
    z.load_from_hl(0x78A4,r.a);
    z.subtract_immediate(0x78A5);
    z.and_immediate(0x78A7);
    z.copy(0x78A9,r.l,r.a);
    z.load_immediate(0x78AA,r.h);
    for (std::uint16_t i=0;i<4;++i) z.add_to_hl(static_cast<std::uint16_t>(0x78AC+i),z.hl());
    z.store_hl_direct(0x78B0);
    z.store_hl_direct(0x78B3);
    z.xor_a(0x78B6);
    z.store_a_direct(0x78B7);
    (void)z.jump(0x78BA);
    return reset_after_transfer(z);
}
// 01:7877: the mode byte is A (8 from the scan, from 01:79B4, or from the
// dungeon's exit), then the transfer of 01:787B.
template<class Z> Exit mark_and_transfer(Z& z) {
    z.store_a_direct(0x7877);
    z.increment_hl(0x787A);
    return transfer_to_map(z);
}
// 01:78E5: a scene record (kind 0Ch); any other kind leaves.
template<class Z> Exit scene_record(Z& z) {
    auto& r=z.r;
    z.compare_immediate(0x78E5);
    if (auto exit=z.final_return_if(0x78E7,mk::Condition::nz)) return *exit;
    z.store_a_direct(0x78E8);
    z.increment_hl(0x78EB);
    z.load_from_hl(0x78EC,r.a);
    z.store_a_direct(0x78ED);
    z.increment_hl(0x78F0);
    z.load_from_hl(0x78F1,r.a);
    z.increment_hl(0x78F2);
    z.load_from_hl(0x78F3,r.h);
    z.copy(0x78F4,r.l,r.a);
    z.store_hl_direct(0x78F5);
    z.xor_a(0x78F8);
    z.store_a_direct(0x78F9);
    z.load_hl_direct(0x78FC);
    z.store_hl_direct(0x78FF);
    z.load_hl_direct(0x7902);
    z.store_hl_direct(0x7905);
    return reset_after_transfer(z);              // falls into 7908
}
// 01:78BD: a dungeon record (kind 0Ah), else the scene kind.
template<class Z> Exit dungeon_record(Z& z) {
    auto& r=z.r;
    z.compare_immediate(0x78BD);
    if (z.jump_if(0x78BF,!z.zero())) return scene_record(z);
    z.store_a_direct(0x78C2);
    z.increment_hl(0x78C5);
    z.load_from_hl(0x78C6,r.d);
    z.increment_hl(0x78C7);
    z.load_from_hl(0x78C8,r.e);
    z.store_pair_direct(0x78C9,r.d,r.e);
    z.increment_hl(0x78CD);
    z.load_from_hl(0x78CE,r.a);
    z.store_a_direct(0x78CF);
    z.load_hl_direct(0x78D2);
    z.store_hl_direct(0x78D5);
    z.load_hl_direct(0x78D8);
    z.store_hl_direct(0x78DB);
    z.xor_a(0x78DE);
    z.store_a_direct(0x78DF);
    (void)z.jump(0x78E2);
    return reset_after_transfer(z);
}

// ---- The entrance-record scan: 01:7810-786E -----------------------------------------------------
// The record kind is tested at 786E-7874; kind 08h goes to the map transfer.
template<class Z> Exit record_found(Z& z) {
    auto& r=z.r;
    z.pop(0x786E,r.h,r.l);
    z.increment_hl(0x786F);
    z.increment_hl(0x7870);
    z.load_from_hl(0x7871,r.a);
    z.compare_immediate(0x7872);
    if (z.jump_if(0x7874,!z.zero())) return dungeon_record(z);
    return mark_and_transfer(z);
}
// The loop of 01:7844-786B. `resumed` is true at the entry 01:7845: the segment
// before has just rendered the `ld a,(hl)` at the loop head. Every arrival at the head with
// scan_after cycles elapsed ends the chunk there (a loop exit on that plain load).
template<class Z> Exit scan_records(Z& z, bool resumed) {
    auto& r=z.r;
    for (;;) {
        if (!resumed) {
            const bool chunk_end=z.loop_exit(scan_after);
            z.load_from_hl(0x7844,r.a);
            if (chunk_end) return mk::fall_through(scan_head,0x7845);
        }
        resumed=false;
        z.compare_immediate(0x7845);
        if (auto exit=z.final_return_if(0x7847,mk::Condition::z)) return *exit;   // the FFh terminator
        z.push(0x7848,r.h,r.l);
        z.load_from_hl(0x7849,r.a);
        z.compare_register(0x784A,r.d);
        bool next=false;
        if (!z.relative_if(0x784B,z.zero())) {       // not the same high nibble: try the neighbouring one
            z.increment(0x784D,r.a);
            z.copy(0x784E,r.b,r.a);
            z.and_immediate(0x784F);
            z.compare_immediate(0x7851);
            z.copy(0x7853,r.a,r.b);
            if (!z.relative_if(0x7854,z.carry())) {
                z.add_immediate(0x7856);
                z.and_immediate(0x7858);
            }
            z.compare_register(0x785A,r.d);
            next=z.relative_if(0x785B,!z.zero());
        }
        if (!next) {
            z.increment_hl(0x785D);
            z.load_from_hl(0x785E,r.a);
            z.compare_register(0x785F,r.e);
            if (z.relative_if(0x7860,z.zero())) return record_found(z);
            z.increment(0x7862,r.a);
            z.compare_register(0x7863,r.e);
            if (z.relative_if(0x7864,z.zero())) return record_found(z);
        }
        z.pop(0x7866,r.h,r.l);
        z.load_bc(0x7867);
        z.add_to_hl(0x786A,z.bc());
        (void)z.jump(0x786B);
    }
}
// 01:7810: the cell the probe tile (E = probe offset) lies in, as a record key
// (D = camera page nibble, E = column byte), then the scan of the record table
// of the area, with bank 3 in slot 2.
template<class Z> Exit scan_entrances(Z& z) {
    auto& r=z.r;
    z.copy(0x7810,r.a,r.e);
    z.load_hl(0x7811);
    z.add_register(0x7814,r.l);
    z.copy(0x7815,r.l,r.a);
    z.add_with_carry(0x7816,r.h);
    z.subtract_register(0x7817,r.l);
    z.copy(0x7818,r.h,r.a);
    z.load_pair_direct(0x7819,r.d,r.e);
    z.copy(0x781D,r.a,r.e);
    z.add_from_hl(0x781E);
    bool adjust=z.relative_if(0x781F,z.carry());
    if (!adjust) {
        z.compare_immediate(0x7821);
        adjust=!z.relative_if(0x7823,z.carry());
    }
    if (adjust) {
        z.add_immediate(0x7825);
        z.increment(0x7827,r.d);
    }
    z.copy(0x7828,r.e,r.a);
    z.exchange_de_hl(0x7829);
    for (std::uint16_t i=0;i<4;++i) z.add_to_hl(static_cast<std::uint16_t>(0x782A+i),z.hl());
    z.exchange_de_hl(0x782E);
    z.increment_hl(0x782F);
    z.load_from_hl(0x7830,r.c);
    z.load_immediate(0x7831,r.b);
    z.load_hl_direct(0x7833);
    z.add_to_hl(0x7836,z.bc());
    for (std::uint16_t i=0;i<4;++i) z.add_to_hl(static_cast<std::uint16_t>(0x7837+i),z.hl());
    z.copy(0x783B,r.e,r.h);
    z.load_hl(0x783C);
    z.store_to_hl(0x783F);                       // $FFFF: bank 3 into slot 2
    z.load_hl_direct(0x7841);
    return scan_records(z,false);
}

// ---- The tile interaction check: 01:77AC-780F ---------------------------------------------------
// 01:7803: nothing to do here; remember the camera for a later return.
template<class Z> Exit remember_camera(Z& z) {
    z.load_hl_direct(0x7803);
    z.store_hl_direct(0x7806);
    z.load_hl_direct(0x7809);
    z.store_hl_direct(0x780C);
    return z.final_return(0x780F);
}
template<class Z> Exit tile_interaction(Z& z) {      // 01:77AC
    auto& r=z.r;
    z.load_hl(0x77AC);
    z.load_a_direct(0x77AF);
    z.or_a(0x77B2);
    if (!z.jump_if(0x77B3,z.zero())) {
        z.store_to_hl(0x77B6);
        return z.final_return(0x77B8);
    }
    z.load_from_hl(0x77B9,r.a);
    z.or_a(0x77BA);
    if (auto exit=z.final_return_if(0x77BB,mk::Condition::nz)) return *exit;
    z.store_to_hl(0x77BC);
    z.load_immediate(0x77BE,r.a);
    z.copy(0x77C0,r.e,r.a);
    return z.final_call(0x77C1);                 // the attributes of the tile ahead
}
template<class Z> Exit after_first_probe(Z& z) {     // 01:77C4
    auto& r=z.r;
    z.copy(0x77C4,r.b,r.a);
    for (std::uint16_t i=0;i<4;++i) z.rotate_right_circular_a(static_cast<std::uint16_t>(0x77C5+i));
    z.and_immediate(0x77C9);
    z.store_a_direct(0x77CB);                    // the interaction type
    z.copy(0x77CE,r.a,r.b);
    z.and_immediate(0x77CF);
    if (z.relative_if(0x77D1,!z.zero())) return scan_entrances(z);
    z.copy(0x77D3,r.a,r.b);
    z.and_immediate(0x77D4);
    return z.final_jump_if(0x77D6,mk::Condition::nz);   // to the special ground, else on
}
template<class Z> Exit vehicle_probes(Z& z) {        // 01:77D9
    auto& r=z.r;
    z.load_a_direct(0x77D9);
    z.or_a(0x77DC);
    if (z.relative_if(0x77DD,z.zero())) return remember_camera(z);
    z.load_immediate(0x77DF,r.a);
    z.copy(0x77E1,r.e,r.a);
    return z.final_call(0x77E2);
}
// 01:77E5, 77EF, 77F9: the result of a vehicle probe; an entrance bit goes to
// the scan, else the next probe tile is asked for (`value`).
template<class Z> Exit next_probe(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.and_immediate(at);
    if (z.relative_if(static_cast<std::uint16_t>(at+2),!z.zero())) return scan_entrances(z);
    z.load_immediate(static_cast<std::uint16_t>(at+4),r.a);
    z.copy(static_cast<std::uint16_t>(at+6),r.e,r.a);
    return z.final_call(static_cast<std::uint16_t>(at+7));
}

// ---- The special ground: 01:792A-7A20 ----------------------------------------------------------
// The common tail of the scene tiles: mode 0Ch, saved camera back, reset.
template<class Z> Exit scene_tail(Z& z) {            // 01:7972
    z.load_immediate(0x7972,z.r.a);
    z.store_a_direct(0x7974);
    z.load_hl_direct(0x7977);
    z.store_hl_direct(0x797A);
    z.load_hl_direct(0x797D);
    z.store_hl_direct(0x7980);
    (void)z.jump(0x7983);
    return reset_after_transfer(z);
}
// 01:79E2: the damage step for the four party slots; the work is in 01:7A0C (a separate segment, called).
template<class Z> Exit damage_first(Z& z) {          // 01:79E2
    auto& r=z.r;
    z.load_immediate(0x79E2,r.b);
    z.load_immediate(0x79E4,r.a);
    return z.final_call(0x79E6);
}
template<class Z> Exit damage_next(Z& z, std::uint16_t at) {   // 01:79E9, 79EE, 79F3
    z.load_immediate(at,z.r.a);
    return z.final_call(static_cast<std::uint16_t>(at+2));
}
template<class Z> Exit damage_end(Z& z) {            // 01:79F8
    auto& r=z.r;
    z.copy(0x79F8,r.a,r.b);
    z.or_a(0x79F9);
    if (auto exit=z.final_return_if(0x79FA,mk::Condition::z)) return *exit;
    z.store_a_direct(0x79FB);
    z.load_hl(0x79FE);
    z.store_hl_direct(0x7A01);
    z.load_immediate(0x7A04,r.a);
    z.store_a_direct(0x7A06);
    (void)z.jump(0x7A09);
    return reset_after_transfer(z);
}
template<class Z> Exit tile_79D1(Z& z) {            // C7h-D1h
    auto& r=z.r;
    z.compare_immediate(0x79D1);
    if (auto exit=z.final_return_if(0x79D3,mk::Condition::c)) return *exit;
    z.compare_immediate(0x79D4);
    if (auto exit=z.final_return_if(0x79D6,mk::Condition::nc)) return *exit;
    z.load_immediate(0x79D7,r.a);
    return z.final_call(0x79D9);                 // the inventory slot search
}
template<class Z> Exit tile_79B7(Z& z) {            // C3h-C6h
    auto& r=z.r;
    z.compare_immediate(0x79B7);
    if (auto exit=z.final_return_if(0x79B9,mk::Condition::c)) return *exit;
    z.compare_immediate(0x79BA);
    if (z.jump_if(0x79BC,!z.carry())) return tile_79D1(z);
    z.load_a_direct(0x79BF);
    z.or_a(0x79C2);
    if (auto exit=z.final_return_if(0x79C3,mk::Condition::nz)) return *exit;
    z.load_immediate(0x79C4,r.a);
    z.store_a_direct(0x79C6);
    z.load_immediate(0x79C9,r.a);
    z.store_a_direct(0x79CB);
    (void)z.jump(0x79CE);
    return scene_tail(z);
}
template<class Z> Exit tile_799C(Z& z) {            // ADh
    auto& r=z.r;
    z.compare_immediate(0x799C);
    if (z.jump_if(0x799E,!z.zero())) return tile_79B7(z);
    z.load_a_direct(0x79A1);
    z.subtract_immediate(0x79A4);
    if (auto exit=z.final_return_if(0x79A6,mk::Condition::c)) return *exit;
    z.copy(0x79A7,r.l,r.a);
    z.load_immediate(0x79A8,r.h);
    z.copy(0x79AA,r.e,r.l);
    z.copy(0x79AB,r.d,r.h);
    z.add_to_hl(0x79AC,z.hl());
    z.add_to_hl(0x79AD,z.de());
    z.load_de(0x79AE);
    z.add_to_hl(0x79B1,z.de());
    z.load_immediate(0x79B2,r.a);
    (void)z.jump(0x79B4);
    return mark_and_transfer(z);
}
template<class Z> Exit tile_7986(Z& z) {            // 5Fh-63h
    z.compare_immediate(0x7986);
    if (auto exit=z.final_return_if(0x7988,mk::Condition::c)) return *exit;
    z.compare_immediate(0x7989);
    if (z.jump_if(0x798B,!z.carry())) return tile_799C(z);
    z.load_a_direct(0x798E);
    z.compare_immediate(0x7991);
    if (auto exit=z.final_return_if(0x7993,mk::Condition::z)) return *exit;
    return z.final_call(0x7994);                 // the magic flash
}
template<class Z> Exit tile_795C(Z& z) {            // 5Eh
    auto& r=z.r;
    z.compare_immediate(0x795C);
    if (z.jump_if(0x795E,!z.zero())) return tile_7986(z);
    z.load_a_direct(0x7961);
    z.or_a(0x7964);
    if (auto exit=z.final_return_if(0x7965,mk::Condition::nz)) return *exit;
    z.load_immediate(0x7966,r.a);
    return z.final_call(0x7968);                 // the inventory slot search
}
template<class Z> Exit special_ground(Z& z) {        // 01:792A
    auto& r=z.r;
    z.load_a_direct(0x792A);
    z.compare_immediate(0x792D);
    if (z.jump_if(0x792F,!z.zero())) return tile_795C(z);
    z.load_a_direct(0x7932);
    z.compare_immediate(0x7935);
    if (auto exit=z.final_return_if(0x7937,mk::Condition::nz)) return *exit;
    z.load_a_direct(0x7938);
    z.or_a(0x793B);
    if (auto exit=z.final_return_if(0x793C,mk::Condition::z)) return *exit;
    z.load_immediate(0x793D,r.a);
    z.store_a_direct(0x793F);
    z.load_hl(0x7942);
    z.store_hl_direct(0x7945);
    z.load_immediate(0x7948,r.a);
    z.store_a_direct(0x794A);
    z.load_hl_direct(0x794D);
    z.store_hl_direct(0x7950);
    z.load_hl_direct(0x7953);
    z.store_hl_direct(0x7956);
    (void)z.jump(0x7959);
    return reset_after_transfer(z);
}
template<class Z> Exit after_chest_slot(Z& z) {      // 01:796B
    if (auto exit=z.final_return_if(0x796B,mk::Condition::z)) return *exit;
    z.load_hl(0x796C);
    z.store_hl_direct(0x796F);
    return scene_tail(z);
}
template<class Z> Exit after_damage_tile_probe(Z& z) {   // 01:79DC
    if (auto exit=z.final_return_if(0x79DC,mk::Condition::z)) return *exit;
    return z.final_call(0x79DD);                 // the magic flash
}

// ---- The damage step: 01:7A0C-7A20 -------------------------------------------------------------
// After `call IsCharacterAlive` (a separate segment) HL points at the character's hit
// points: unless the character is dead, C is taken off the low byte (never
// below zero); a character brought to zero also has the high byte cleared; B
// collects one bit per character, shifted in from the carry (set when the
// character just fell).
template<class Z> Exit damage_step(Z& z) {          // 01:7A0F
    auto& r=z.r;
    if (!z.relative_if(0x7A0F,z.zero())) {
        z.increment_hl(0x7A11);
        z.load_from_hl(0x7A12,r.a);
        z.subtract_register(0x7A13,r.c);
        if (!z.relative_if(0x7A14,!z.carry())) z.xor_a(0x7A16);
        z.store_register_to_hl(0x7A17,r.a);
        if (!z.relative_if(0x7A18,!z.zero())) {
            z.decrement_hl(0x7A1A);
            z.store_register_to_hl(0x7A1B,r.a);
            z.subtract_immediate(0x7A1C);
        }
    }
    z.rotate_left_through_carry(0x7A1E,r.b);
    return z.final_return(0x7A20);
}

// ---- Registration -------------------------------------------------------------------------------
template<class Logic> void add(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit,
                               std::vector<SegmentExit> exits, mk::Permissions permissions, Logic logic) {
    mk::add_segment(registry,entry,mk::segment(unit,std::move(exits),permissions),std::move(logic));
}
// The exits the scan shares: the chunk end at the loop head, the end of the
// table, a record of another kind, and the return past the reset.
std::vector<SegmentExit> scan_exits(std::vector<SegmentExit> before) {
    before.insert(before.end(),{mk::loop_at(scan_head,scan_after),mk::returned_at(0x7847),mk::returned_at(0x78E7),
                                mk::core_final_at(0x7921)});
    return before;
}
}

void register_map_entrances(RoutineRegistry& registry) {
    namespace m=map;
    using mk::returned_at; using mk::core_final_at; using mk::conditional_at;
    const auto tile=m::tile_interaction_unit;
    const auto special=m::special_ground_unit;
    const mk::Permissions scan{.slot2_mapper=true,.scratch_stack=2};
    const mk::Permissions inherited{.inherited_stack=2};   // only 01:7928 pops the caller's return address

    // 01:77AC: the per-frame check.
    add(registry,tile,tile,{returned_at(0x77B8),returned_at(0x77BB),core_final_at(0x77C1)},{},
        [](auto& z) -> Exit { return tile_interaction(z); });
    add(registry,m::at1(0x77C4),tile,scan_exits({conditional_at(0x77D6)}),scan,
        [](auto& z) -> Exit { return after_first_probe(z); });
    add(registry,m::at1(0x77D9),tile,{core_final_at(0x77E2),returned_at(0x780F)},{},
        [](auto& z) -> Exit { return vehicle_probes(z); });
    add(registry,m::at1(0x77E5),tile,scan_exits({core_final_at(0x77EC)}),scan,
        [](auto& z) -> Exit { return next_probe(z,0x77E5); });
    add(registry,m::at1(0x77EF),tile,scan_exits({core_final_at(0x77F6)}),scan,
        [](auto& z) -> Exit { return next_probe(z,0x77EF); });
    add(registry,m::at1(0x77F9),tile,scan_exits({core_final_at(0x7800)}),scan,
        [](auto& z) -> Exit { return next_probe(z,0x77F9); });
    add(registry,m::at1(0x7803),tile,{returned_at(0x780F)},{},
        [](auto& z) -> Exit { return remember_camera(z); });
    add(registry,m::at1(0x7845),tile,{mk::loop_at(scan_head,scan_after),returned_at(0x7847),returned_at(0x78E7),core_final_at(0x7921)},
        {.scratch_stack=2},[](auto& z) -> Exit { return scan_records(z,true); });

    // The transfers.
    add(registry,m::dungeon_exit_transfer_unit,m::dungeon_exit_transfer_unit,{core_final_at(0x7921)},{},
        [](auto& z) -> Exit { return mark_and_transfer(z); });
    add(registry,m::transfer_unit,m::transfer_unit,{core_final_at(0x7921)},{},
        [](auto& z) -> Exit { return transfer_to_map(z); });
    add(registry,m::reset_after_transfer_unit,m::reset_after_transfer_unit,{core_final_at(0x7921)},{},
        [](auto& z) -> Exit { return reset_after_transfer(z); });
    add(registry,m::at1(0x7928),m::reset_after_transfer_unit,{returned_at(0x7929,2)},inherited,
        [](auto& z) -> Exit { return reset_return(z); });

    // 01:792A: the special ground.
    add(registry,special,special,
        {returned_at(0x7937),returned_at(0x793C),returned_at(0x7965),returned_at(0x7988),returned_at(0x7993),
         returned_at(0x79A6),returned_at(0x79B9),returned_at(0x79C3),returned_at(0x79D3),returned_at(0x79D6),
         core_final_at(0x7968),core_final_at(0x7994),core_final_at(0x79D9),core_final_at(0x7921)},{},
        [](auto& z) -> Exit { return special_ground(z); });
    add(registry,m::at1(0x796B),special,{returned_at(0x796B),core_final_at(0x7921)},{},
        [](auto& z) -> Exit { return after_chest_slot(z); });
    add(registry,m::at1(0x7997),special,{core_final_at(0x79E6)},{},
        [](auto& z) -> Exit { z.load_immediate(0x7997,z.r.c); (void)z.jump(0x7999); return damage_first(z); });
    add(registry,m::at1(0x79DC),special,{returned_at(0x79DC),core_final_at(0x79DD)},{},
        [](auto& z) -> Exit { return after_damage_tile_probe(z); });
    add(registry,m::at1(0x79E0),special,{core_final_at(0x79E6)},{},
        [](auto& z) -> Exit { z.load_immediate(0x79E0,z.r.c); return damage_first(z); });
    add(registry,m::at1(0x79E9),special,{core_final_at(0x79EB)},{},
        [](auto& z) -> Exit { return damage_next(z,0x79E9); });
    add(registry,m::at1(0x79EE),special,{core_final_at(0x79F0)},{},
        [](auto& z) -> Exit { return damage_next(z,0x79EE); });
    add(registry,m::at1(0x79F3),special,{core_final_at(0x79F5)},{},
        [](auto& z) -> Exit { return damage_next(z,0x79F3); });
    add(registry,m::at1(0x79F8),special,{returned_at(0x79FA),core_final_at(0x7921)},{},
        [](auto& z) -> Exit { return damage_end(z); });
    add(registry,m::at1(0x7A0F),m::ground_damage_unit,{returned_at(0x7A20)},{},
        [](auto& z) -> Exit { return damage_step(z); });

    // Joining stretches: the fill of the object table starts with its seed
    // store after the copy count is loaded, and the damage step's first
    // instruction asks whether the character is alive.
    {
        auto spec=mk::segment(m::reset_after_transfer_unit,{core_final_at(0x7924)});
        spec.single_instruction=true;
        mk::add_segment(registry,m::at1(0x7924),std::move(spec),[](auto& z) -> Exit {
            z.store_to_hl(0x7924);
            return mk::fall_through(0x7924,0x7926);
        });
        auto alive=mk::segment(m::ground_damage_unit,{core_final_at(0x7A0C)},{.scratch_stack=2});
        alive.single_instruction=true;
        mk::add_segment(registry,m::at1(0x7A0C),std::move(alive),[](auto& z) -> Exit { return z.final_call(0x7A0C); });
    }
}
}
