// SPDX-License-Identifier: GPL-3.0-or-later
// Menus and windows (milestone 12), part engine:
// The window engine and the printers with interrupts off: saving what lies
// under a window 00:3AA6, drawing a window one row per frame 00:3A57 and
// 00:3A68 or at once 00:3A83, the number, money, item-name, enemy-name and
// shop-line printers (00:2FE1, 00:35C4, 00:356B, 00:315C, 00:393A and kin), the
// digit helper 00:3645 and the dialogue scroll 00:3439 and 00:342C.
// Planned segments: menu::engine_segments in game/menu.h, rendered with
// game/menu_kit.h.
//
// Almost all of this code talks to the video chip with interrupts off: a DI,
// the VDP address set through RST 08 (rendered inline), a run of data-port
// writes (OUT, OUTI) or reads (INI), and an EI. The DI and EI, the instruction
// after each EI and the pushes and pops right before them stay native
// (menu::native_instructions); each segment starts after the DI and ends on
// the last plain instruction before the EI (an `ex de,hl` in every printer,
// `ld a,n` at 00:3A87). Every port access happens at the T-state of the
// original instruction (the kit times them), so the VDP sees the same reads
// and writes at the same cycles, its read buffer and address included.
//
// The two whole-window loops (00:3A83 drawing at once, 00:3AA6 saving under a
// window) are cut into short chunks inside each row (one byte, three bytes),
// because a big window takes longer than a frame and a chunk that does not
// fit before the frame boundary runs natively up to the next entry; 00:3A68
// draws one row per call and frame. The enemy-name printer's
// character loop is cut on its DJNZ. The calls between units stay native
// (bare calls are core-executed exits); the digit helper 00:3645 is rendered
// inline inside the printers and also registered on its own for the native
// paths a fallback takes.
#include "game/menu.h"
#include "game/menu_kit.h"
#include "game/routines.h"
#include <stdexcept>
#include <type_traits>

