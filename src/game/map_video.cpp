// SPDX-License-Identifier: GPL-3.0-or-later
// World map and towns (milestone 11), part video:
// The picture's work in the frame interrupt: the map's frame handler 00:0235 and
// the loading one 00:029F, the sprite table upload 01:588B and its unrolled
// tails, the party frame tiles 01:61F5 and their kinds, the tile animators
// 01:62BC, the strip upload 01:73D0 and the colour cycle 01:7C18.
// Planned segments: map::video_segments in game/map.h, rendered with game/map_kit.h.
//
// Every one of these runs inside the frame interrupt with interrupts off (the
// map loader also calls 01:61F5 between a DI and an EI), so the work is mostly
// port output: VDP register writes for the scroll, the sprite attribute table,
// pattern tiles and name-table strips. Each port write happens at the T-state
// of the original OUT or OUTI (the kit times them), so the VDP sees the same
// writes at the same cycles. Calls between these units end a segment (bare
// calls are exits; the callee is another registered segment); the unrolled OUTI tails 01:589E, 01:591E and
// 01:595E are rendered inline where a unit calls or jumps into them, and the
// VDP address helper behind RST 08 is rendered inline everywhere.
#include "game/map.h"
#include "game/map_kit.h"
#include "game/routines.h"
#include <stdexcept>
#include <type_traits>

namespace psmodern::game {
namespace {
namespace mk=game::map_kit;
using mk::Exit;
constexpr RoutineEntry at0(std::uint16_t address) { return {0,address}; }
constexpr RoutineEntry at1(std::uint16_t address) { return {1,address}; }

// ---- The unrolled upload tail (01:589E-01:599E) --------------------------------------------
// 128 OUTI from 01:589E, 64 from 01:591E, 32 from 01:595E, one RET at 01:599E.
constexpr std::uint16_t tail_return=0x599E;
template<class Z> void upload_tail(Z& z, std::uint16_t from) {
    z.output_increment_run(from,static_cast<unsigned>((tail_return-from)/2));
}
// A bare `call` at `at` into the tail at `from`, rendered with its return.
template<class Z> void call_tail(Z& z, std::uint16_t at, std::uint16_t from) {
    (void)z.inline_call(at,tail_return,[from](auto& callee) { upload_tail(callee,from); });
}
// A `jp` at `at` into the tail: the tail's RET is the segment's exit. The
// target is the operand read through the bus; where a push on the mapper
// registers has switched slot 1 away (a stack the dispatch refuses anyway, but
// whose bound is estimated first) it is not the tail: an explicit guard (the
// estimate gives up, the live run stops). The jump operand is read from the
// cartridge, so the guard holds across a delivery inside the body.
template<class Z> Exit jump_tail(Z& z, std::uint16_t at) {
    const auto from=z.jump(at);
    if (!z.guard(from>=0x589E && from<=tail_return && (from-0x589E)%2==0,"a registered video segment jumped outside the upload tail"))
        return z.final_return(tail_return);
    upload_tail(z,from);
    return z.final_return(tail_return);
}

// ---- 00:0235 and 00:029F: the scroll registers ------------------------------------------------
// VDP register 8 takes the horizontal scroll (C300h), register 9 the vertical
// one (C304h); then the sprite table upload is called.
template<class Z> Exit scroll_registers(Z& z, std::uint16_t at) {
    map_kit::write_scroll_registers(z,at);
    return z.final_call(static_cast<std::uint16_t>(at+18));
}

// ---- 00:02B4: the loading handler's palette and name table -----------------------------------
// 32 bytes from C220h to CRAM (through 01:595E), then 7 x 256 bytes of the
// name-table mirror D000h to VRAM 3800h. The long OUTI loop (about 46,600
// cycles) always runs across the frame boundary; a segment must end before the
// frame limit, at the next entry (one OUTI and its JP NZ), so the loop is cut at its inner
// JP NZ (00:02D2) once 128 cycles have passed.
constexpr std::uint16_t name_table_after=128;
template<class Z> Exit name_table_loop(Z& z, bool at_count) {
    auto& r=z.r;
    for (;;) {
        if (!at_count) {
            z.output_increment(0x02D0);
            if (z.loop_exit(name_table_after)) return z.final_jump_if(0x02D2,mk::Condition::nz);
            if (z.jump_if(0x02D2,!z.zero())) continue;
        }
        at_count=false;
        z.decrement(0x02D5,r.a);
        if (z.jump_if(0x02D6,!z.zero())) continue;
        return z.final_call(0x02D9);
    }
}
template<class Z> Exit loading_upload(Z& z) {
    auto& r=z.r;
    z.load_hl(0x02B4);
    z.load_de(0x02B7);
    (void)z.restart_vdp_address(0x02BA);
    z.load_immediate(0x02BB,r.c);
    call_tail(z,0x02BD,0x595E);
    z.load_hl(0x02C0);
    z.xor_a(0x02C3);
    z.output(0x02C4);
    z.load_immediate(0x02C6,r.a);
    z.output(0x02C8);
    z.load_immediate(0x02CA,r.c);
    z.load_immediate(0x02CC,r.a);
    z.load_immediate(0x02CE,r.b);
    return name_table_loop(z,false);
}

// ---- 01:588B: the sprite attribute table ----------------------------------------------------
// 64 Y positions from C900h to VRAM 3F00h, then 128 X/tile bytes from C980h
// to VRAM 3F80h. Always 3,258 cycles with its return.
template<class Z> Exit sprite_upload(Z& z) {
    auto& r=z.r;
    z.load_hl(0x588B);
    z.load_de(0x588E);
    (void)z.restart_vdp_address(0x5891);
    z.load_immediate(0x5892,r.c);
    call_tail(z,0x5894,0x591E);
    z.load_hl(0x5897);
    z.load_de(0x589A);
    (void)z.restart_vdp_address(0x589D);   // the helper returns into 01:589E
    upload_tail(z,0x589E);
    return z.final_return(tail_return);
}

// ---- 01:61F5: the party frame tiles ---------------------------------------------------------
// The first four object slots (C800h, 20h bytes each): a slot whose frame
// (+10h) differs from the one last uploaded (+11h) records it, and when the
// slot's kind (+1) is not zero jumps through the table at 01:621D (00:00E6)
// to that kind's upload, which returns straight to the frame handler. So at
// most one slot's tiles are uploaded per call; the others wait.
template<class Z> Exit party_slots(Z& z, bool at_next) {
    auto& r=z.r;
    for (;;) {
        if (!at_next) {
            z.load_from_indexed(0x6200,mk::Index::ix,r.a);
            z.compare_indexed(0x6203,mk::Index::ix);
            if (!z.jump_if(0x6206,z.zero())) {
                z.store_to_indexed(0x6209,mk::Index::ix,r.a);
                z.copy(0x620C,r.d,r.a);
                z.load_from_indexed(0x620D,mk::Index::ix,r.a);
                z.or_a(0x6210);
                z.load_hl(0x6211);
                return z.final_jump_if(0x6214,mk::Condition::nz);
            }
        }
        at_next=false;
        z.load_de(0x6217);
        z.add_to_index(0x621A,mk::Index::ix,z.de());
        if (z.decrement_jump(0x621C)) continue;
        return z.final_return(0x621E);
    }
}
template<class Z> Exit party_frames(Z& z) {
    z.load_hl(0x61F5);
    z.store_to_hl(0x61F8);                  // slot 2: the party's tile bank
    z.load_index(0x61FA,mk::Index::ix);
    z.load_immediate(0x61FE,z.r.b);
    return party_slots(z,false);
}
// 01:6229, 01:6246, 01:6263: a 192-byte frame (D x C0h) of one bank-28
// sheet, 128 bytes through 01:589E and 64 through 01:591E.
template<class Z> Exit frame_192(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.load_immediate(at,r.e);
    z.shift_right_logical(at+2,r.d);
    z.rotate_right_through_carry(at+4,r.e);
    z.copy(at+6,r.l,r.e);
    z.copy(at+7,r.h,r.d);
    z.shift_right_logical(at+8,r.d);
    z.rotate_right_through_carry(at+10,r.e);
    z.add_to_hl(at+12,z.de());
    z.load_de(at+13);
    z.add_to_hl(at+16,z.de());
    z.load_de(at+17);
    (void)z.restart_vdp_address(at+20);
    z.load_immediate(at+21,r.c);
    call_tail(z,at+23,0x589E);
    return jump_tail(z,at+26);
}
// 01:6280: a 128-byte frame (D x 80h).
template<class Z> Exit frame_128(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x6280,r.e);
    z.shift_right_logical(0x6282,r.d);
    z.rotate_right_through_carry(0x6284,r.e);
    z.load_hl(0x6286);
    z.add_to_hl(0x6289,z.de());
    z.load_de(0x628A);
    (void)z.restart_vdp_address(0x628D);
    z.load_immediate(0x628E,r.c);
    return jump_tail(z,0x6290);
}
// 01:62AE: after the VDP address is set, three 128-byte blocks and a last
// jump into the same tail (the frame's 512 bytes).
template<class Z> Exit frame_512_tail(Z& z) {
    z.load_immediate(0x62AE,z.r.c);
    call_tail(z,0x62B0,0x589E);
    call_tail(z,0x62B3,0x589E);
    call_tail(z,0x62B6,0x589E);
    return jump_tail(z,0x62B9);
}
// 01:6293: a 512-byte frame (D x 200h) from bank 18, only in game modes 5
// and 9; any other mode returns at once.
template<class Z> Exit frame_512(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x6293);
    z.compare_immediate(0x6296);
    if (!z.relative_if(0x6298,z.zero())) {
        z.compare_immediate(0x629A);
        if (auto exit=z.final_return_if(0x629C,mk::Condition::nz)) return *exit;
    }
    z.load_hl(0x629D);
    z.store_to_hl(0x62A0);
    z.load_immediate(0x62A2,r.l);
    z.copy(0x62A4,r.h,r.d);
    z.add_to_hl(0x62A5,z.hl());
    z.load_de(0x62A6);
    z.add_to_hl(0x62A9,z.de());
    z.load_de(0x62AA);
    (void)z.restart_vdp_address(0x62AD);
    return frame_512_tail(z);
}