namespace psmodern::game {
namespace {
namespace mk=game::menu_kit;
using mk::Exit;
using mk::call_cycles;
using mk::continues;
using mk::jump_cycles;
using mk::return_cycles;
using mk::returns;
constexpr unsigned ex_de_hl_cycles=4, load_immediate_cycles=7;
constexpr RoutineEntry at0(std::uint16_t address) { return {0,address}; }

// ---- Shared pieces ----------------------------------------------------------------------------
// `push af; pop af; ld a,n; out (n),a` at `at`: one data-port write after a
// 21-cycle delay (the VDP needs time between writes with the display on).
template<class Z> void delayed_out(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.push(at,r.a,r.f);
    z.pop(static_cast<std::uint16_t>(at+1),r.a,r.f);
    z.load_immediate(static_cast<std::uint16_t>(at+2),r.a);
    z.output(static_cast<std::uint16_t>(at+4));
}

// 00:3645: one digit A (low four bits). A zero digit keeps the tile base in B
// (C0h, a blank, while only zeros have been printed), a non-zero one switches
// to the digit tiles (C1h onwards) for this and every later digit. Writes the
// tile and its attribute (C) to the data port. Rendered up to its RET.
template<class Z> void digit_body(Z& z) {
    auto& r=z.r;
    z.and_immediate(0x3645);
    if (!z.jump_if(0x3647,z.zero())) z.load_bc(0x364A);
    z.add_register(0x364D,r.b);
    z.output(0x364E);
    z.push(0x3650,r.a,r.f);
    z.pop(0x3651,r.a,r.f);
    z.copy(0x3652,r.a,r.c);
    z.output(0x3653);
}
constexpr std::uint16_t digit_return=0x3655;
// A bare `call 3645` at `at` rendered with the helper and its return.
template<class Z> void call_digit(Z& z, std::uint16_t at) {
    (void)z.inline_subroutine(at,[](auto& callee) { digit_body(callee); return callee.return_from(digit_return); });
}

// The `jr nc` closing a digit count at `at`. The counts end when the
// subtraction borrows, which with the game's operands takes at most 10
// rounds; but the estimate also meets states whose code operands are read
// through a mapping the segment itself disturbed (a stack on the mapper
// registers, which dispatch refuses after estimating), where a zero operand
// would count forever. Past the uncertain maximum the estimate gives up; the
// live side never meets such a state.
template<class Z> bool counting(Z& z, std::uint16_t at) {
    if (!z.relative_if(at,!z.carry())) return false;
    if (z.elapsed()<=mk::uncertain_maximum) return true;
    if constexpr (std::is_base_of_v<kit::Estimate,std::remove_reference_t<decltype(z.bus())>>) { z.bus().uncertain=true; return false; }
    else throw std::logic_error("a registered engine segment counted a digit past one frame");
}

// `ld a,(hl); out (n),a; inc hl; djnz` at `at`: B label tiles from HL.
template<class Z> void label(Z& z, std::uint16_t at) {
    do {
        z.load_from_hl(at,z.r.a);
        z.output(static_cast<std::uint16_t>(at+1));
        z.increment_hl(static_cast<std::uint16_t>(at+3));
    } while (z.decrement_jump(static_cast<std::uint16_t>(at+4)));
}

// One character of a name at `at` (00:316F, 00:3588, 00:395C): the tile of
// character (HL) for row C from the two-tile font table (the `ld de,nn`
// operand), written with attribute 10h after a delay; HL advances. The DJNZ
// at at+17h is left to the caller.
template<class Z> void character(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.load_from_hl(at,r.a);
    z.subtract_immediate(static_cast<std::uint16_t>(at+1));
    z.add_register(static_cast<std::uint16_t>(at+3),r.a);
    z.add_register(static_cast<std::uint16_t>(at+4),r.c);
    z.load_de(static_cast<std::uint16_t>(at+5));
    z.add_register(static_cast<std::uint16_t>(at+8),r.e);
    z.copy(static_cast<std::uint16_t>(at+9),r.e,r.a);
    z.add_with_carry(static_cast<std::uint16_t>(at+10),r.d);
    z.subtract_register(static_cast<std::uint16_t>(at+11),r.e);
    z.copy(static_cast<std::uint16_t>(at+12),r.d,r.a);
    z.load_a_from_pair(static_cast<std::uint16_t>(at+13),z.de());
    z.output(static_cast<std::uint16_t>(at+14));
    delayed_out(z,static_cast<std::uint16_t>(at+16));
    z.increment_hl(static_cast<std::uint16_t>(at+22));
}
// The whole name: 8 characters, the DJNZ at at+17h rendered.
template<class Z> void name(Z& z, std::uint16_t at) {
    do character(z,at); while (z.decrement_jump(static_cast<std::uint16_t>(at+0x17)));
}

// The end of every printer at `at`: two delayed writes (the right border tile
// and its attribute), `pop de; ld hl,0040h; add hl,de`, and the
// `ex de,hl` (DE one name-table row further down) executed by the core.
template<class Z> Exit printer_end(Z& z, std::uint16_t at) {
    auto& r=z.r;
    delayed_out(z,at);
    delayed_out(z,static_cast<std::uint16_t>(at+6));
    z.pop(static_cast<std::uint16_t>(at+12),r.d,r.e);
    z.load_hl(static_cast<std::uint16_t>(at+13));
    z.add_to_hl(static_cast<std::uint16_t>(at+16),z.de());
    return continues(static_cast<std::uint16_t>(at+17),ex_de_hl_cycles);
}

// Tens and units of L (`ld d,FFh; ld a,l; sub n; inc d; jr nc` blocks at
// `tens` and `units`; the units always print, with the digit tiles), then the
// printer's end.
template<class Z> Exit small_digits(Z& z, std::uint16_t tens, std::uint16_t units, std::uint16_t end) {
    auto& r=z.r;
    z.load_immediate(tens,r.d);
    z.copy(static_cast<std::uint16_t>(tens+2),r.a,r.l);
    do { z.subtract_immediate(static_cast<std::uint16_t>(tens+3)); z.increment(static_cast<std::uint16_t>(tens+5),r.d); }
    while (counting(z,static_cast<std::uint16_t>(tens+6)));
    z.add_immediate(static_cast<std::uint16_t>(tens+8));
    z.copy(static_cast<std::uint16_t>(tens+10),r.l,r.a);
    z.copy(static_cast<std::uint16_t>(tens+11),r.a,r.d);
    call_digit(z,static_cast<std::uint16_t>(tens+12));
    z.load_immediate(units,r.d);
    z.copy(static_cast<std::uint16_t>(units+2),r.a,r.l);
    do { z.subtract_immediate(static_cast<std::uint16_t>(units+3)); z.increment(static_cast<std::uint16_t>(units+5),r.d); }
    while (counting(z,static_cast<std::uint16_t>(units+6)));
    z.copy(static_cast<std::uint16_t>(units+8),r.a,r.d);
    z.load_bc(static_cast<std::uint16_t>(units+9));
    call_digit(z,static_cast<std::uint16_t>(units+12));
    return printer_end(z,end);
}
// ---- 00:2FD8 / 00:2FE1: a label and a number 0-255 -------------------------------------------
// 16 (2FD8) or 8 (2FE1) label tiles from HL, then A as three digits with
// leading blanks.
template<class Z> Exit byte_number(Z& z) {
    auto& r=z.r;
    label(z,0x2FE7);
    z.pop(0x2FED,r.a,r.f);
    z.load_bc(0x2FEE);
    z.load_immediate(0x2FF1,r.d);
    do { z.subtract_immediate(0x2FF3); z.increment(0x2FF5,r.d); } while (counting(z,0x2FF6));
    z.add_immediate(0x2FF8);
    z.copy(0x2FFA,r.l,r.a);
    z.copy(0x2FFB,r.a,r.d);
    call_digit(z,0x2FFC);
    return small_digits(z,0x2FFF,0x300E,0x301D);
}
template<class Z> Exit number_wide_label(Z& z) {      // 00:2FD9
    auto& r=z.r;
    z.push(0x2FD9,r.d,r.e);
    z.push(0x2FDA,r.a,r.f);
    (void)z.restart_vdp_address(0x2FDB);
    z.load_immediate(0x2FDC,r.b);
    (void)z.jump(0x2FDE);                            // into 2FE1's body at 2FE7
    return byte_number(z);
}
template<class Z> Exit number(Z& z) {                 // 00:2FE2
    auto& r=z.r;
    z.push(0x2FE2,r.d,r.e);
    z.push(0x2FE3,r.a,r.f);
    (void)z.restart_vdp_address(0x2FE4);
    z.load_immediate(0x2FE5,r.b);
    return byte_number(z);
}

// ---- 00:3036: a label and a number 0-65535 ---------------------------------------------------
// 12 label tiles, then BC as five digits. The thousands and hundreds count
// from `ld a,FFh`, which does not touch carry, so the subtraction that follows
// would take one more if carry were set. It is not: the digit helper 00:3645
// called just before has ended with `and 0Fh`, which clears carry, so the
// counts are exact (original-quirks.md).
template<class Z> void wide_digit(Z& z, std::uint16_t at) {   // ld de,nn at `at`, the count from at+5
    auto& r=z.r;
    do { z.subtract_hl_with_carry(static_cast<std::uint16_t>(at+5),z.de()); z.increment(static_cast<std::uint16_t>(at+7),r.a); }
    while (counting(z,static_cast<std::uint16_t>(at+8)));
    z.add_to_hl(static_cast<std::uint16_t>(at+10),z.de());
    call_digit(z,static_cast<std::uint16_t>(at+11));
}
template<class Z> Exit long_number(Z& z) {            // 00:3037
    auto& r=z.r;
    z.push(0x3037,r.d,r.e);
    z.push(0x3038,r.b,r.c);
    (void)z.restart_vdp_address(0x3039);
    z.load_immediate(0x303A,r.b);
    label(z,0x303C);
    z.pop(0x3042,r.h,r.l);
    z.load_bc(0x3043);
    z.load_de(0x3046);
    z.xor_a(0x3049);
    z.decrement(0x304A,r.a);
    do { z.subtract_hl_with_carry(0x304B,z.de()); z.increment(0x304D,r.a); } while (counting(z,0x304E));
    z.add_to_hl(0x3050,z.de());
    call_digit(z,0x3051);
    for (const std::uint16_t at:{std::uint16_t{0x3054},std::uint16_t{0x3062}}) {
        z.load_de(at);
        z.load_immediate(static_cast<std::uint16_t>(at+3),r.a);
        wide_digit(z,at);
    }
    return small_digits(z,0x3070,0x307F,0x308E);
}

// ---- 00:35BC / 00:35C4 / 00:393A: money and prices ---------------------------------------------
// 12 (35BC) or 8 (35C4) label tiles from 00:3639, then the money (C4E0h) or a
// price (393A) as five digits with leading blanks; each count starts from
// `xor a; cpl` (carry clear).
template<class Z> Exit money_digits(Z& z) {           // 00:35D5
    z.load_bc(0x35D5);
    for (const std::uint16_t at:{std::uint16_t{0x35D8},std::uint16_t{0x35E6},std::uint16_t{0x35F4}}) {
        z.load_de(at);
        z.xor_a(static_cast<std::uint16_t>(at+3));
        z.complement_a(static_cast<std::uint16_t>(at+4));
        wide_digit(z,at);
    }
    return small_digits(z,0x3602,0x3611,0x3620);
}
template<class Z> Exit money_label(Z& z) {            // 00:35C9
    z.load_hl(0x35C9);
    label(z,0x35CC);
    z.load_hl_direct(0x35D2);
    return money_digits(z);
}
template<class Z> Exit money_wide_label(Z& z) {       // 00:35BD
    auto& r=z.r;
    z.push(0x35BD,r.d,r.e);
    (void)z.restart_vdp_address(0x35BE);
    z.load_immediate(0x35BF,r.b);
    (void)z.jump(0x35C1);                            // into 35C4's body at 35C9
    return money_label(z);
}
template<class Z> Exit money(Z& z) {                  // 00:35C5
    auto& r=z.r;
    z.push(0x35C5,r.d,r.e);
    (void)z.restart_vdp_address(0x35C6);
    z.load_immediate(0x35C7,r.b);
    return money_label(z);
}
// 00:393B: one shop line from the record at HL: the item (record byte 0) as
// a name for row C (an empty item forces row 0), blanks (1 on row 1, 6 on
// row 0), then on row 1 the price (record bytes 1-2) through the money
// digits, on row 0 straight to the printer's end.
template<class Z> Exit shop_line(Z& z) {
    auto& r=z.r;
    z.push(0x393B,r.d,r.e);
    z.push(0x393C,r.h,r.l);
    (void)z.restart_vdp_address(0x393D);
    z.load_from_hl(0x393E,r.a);
    z.or_a(0x393F);
    if (!z.relative_if(0x3940,!z.zero())) z.load_immediate(0x3942,r.c);
    z.copy(0x3944,r.l,r.a);
    z.load_immediate(0x3945,r.h);
    z.add_to_hl(0x3947,z.hl());
    z.add_to_hl(0x3948,z.hl());
    z.add_to_hl(0x3949,z.hl());
    z.load_de(0x394A);
    z.add_to_hl(0x394D,z.de());
    delayed_out(z,0x394E);
    delayed_out(z,0x3954);
    z.load_immediate(0x395A,r.b);
    name(z,0x395C);
    z.copy(0x3975,r.a,r.c);
    z.or_a(0x3976);
    z.load_immediate(0x3977,r.b);
    if (!z.relative_if(0x3979,!z.zero())) z.load_immediate(0x397B,r.b);
    do { delayed_out(z,0x397D); delayed_out(z,0x3983); } while (z.decrement_jump(0x3989));
    z.pop(0x398B,r.h,r.l);
    z.increment_hl(0x398C);
    z.load_from_hl(0x398D,r.a);
    z.increment_hl(0x398E);
    z.load_from_hl(0x398F,r.h);
    z.copy(0x3990,r.l,r.a);
    z.copy(0x3991,r.a,r.c);
    z.or_a(0x3992);
    if (z.jump_if(0x3993,!z.zero())) return money_digits(z);   // jp nz,35D5
    (void)z.jump(0x3996);                                         // jp 3620
    return printer_end(z,0x3620);
}

// ---- 00:356B: an item name line ----------------------------------------------------------------
// The name of item (HL) for row C: 8 characters of the item-name table entry
// (item x 8 from the `ld de,nn` operand).
template<class Z> Exit item_name_line(Z& z) {         // 00:356C
    auto& r=z.r;
    z.push(0x356C,r.b,r.c);
    z.push(0x356D,r.h,r.l);
    z.push(0x356E,r.d,r.e);
    (void)z.restart_vdp_address(0x356F);
    z.load_from_hl(0x3570,r.l);
    z.load_immediate(0x3571,r.h);
    z.add_to_hl(0x3573,z.hl());
    z.add_to_hl(0x3574,z.hl());
    z.add_to_hl(0x3575,z.hl());
    z.load_de(0x3576);
    z.add_to_hl(0x3579,z.de());
    delayed_out(z,0x357A);
    delayed_out(z,0x3580);
    z.load_immediate(0x3586,r.b);
    name(z,0x3588);
    return printer_end(z,0x35A1);
}

// ---- 00:315C: an enemy name line ---------------------------------------------------------------
// 8 characters from HL for row C, the left and right borders. The character
// loop is cut on its DJNZ (00:3186) once `name_after` cycles have passed; the
// two resumes are entries 00:316F (taken) and 00:3188 (not taken). A
// character costs 128 cycles: 00:315D always ends after the first one (its
// end at 00:3199 is never reached from there), 00:316F after two. Short
// chunks matter beyond the bound: a chunk that falls back near the frame
// boundary runs natively only up to the next entry, which is again this loop,
// so a long chunk falls back once per character until the boundary is passed
// (segments.md planned 512 cycles; the census had 316F at 55-84%).
constexpr std::uint16_t name_after=128;
enum class Name { start, character, end };
template<class Z> Exit enemy_name_line(Z& z, Name from) {
    auto& r=z.r;
    if (from==Name::start) {
        z.push(0x315D,r.b,r.c);
        z.push(0x315E,r.h,r.l);
        z.push(0x315F,r.d,r.e);
        (void)z.restart_vdp_address(0x3160);
        delayed_out(z,0x3161);
        delayed_out(z,0x3167);
        z.load_immediate(0x316D,r.b);
    }
    if (from!=Name::end)
        for (;;) {
            character(z,0x316F);
            if (z.loop_exit(name_after)) return mk::decrement_jump_exit(z,0x3186);
            if (!z.decrement_jump(0x3186)) break;
        }
    return printer_end(z,0x3188);
}

// ---- 00:3439 / 00:342C: the dialogue scroll ----------------------------------------------------
// Save the lower three text rows (3AA6), draw them back one row higher
// (3A83), draw a blank row at the bottom (3A83, native loads 3451/3454), wait
// four frames. 342C scrolls twice, keeping BC, DE and HL.
template<class Z> Exit three_loads(Z& z, std::uint16_t at) {
    z.load_hl(at);
    z.load_de(static_cast<std::uint16_t>(at+3));
    z.load_bc(static_cast<std::uint16_t>(at+6));
    return continues(static_cast<std::uint16_t>(at+9),call_cycles);
}
template<class Z> Exit scroll_wait(Z& z, bool after_wait) {
    auto& r=z.r;
    if (after_wait) {
        if (!z.decrement_jump(0x3461)) return returns(0x3463,return_cycles);
    } else z.load_immediate(0x345A,r.b);
    z.load_immediate(0x345C,r.a);
    return continues(0x345E,call_cycles);
}

// ---- 00:3A57 / 00:3A68: draw a window one row per frame ---------------------------------------
// 3A57 switches slot 2 to the window bank (the `ld a,n` operand) around the
// native call of 3A68 and restores it. 3A68: per row, interrupts off, the row
// (C bytes from HL) to VRAM DE with OUTI, DE one row down, interrupts on and
// a frame wait (native), B rows.
template<class Z> Exit draw_window(Z& z) {            // 00:3A57
    auto& r=z.r;
    z.load_a_direct(0x3A57);
    z.push(0x3A5A,r.a,r.f);
    z.load_immediate(0x3A5B,r.a);
    z.store_a_direct(0x3A5D);
    return continues(0x3A60,call_cycles);
}
// The slot-2 store at `store_at` followed by the RET (00:3A64, 00:3AA2). The
// store also lands in the work-RAM byte under $FFFF; with the stack at FFFEh
// the return word is read back changed and the original returns elsewhere,
// which the core-executed RET cannot be declared for: the estimate gives up
// (the dispatch falls back) and the live side never meets such a state.
template<class Z> Exit restore_bank(Z& z, std::uint16_t pop_at, std::uint16_t store_at) {
    auto& r=z.r;
    if (pop_at) z.pop(pop_at,r.a,r.f);
    z.store_a_direct(store_at);
    if (r.sp==0xFFFE || r.sp==0xFFFF) {
        if constexpr (std::is_base_of_v<kit::Estimate,std::remove_reference_t<decltype(z.bus())>>) z.bus().uncertain=true;
        else throw std::logic_error("a registered engine segment returned through the slot-2 register");
    }
    return returns(static_cast<std::uint16_t>(store_at+3),return_cycles);
}
template<class Z> Exit row(Z& z) {                    // 00:3A6A
    auto& r=z.r;
    (void)z.restart_vdp_address(0x3A6A);
    z.copy(0x3A6B,r.b,r.c);
    z.load_immediate(0x3A6C,r.c);
    do z.output_increment(0x3A6E); while (z.jump_if(0x3A70,!z.zero()));
    z.exchange_de_hl(0x3A73);
    z.load_bc(0x3A74);
    z.add_to_hl(0x3A77,z.bc());
    return continues(0x3A78,ex_de_hl_cycles);
}
template<class Z> Exit next_row(Z& z) {               // 00:3A7F
    z.pop(0x3A7F,z.r.b,z.r.c);
    return mk::decrement_jump_exit(z,0x3A80);
}

// ---- 00:3A83: draw a window at once -------------------------------------------------------------
// The same rows without frame waits, interrupts off for the whole window.
// One byte per segment: the row start 00:3A8D (push bc, the VDP address, the
// first OUTI) and each further OUTI 00:3A92 end on the loop's JP NZ
// (00:3A94), decided by the core. The row end 00:3A97-00:3A9E stays native
// (not registered, segments.md section 7): the dialogue scroll is locked to
// its frame wait, so the frame boundary keeps falling on it, and with any
// longer chunk a fallback's native byte runs into it with little of the frame
// left. A one-byte chunk (26 cycles) that falls back crosses the boundary
// within its own native byte, so the next entry always has a whole frame.
template<class Z> Exit at_once_start(Z& z) {          // 00:3A83
    z.load_a_direct(0x3A83);
    z.push(0x3A86,z.r.a,z.r.f);
    return continues(0x3A87,load_immediate_cycles);
}
template<class Z> Exit at_once_byte(Z& z, bool row_start) {
    auto& r=z.r;
    if (row_start) {
        z.push(0x3A8D,r.b,r.c);
        (void)z.restart_vdp_address(0x3A8E);
        z.copy(0x3A8F,r.b,r.c);
        z.load_immediate(0x3A90,r.c);
    }
    z.output_increment(0x3A92);
    return mk::jump_if_exit(0x3A94);
}

// ---- 00:3AA6: save what lies under a window --------------------------------------------------------
// B rows of C bytes from VRAM DE (bit 6 of D cleared: a read address) into
// the buffer at HL with INI, each byte followed by a 21-cycle delay. One
// segment per row; the INI loop is cut on its JP NZ (00:3AB4) once
// `save_after` cycles have passed (resumes 00:3AB0 taken, 00:3AB7 not): three
// bytes (37 cycles each) per chunk from 00:3AB0, one from the row starts
// 00:3AA7 and 00:3AAB (whose row end is then never reached). The plan's 768
// cycles left 3AB0 at 70-85% in the census for the reason given at 00:315C:
// a fallback repeats on every byte until the frame boundary is passed.
constexpr std::uint16_t save_after=96;
enum class Save { start, row, inside, end };
template<class Z> Exit save_under(Z& z, Save from) {
    auto& r=z.r;
    if (from==Save::start) {
        z.push(0x3AA7,r.b,r.c);
        z.push(0x3AA8,r.d,r.e);
        z.reset_bit(0x3AA9,6,r.d);
    }
    if (from==Save::start || from==Save::row) {
        z.push(0x3AAB,r.b,r.c);
        (void)z.restart_vdp_address(0x3AAC);
        z.copy(0x3AAD,r.b,r.c);
        z.load_immediate(0x3AAE,r.c);
    }
    if (from!=Save::end)
        for (;;) {
            z.input_increment(0x3AB0);
            z.push(0x3AB2,r.a,r.f);
            z.pop(0x3AB3,r.a,r.f);
            if (z.loop_exit(save_after)) return continues(0x3AB4,jump_cycles);
            if (!z.jump_if(0x3AB4,!z.zero())) break;
        }
    z.exchange_de_hl(0x3AB7);
    z.load_bc(0x3AB8);
    z.add_to_hl(0x3ABB,z.bc());
    z.exchange_de_hl(0x3ABC);
    z.pop(0x3ABD,r.b,r.c);
    return mk::decrement_jump_exit(z,0x3ABE);
}

// ---- Exit lists -----------------------------------------------------------------------------------
std::vector<SegmentExit> enemy_name_exits(std::int8_t loop, std::int8_t end) {
    return {mk::conditional_loop_at(0x3186,name_after,loop),mk::core_final_at(0x3199,end)};
}
}

void register_menu_engine(RoutineRegistry& registry) {
    // The number printers (00:2FD8 renders into 00:2FE1's body).
    mk::add_segment(registry,at0(0x2FD9),
        mk::segment(menu::number_wide_label_unit,{mk::core_final_at(0x302E)},{.scratch_stack=6}),
        [](auto& z) { return number_wide_label(z); });
    mk::add_segment(registry,at0(0x2FE2),
        mk::segment(menu::number_unit,{mk::core_final_at(0x302E)},{.scratch_stack=6}),
        [](auto& z) { return number(z); });
    mk::add_segment(registry,at0(0x3037),
        mk::segment(menu::long_number_unit,{mk::core_final_at(0x309F)},{.scratch_stack=6}),
        [](auto& z) { return long_number(z); });

    // The enemy name line and the resumes of its character loop.
    mk::add_segment(registry,at0(0x315D),
        mk::segment(menu::enemy_name_line_unit,{mk::conditional_loop_at(0x3186,name_after,-6)},{.scratch_stack=8}),
        [](auto& z) { return enemy_name_line(z,Name::start); });
    mk::add_segment(registry,at0(0x316F),
        mk::segment(menu::enemy_name_line_unit,enemy_name_exits(0,+2),{.scratch_stack=2,.inherited_stack=2}),
        [](auto& z) { return enemy_name_line(z,Name::character); });
    mk::add_segment(registry,at0(0x3188),
        mk::segment(menu::enemy_name_line_unit,{mk::core_final_at(0x3199,+2)},{.scratch_stack=2,.inherited_stack=2}),
        [](auto& z) { return enemy_name_line(z,Name::end); });

    // The dialogue scroll.
    mk::add_segment(registry,at0(0x342C),
        mk::segment(menu::dialogue_scroll_two_unit,{mk::core_final_at(0x342F,-6)},{.scratch_stack=6}),
        [](auto& z) {
            auto& r=z.r;
            z.push(0x342C,r.b,r.c); z.push(0x342D,r.d,r.e); z.push(0x342E,r.h,r.l);
            return continues(0x342F,call_cycles);
        });
    mk::add_segment(registry,at0(0x3435),
        mk::segment(menu::dialogue_scroll_two_unit,{mk::returned_at(0x3438,+6)},{.inherited_stack=6}),
        [](auto& z) {
            auto& r=z.r;
            z.pop(0x3435,r.h,r.l); z.pop(0x3436,r.d,r.e); z.pop(0x3437,r.b,r.c);
            return returns(0x3438,return_cycles);
        });
    mk::add_segment(registry,at0(0x3439),mk::segment(menu::dialogue_scroll_unit,{mk::core_final_at(0x3442)}),
        [](auto& z) { return three_loads(z,0x3439); });
    mk::add_segment(registry,at0(0x3445),mk::segment(menu::dialogue_scroll_unit,{mk::core_final_at(0x344E)}),
        [](auto& z) { return three_loads(z,0x3445); });
    mk::add_segment(registry,at0(0x345A),mk::segment(menu::dialogue_scroll_unit,{mk::core_final_at(0x345E)}),
        [](auto& z) { return scroll_wait(z,false); });
    mk::add_segment(registry,at0(0x3461),
        mk::segment(menu::dialogue_scroll_unit,{mk::core_final_at(0x345E),mk::returned_at(0x3463)}),
        [](auto& z) { return scroll_wait(z,true); });

    // The item name line, the money printers, the digit helper and the shop line.
    mk::add_segment(registry,at0(0x356C),
        mk::segment(menu::item_name_line_unit,{mk::core_final_at(0x35B2,-4)},{.scratch_stack=8}),
        [](auto& z) { return item_name_line(z); });
    mk::add_segment(registry,at0(0x35BD),
        mk::segment(menu::money_wide_label_unit,{mk::core_final_at(0x3631)},{.scratch_stack=6}),
        [](auto& z) { return money_wide_label(z); });
    mk::add_segment(registry,at0(0x35C5),
        mk::segment(menu::money_unit,{mk::core_final_at(0x3631)},{.scratch_stack=6}),
        [](auto& z) { return money(z); });
    mk::add_segment(registry,at0(0x3645),
        mk::segment(menu::digit_unit,{mk::returned_at(digit_return)},{.scratch_stack=2}),
        [](auto& z) { digit_body(z); return returns(digit_return,return_cycles); });
    mk::add_segment(registry,at0(0x393B),
        mk::segment(menu::shop_line_unit,{mk::core_final_at(0x3631)},{.scratch_stack=6}),
        [](auto& z) { return shop_line(z); });

    // Drawing a window one row per frame.
    mk::add_segment(registry,at0(0x3A57),
        mk::segment(menu::draw_window_unit,{mk::core_final_at(0x3A60,-2)},{.slot2_mapper=true,.scratch_stack=2}),
        [](auto& z) { return draw_window(z); });
    mk::add_segment(registry,at0(0x3A63),
        mk::segment(menu::draw_window_unit,{mk::returned_at(0x3A67,+2)},{.slot2_mapper=true,.inherited_stack=2}),
        [](auto& z) { return restore_bank(z,0x3A63,0x3A64); });
    mk::add_segment(registry,at0(0x3A6A),
        mk::segment(menu::row_drawer_unit,{mk::core_final_at(0x3A78)},{.scratch_stack=2}),
        [](auto& z) { return row(z); });
    mk::add_segment(registry,at0(0x3A7F),
        mk::segment(menu::row_drawer_unit,{mk::conditional_at(0x3A80,+2)},{.inherited_stack=2}),
        [](auto& z) { return next_row(z); });

    // Drawing a window at once.
    mk::add_segment(registry,at0(0x3A83),
        mk::segment(menu::draw_window_at_once_unit,{mk::core_final_at(0x3A87,-2)},{.scratch_stack=2}),
        [](auto& z) { return at_once_start(z); });
    mk::add_segment(registry,at0(0x3A8D),
        mk::segment(menu::draw_window_at_once_unit,{mk::conditional_at(0x3A94,-2)},{.scratch_stack=4}),
        [](auto& z) { return at_once_byte(z,true); });
    mk::add_segment(registry,at0(0x3A92),mk::segment(menu::draw_window_at_once_unit,{mk::conditional_at(0x3A94)}),
        [](auto& z) { return at_once_byte(z,false); });
    mk::add_segment(registry,at0(0x3AA2),
        mk::segment(menu::draw_window_at_once_unit,{mk::returned_at(0x3AA5)},{.slot2_mapper=true}),
        [](auto& z) { return restore_bank(z,0,0x3AA2); });

    // Saving what lies under a window.
    mk::add_segment(registry,at0(0x3AA7),
        mk::segment(menu::save_under_unit,{mk::conditional_loop_at(0x3AB4,save_after,-6)},{.scratch_stack=8}),
        [](auto& z) { return save_under(z,Save::start); });
    mk::add_segment(registry,at0(0x3AAB),
        mk::segment(menu::save_under_unit,{mk::conditional_loop_at(0x3AB4,save_after,-2)},{.scratch_stack=4}),
        [](auto& z) { return save_under(z,Save::row); });
    mk::add_segment(registry,at0(0x3AB0),
        mk::segment(menu::save_under_unit,{mk::conditional_loop_at(0x3AB4,save_after),mk::conditional_at(0x3ABE,+2)},
                    {.scratch_stack=2,.inherited_stack=2}),
        [](auto& z) { return save_under(z,Save::inside); });
    mk::add_segment(registry,at0(0x3AB7),
        mk::segment(menu::save_under_unit,{mk::conditional_at(0x3ABE,+2)},{.inherited_stack=2}),
        [](auto& z) { return save_under(z,Save::end); });
}
void register_window_rows(RoutineRegistry& registry) {
    // Dialogue reuses the current destination for the final strip of tiles.
    auto last_load=mk::core_final_at(0x3454);
    last_load.native_frame_tail=true;
    mk::add_segment(registry,window_row_entries[0],mk::segment(menu::dialogue_scroll_unit,
        {last_load}),[](auto& z) {
            z.load_hl(0x3451);
            return mk::continues(0x3454,10);
        });
    mk::add_segment(registry,window_row_entries[1],mk::segment(menu::dialogue_scroll_unit,
        {mk::core_final_at(0x3457)},{.scratch_stack=2}),[](auto& z) {
            z.load_bc(0x3454);
            return mk::continues(0x3457,mk::call_cycles);
        });
    mk::add_segment(registry,window_row_entries[2],mk::segment(menu::draw_window_at_once_unit,
        {mk::core_final_at(0x3A9B)}),[](auto& z) {
            z.exchange_de_hl(0x3A97);
            z.load_bc(0x3A98);
            return mk::continues(0x3A9B,11);
        });
    mk::add_segment(registry,window_row_entries[3],mk::segment(menu::draw_window_at_once_unit,
        {mk::conditional_at(0x3A9E,2)},{.inherited_stack=2}),[](auto& z) {
            z.exchange_de_hl(0x3A9C);
            z.pop(0x3A9D,z.r.b,z.r.c);
            return mk::decrement_jump_exit(z,0x3A9E);
        });
}

}