// ---- 01:62BC and 01:630A: the tile animators ---------------------------------------------------
// Six animators, each a call of 01:630A with HL = its 4-byte record, DE = its
// table of eight source addresses, B = tiles and C = the first tile number.
template<class Z> Exit animator_call(Z& z, std::uint16_t at) {
    z.load_hl(at);
    z.load_de(at+3);
    z.load_bc(at+6);
    return z.final_call(static_cast<std::uint16_t>(at+9));
}
template<class Z> Exit animators(Z& z) {
    z.load_hl(0x62BC);
    z.store_to_hl(0x62BF);                  // slot 2: the animation tiles' bank
    return animator_call(z,0x62C1);
}
// The upload loop: A rows of 32 bytes, an OUTI and a NOP each. The end pops
// the caller's return address (01:6343) and returns past 01:62BC.
// The loop is long (up to 16 rows, about 15,700 cycles) and often runs across
// the frame boundary, where a call is refused at its entry; a segment must end at
// the next entry, so the chunks are cut fine: at the inner JP NZ (01:633C,
// resumed at 01:6339 or 01:633F) and at the outer one (01:6340, resumed at
// 01:6337 or 01:6343), each at its first arrival once 32 cycles have passed.
constexpr std::uint16_t animator_after=32;
enum class Tiles { row, tile, count, end };
template<class Z> Exit animator_tiles(Z& z, Tiles from) {
    auto& r=z.r;
    for (;;) {
        switch (from) {
        case Tiles::row:
            z.load_immediate(0x6337,r.b);
            [[fallthrough]];
        case Tiles::tile:
            z.output_increment(0x6339);
            z.nop(0x633B);
            if (z.loop_exit(animator_after)) return z.final_jump_if(0x633C,mk::Condition::nz);
            if (z.jump_if(0x633C,!z.zero())) { from=Tiles::tile; continue; }
            [[fallthrough]];
        case Tiles::count:
            z.decrement(0x633F,r.a);
            if (z.loop_exit(animator_after)) return z.final_jump_if(0x6340,mk::Condition::nz);
            if (z.jump_if(0x6340,!z.zero())) { from=Tiles::row; continue; }
            [[fallthrough]];
        case Tiles::end:
            break;
        }
        z.pop(0x6343,r.h,r.l);
        return z.final_return(0x6344);
    }
}
// 01:6334: after the VDP address is set, the tile count and the row loop.
template<class Z> Exit animator_rows(Z& z) {
    z.load_immediate(0x6334,z.r.c);
    z.copy(0x6336,z.r.a,z.r.b);
    return animator_tiles(z,Tiles::row);
}
// The record: +0 on (0 skips), +1 delay, +2 countdown, +3 step (eight
// frames, counted down on the road's later roads, C2E9h >= 4, else up).
template<class Z> Exit animator(Z& z) {
    auto& r=z.r;
    z.load_from_hl(0x630A,r.a);
    z.or_a(0x630B);
    if (auto exit=z.final_return_if(0x630C,mk::Condition::z)) return *exit;
    z.increment_hl(0x630D);
    z.load_from_hl(0x630E,r.a);
    z.increment_hl(0x630F);
    z.decrement_at_hl(0x6310);
    if (auto exit=z.final_return_if(0x6311,mk::Condition::p)) return *exit;
    z.store_register_to_hl(0x6312,r.a);
    z.increment_hl(0x6313);
    z.load_a_direct(0x6314);
    z.compare_immediate(0x6317);
    if (z.relative_if(0x6319,z.carry())) z.increment_at_hl(0x631E);
    else {
        z.decrement_at_hl(0x631B);
        z.relative(0x631C);
    }
    z.load_from_hl(0x631F,r.a);
    z.and_immediate(0x6320);
    z.copy(0x6322,r.l,r.a);
    z.load_immediate(0x6323,r.h);
    z.add_to_hl(0x6325,z.hl());
    z.add_to_hl(0x6326,z.de());
    z.load_from_hl(0x6327,r.a);
    z.increment_hl(0x6328);
    z.load_from_hl(0x6329,r.h);
    z.copy(0x632A,r.l,r.a);
    z.exchange_de_hl(0x632B);
    z.copy(0x632C,r.l,r.c);
    z.load_immediate(0x632D,r.h);
    z.add_to_hl(0x632F,z.hl());
    z.add_to_hl(0x6330,z.hl());
    z.add_to_hl(0x6331,z.hl());
    z.exchange_de_hl(0x6332);
    (void)z.restart_vdp_address(0x6333);
    return animator_rows(z);
}

// ---- 01:73D0: one pending strip to the name table ------------------------------------------------
// C264h low nibble: 0 nothing; bits 0-1 a row, else a column. C263h is the
// bank of the strip's source. A column is 28 cells (two bytes each, one VRAM
// address per cell) from the mirror at D000h; a row is 64 bytes.
template<class Z> Exit strip_column_loop(Z& z);
template<class Z> Exit strip_row(Z& z) {
    auto& r=z.r;
    z.copy(0x741C,r.a,r.b);
    z.and_immediate(0x741D);
    z.load_a_direct(0x741F);
    z.load_immediate(0x7422,r.b);
    if (!z.relative_if(0x7424,!z.zero())) {
        z.compare_immediate(0x7426);
        if (!z.relative_if(0x7428,z.carry())) z.add_immediate(0x742A);
        z.load_immediate(0x742C,r.b);
        z.add_register(0x742E,r.b);
    }
    z.and_immediate(0x742F);
    z.copy(0x7431,r.l,r.a);
    z.load_immediate(0x7432,r.h);
    z.add_to_hl(0x7434,z.hl());
    z.add_to_hl(0x7435,z.hl());
    z.add_to_hl(0x7436,z.hl());
    z.copy(0x7437,r.a,r.h);
    z.add_immediate(0x7438);
    z.copy(0x743A,r.d,r.a);
    z.copy(0x743B,r.e,r.l);
    (void)z.restart_vdp_address(0x743C);
    z.copy(0x743D,r.a,r.h);
    z.add_immediate(0x743E);
    z.copy(0x7440,r.h,r.a);
    z.load_bc(0x7441);
    do {
        z.output_increment(0x7444);
        z.nop(0x7446);
    } while (z.jump_if(0x7447,!z.zero()));
    return z.final_return(0x744A);
}
template<class Z> Exit strip_upload(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x73D0);
    z.and_immediate(0x73D3);
    if (auto exit=z.final_return_if(0x73D5,mk::Condition::z)) return *exit;
    z.copy(0x73D6,r.b,r.a);
    z.and_immediate(0x73D7);
    z.load_a_direct(0x73D9);
    z.store_a_direct(0x73DC);               // slot 2
    if (z.jump_if(0x73DF,!z.zero())) return strip_row(z);
    z.load_immediate(0x73E2,r.c);
    z.load_a_direct(0x73E4);
    z.and_immediate(0x73E7);
    if (!z.relative_if(0x73E9,z.zero())) {
        z.copy(0x73EB,r.a,r.b);
        z.and_immediate(0x73EC);
        if (!z.relative_if(0x73EE,!z.zero())) z.load_immediate(0x73F0,r.c);
    }
    z.load_a_direct(0x73F2);
    z.add_register(0x73F5,r.c);
    z.rotate_right_circular_a(0x73F6);
    z.rotate_right_circular_a(0x73F7);
    z.and_immediate(0x73F8);
    z.copy(0x73FA,r.e,r.a);
    z.copy(0x73FB,r.l,r.a);
    z.load_immediate(0x73FC,r.d);
    z.load_immediate(0x73FE,r.h);
    z.load_bc(0x7400);
    return strip_column_loop(z);
}
// 01:7403: the column's cells, one VRAM address and four bytes each.
template<class Z> Exit strip_column_loop(Z& z) {
    auto& r=z.r;
    do {
        // With the stack in ROM the pop gives back the same count every pass, so the original's loop
        // never ends: the estimate gives up after two frames (uncertain); the live run, which only
        // follows an estimate that ended, never reaches the guard (it would stop with its message).
        if (!z.guard(z.elapsed()<=mk::uncertain_maximum,"a registered video segment ran past two frames"))
            return z.final_return(0x741B);
        z.push(0x7403,r.b,r.c);
        // A stack that does not read back (in ROM) makes the helper return
        // elsewhere and the popped count meaningless: the estimate gives up
        // here (uncertain) rather than follow a loop that may not end.
        if (!z.restart_vdp_address(0x7404)) return z.final_return(0x741B);
        z.nop(0x7405);
        z.nop(0x7406);
        z.nop(0x7407);
        z.output_increment(0x7408);
        z.nop(0x740A);
        z.nop(0x740B);
        z.nop(0x740C);
        z.output_increment(0x740D);
        z.load_bc(0x740F);
        z.add_to_hl(0x7412,z.bc());
        z.exchange_de_hl(0x7413);
        z.load_immediate(0x7414,r.c);
        z.add_to_hl(0x7416,z.bc());
        z.exchange_de_hl(0x7417);
        z.pop(0x7418,r.b,r.c);
    } while (z.decrement_jump(0x7419));
    return z.final_return(0x741B);
}

// ---- 01:7C18: the vehicles' and the ship's colours --------------------------------------------
// On foot (C30Eh = 0) nothing. The ship (10h, 11h) uploads the 32 palette
// bytes from C220h. The vehicles below 0Eh cycle three colours every four
// frames (C310h countdown, C30Fh step), the hovercraft (8) always and the
// others only while C265h is set, and request a sound (C004h) each step.
template<class Z> Exit ship_palette(Z& z) {
    z.load_hl(0x7C79);
    z.load_de(0x7C7C);
    (void)z.restart_vdp_address(0x7C7F);
    z.load_immediate(0x7C80,z.r.c);
    return jump_tail(z,0x7C82);
}
template<class Z> Exit colour_cycle(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x7C18);
    z.or_a(0x7C1B);
    if (auto exit=z.final_return_if(0x7C1C,mk::Condition::z)) return *exit;
    z.compare_immediate(0x7C1D);
    if (z.jump_if(0x7C1F,z.zero())) return ship_palette(z);
    z.compare_immediate(0x7C22);
    if (z.jump_if(0x7C24,z.zero())) return ship_palette(z);
    z.compare_immediate(0x7C27);
    if (auto exit=z.final_return_if(0x7C29,mk::Condition::nc)) return *exit;
    z.copy(0x7C2A,r.b,r.a);
    z.load_immediate(0x7C2B,r.c);
    z.compare_immediate(0x7C2D);
    if (!z.jump_if(0x7C2F,z.zero())) {
        z.load_immediate(0x7C32,r.c);
        z.load_a_direct(0x7C34);
        z.or_a(0x7C37);
        if (auto exit=z.final_return_if(0x7C38,mk::Condition::z)) return *exit;
    }
    z.load_hl(0x7C39);
    z.decrement_at_hl(0x7C3C);
    if (auto exit=z.final_return_if(0x7C3D,mk::Condition::p)) return *exit;
    z.store_to_hl(0x7C3E);
    z.decrement_hl(0x7C40);
    z.copy(0x7C41,r.a,r.c);
    z.store_a_direct(0x7C42);               // the sound request
    z.load_from_hl(0x7C45,r.a);
    z.increment(0x7C46,r.a);
    z.compare_immediate(0x7C47);
    if (!z.relative_if(0x7C49,z.carry())) z.xor_a(0x7C4B);
    z.store_register_to_hl(0x7C4C,r.a);
    z.copy(0x7C4D,r.c,r.a);
    z.copy(0x7C4E,r.a,r.b);
    z.compare_immediate(0x7C4F);
    z.load_hl(0x7C51);
    z.load_de(0x7C54);
    if (!z.jump_if(0x7C57,!z.zero())) {
        z.load_hl(0x7C5A);
        z.load_de(0x7C5D);
    }
    z.load_immediate(0x7C60,r.b);
    z.add_to_hl(0x7C62,z.bc());
    (void)z.restart_vdp_address(0x7C63);
    z.load_bc(0x7C64);
    do z.output_increment(0x7C67); while (z.jump_if(0x7C69,!z.zero()));
    return z.final_return(0x7C6C);
}

// ---- Exit lists ------------------------------------------------------------------------------------
std::vector<SegmentExit> loading_exits() {
    return {mk::conditional_loop_at(0x02D2,name_table_after),mk::core_final_at(0x02D9)};
}
std::vector<SegmentExit> party_slot_exits() { return {mk::conditional_at(0x6214),mk::returned_at(0x621E)}; }
std::vector<SegmentExit> animator_tile_exits() {
    return {mk::conditional_loop_at(0x633C,animator_after),mk::conditional_loop_at(0x6340,animator_after),
            mk::returned_at(0x6344,+2)};
}
}

void register_sprite_output_slices(RoutineRegistry& registry) {
    // Other call and resume positions use eight writes at a time. The last
    // OUTI is left to the next segment so the next opcode remains an observable boundary.
    for (std::uint16_t from=0x58AE;from<tail_return;from+=16) {
        if (from==0x595E) continue; // already has a full-tail entry
        auto final=mk::core_final_at(from+14);
        final.sprite_outputs=8;
        mk::add_segment(registry,at1(from),mk::segment(map::sprite_upload_unit,{final}),
            [](const Machine&) -> std::uint64_t { return 128; },[from](auto& z) {
                z.output_increment_run(from,8);
                return mk::fall_through(static_cast<std::uint16_t>(from+14),static_cast<std::uint16_t>(from+16));
            });
    }
}

void register_map_video(RoutineRegistry& registry) {
    // The frame handlers.
    mk::add_segment(registry,map::frame_handler_unit,
        mk::segment(map::frame_handler_unit,{mk::core_final_at(0x0247)}),
        [](auto& z) { return scroll_registers(z,0x0235); });
    mk::add_segment(registry,map::load_frame_handler_unit,
        mk::segment(map::load_frame_handler_unit,{mk::core_final_at(0x02B1)}),
        [](auto& z) { return scroll_registers(z,0x029F); });
    mk::add_segment(registry,at0(0x02B4),
        mk::segment(map::load_frame_handler_unit,loading_exits(),{.scratch_stack=2}),
        [](auto& z) { return loading_upload(z); });
    mk::add_segment(registry,at0(0x02D0),mk::segment(map::load_frame_handler_unit,loading_exits()),
        [](auto& z) { return name_table_loop(z,false); });
    mk::add_segment(registry,at0(0x02D5),mk::segment(map::load_frame_handler_unit,loading_exits()),
        [](auto& z) { return name_table_loop(z,true); });

    // The sprite table upload and its tails.
    mk::add_segment(registry,map::sprite_upload_unit,
        mk::segment(map::sprite_upload_unit,{mk::returned_at(tail_return)},{.scratch_stack=2}),
        [](auto& z) { return sprite_upload(z); });
    // Preserve the full-tail entries already used by the sprite and palette uploads.
    for (const std::uint16_t from:{std::uint16_t{0x589E},std::uint16_t{0x595E}})
        mk::add_segment(registry,at1(from),mk::segment(map::sprite_upload_unit,{mk::returned_at(tail_return)}),
            [from](auto& z) { upload_tail(z,from); return z.final_return(tail_return); });

    // Joining stretches: the return that ends the unrolled tail when it was
    // called from code outside these segments, and the RET that ends the animators' call
    // chain (the hottest of them: once per animator and frame).
    for (const auto& [entry,of_unit]:{std::pair{std::uint16_t{0x599E},map::sprite_upload_unit},
                                      std::pair{std::uint16_t{0x6309},map::tile_animators_unit},
                                      std::pair{std::uint16_t{0x741B},map::strip_upload_unit}}) {
        auto spec=mk::segment(of_unit,{mk::returned_at(entry)});
        spec.single_instruction=true;
        const auto at=entry;
        mk::add_segment(registry,at1(entry),std::move(spec),[at](auto& z) -> Exit { return z.final_return(at); });
    }

    // The party frame tiles and their kinds.
    mk::add_segment(registry,map::party_frames_unit,
        mk::segment(map::party_frames_unit,party_slot_exits(),{.slot2_mapper=true}),
        [](auto& z) { return party_frames(z); });
    mk::add_segment(registry,at1(0x6217),mk::segment(map::party_frames_unit,party_slot_exits()),
        [](auto& z) { return party_slots(z,true); });
    for (const std::uint16_t kind:{std::uint16_t{0x6229},std::uint16_t{0x6246},std::uint16_t{0x6263}})
        mk::add_segment(registry,at1(kind),
            mk::segment(map::party_frames_unit,{mk::returned_at(tail_return)},{.scratch_stack=2}),
            [kind](auto& z) { return frame_192(z,kind); });
    mk::add_segment(registry,at1(0x6280),
        mk::segment(map::party_frames_unit,{mk::returned_at(tail_return)},{.scratch_stack=2}),
        [](auto& z) { return frame_128(z); });
    mk::add_segment(registry,at1(0x6293),
        mk::segment(map::party_frames_unit,{mk::returned_at(0x629C),mk::returned_at(tail_return)},
                    {.slot2_mapper=true,.scratch_stack=2}),
        [](auto& z) { return frame_512(z); });

    // The tile animators.
    mk::add_segment(registry,map::tile_animators_unit,
        mk::segment(map::tile_animators_unit,{mk::core_final_at(0x62CA)},{.slot2_mapper=true}),
        [](auto& z) { return animators(z); });
    for (const std::uint16_t call:{std::uint16_t{0x62CD},std::uint16_t{0x62D9},std::uint16_t{0x62E5},
                                   std::uint16_t{0x62F1},std::uint16_t{0x62FD}})
        mk::add_segment(registry,at1(call),
            mk::segment(map::tile_animators_unit,{mk::core_final_at(static_cast<std::uint16_t>(call+9))}),
            [call](auto& z) { return animator_call(z,call); });
    {
        auto exits=animator_tile_exits();
        exits.insert(exits.begin(),{mk::returned_at(0x630C),mk::returned_at(0x6311)});
        mk::add_segment(registry,at1(0x630A),
            mk::segment(map::tile_animators_unit,exits,{.scratch_stack=2,.inherited_stack=2}),
            [](auto& z) { return animator(z); });
    }
    // 01:6339 and 01:633F are entries added inside the unit (segments.md
    // section 7): the resumes of the inner loop exit.
    for (const auto& [resume,from]:{std::pair{std::uint16_t{0x6337},Tiles::row},std::pair{std::uint16_t{0x6339},Tiles::tile},
                                    std::pair{std::uint16_t{0x633F},Tiles::count}})
        mk::add_segment(registry,at1(resume),
            mk::segment(map::tile_animators_unit,animator_tile_exits(),{.inherited_stack=2}),
            [from](auto& z) { return animator_tiles(z,from); });
    mk::add_segment(registry,at1(0x6343),
        mk::segment(map::tile_animators_unit,{mk::returned_at(0x6344,+2)},{.inherited_stack=2}),
        [](auto& z) { return animator_tiles(z,Tiles::end); });

    // 01:62AE: the frame's 512 bytes after the VDP address is set.
    mk::add_segment(registry,at1(0x62AE),
        mk::segment(map::party_frames_unit,{mk::returned_at(tail_return)},{.scratch_stack=2}),
        [](auto& z) { return frame_512_tail(z); });
    // 01:6334: the animator's tile count and row loop after the VDP address.
    mk::add_segment(registry,at1(0x6334),
        mk::segment(map::tile_animators_unit,animator_tile_exits(),{.inherited_stack=2}),
        [](auto& z) { return animator_rows(z); });

    // The strip upload and the colour cycle.
    mk::add_segment(registry,map::strip_upload_unit,
        mk::segment(map::strip_upload_unit,{mk::returned_at(0x73D5),mk::returned_at(0x741B),mk::returned_at(0x744A)},
                    {.slot2_mapper=true,.scratch_stack=4}),
        [](auto& z) { return strip_upload(z); });
    // 01:7403: the column's cells when a chunk was cut.
    mk::add_segment(registry,at1(0x7403),
        mk::segment(map::strip_upload_unit,{mk::returned_at(0x741B)},{.scratch_stack=4}),
        [](auto& z) { return strip_column_loop(z); });
    mk::add_segment(registry,map::colour_cycle_unit,
        mk::segment(map::colour_cycle_unit,{mk::returned_at(0x7C1C),mk::returned_at(0x7C29),mk::returned_at(0x7C38),
                    mk::returned_at(0x7C3D),mk::returned_at(0x7C6C),mk::returned_at(tail_return)},{.scratch_stack=2}),
        [](auto& z) { return colour_cycle(z); });
}
}
