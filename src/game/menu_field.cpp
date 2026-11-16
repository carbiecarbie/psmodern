// SPDX-License-Identifier: GPL-3.0-or-later
// Menus and windows (milestone 12), part field:
// The field menu 00:1BE1 and its commands: status 00:1CA1, save 00:1CDF (the
// cartridge-RAM copy 00:1D0E-00:1D32 is milestone 13's, in save_menu.cpp), magic 00:1D4D, items
// 00:2168 with use 00:2201, equip 00:26C8 and drop 00:2752, the inventory
// helpers 00:277C, 00:277F, 00:279F and 00:282E, search 00:2839 and chests
// 00:28DB (with the trap 00:2950 and its per-member step 00:2966).
// Planned segments: menu::field_segments in game/menu.h, rendered with
// game/menu_kit.h.
//
// How the code is cut. Every command of the field menu is a chain of the same
// few moves: open a window (a call into the windows part), wait for a button,
// read a choice, read or change a few bytes of the party's records, and open
// the next window. The windows, the choosing and the text are other parts'
// routines, so every call into them is
// where a segment ends: the segment does the arithmetic, the table lookups and
// the tests around those calls and hands the call itself to the Z80 core. The
// instruction after a call is the next entry (the registration at the end of
// the file lists each one with the call it follows). The pieces between calls
// are short (the longest is the inventory removal, one memory copy of at most
// 32 bytes), so a segment never comes close to a frame.
//
// So below, each routine of the original is written as one C++ function per
// stretch between two calls, in the order the player meets them, and the
// original's jumps are plain if/else and early returns. A function that ends
// "return call_at(x)" stops at the call at x; the core runs it. The addresses
// passed to the kit are the original's, because the kit reads each
// instruction's operands and cycle cost from the player's ROM; no ROM value is
// written here.
//
// The routines of this file:
//   00:1BE1  the field menu. It clears the "what to do next" byte (C2D8) and
//            the last result (C29D), draws the command window and the party
//            window, then loops: read a command with the cursor (the choosing
//            part's routine), run it through the command table (STATS, MAGIC,
//            ITEM, SEARCH, SAVE are the five entries), redraw. Button 1 (or a
//            nonzero C2D8 set by a command) leaves with a result: FFh back to
//            the game, 0-2 nothing more, 3-4 open a door (dungeon), 5 a
//            palette effect and a scene, 6 a second palette effect then the
//            menu again, 7 and up a scene or the map transfer record of the
//            current location.
//   00:1CA1  STATS: choose a member, show the equipment window and the status
//            window, wait, then (for members who know battle magic) show their
//            spell list too; the fourth member (Noah, number 3) is drawn in
//            the third member's row (2) of that list.
//   00:1CDF  SAVE: the confirmation texts, a yes/no, the slot of the cartridge
//            RAM chosen by the dialogue number, the copy of the 1,024 bytes
//            at C300h (save_menu.cpp) and the "saved" / "now play on" ending. Only a
//            save into a slot that was unused leaves the field menu
//            altogether, by dropping two return addresses (`pop hl; pop hl;
//            ret`); a save over a slot in use shows a message and stays.
//   00:1D4D  MAGIC: choose a member, refuse a dead member, the third member
//            (Odin, number 2) or one with no map spells, list the spells,
//            choose one, check the points (00:1B87) and jump through the spell
//            table (the recovery, battle and field-magic families own the effects).
//   00:2168  ITEM: list the items, handle the vehicle special case (an item
//            for a vehicle the party is already using can put it away), pick
//            USE / EQUIP / DROP and run it.
//   00:26C8  EQUIP, 00:2752 DROP, 00:277C-00:27BB the inventory helpers
//            (remove the selected item, add one) and 00:282E (does the party
//            carry this item?).
//   00:2839  SEARCH: a chest in front of the party runs the chest routine;
//            two fixed places in two locations hand out an item, two scene
//            bytes start a scene, anything else says "nothing here".
//   00:28DB  chests: ask, open, trap damage, money and item reward.
#include "game/menu.h"
#include "game/menu_kit.h"
#include "game/routines.h"
#include <utility>

namespace psmodern::game {
namespace {
namespace mk=menu_kit;
namespace menu=game::menu;
using mk::Exit;

constexpr unsigned button_one=4;   // `bit 4,c`: the pressed-button byte's first-button bit (the ROM's constant)

// The ways a segment ends. All of them name the original's address of the
// final instruction and charge its cycles; the core runs the instruction.
Exit call_at(std::uint16_t at) { return mk::continues(at,mk::call_cycles); }
Exit call_if_at(std::uint16_t at, bool taken) { return mk::continues(at,taken ? mk::call_cycles : mk::untaken_call_cycles); }
Exit jump_at(std::uint16_t at) { return mk::continues(at,mk::jump_cycles); }
Exit return_at(std::uint16_t at) { return mk::returns(at,mk::return_cycles); }
// `ld hl,nn` (the text to show) at `at`, then the bare call of the dialogue
// routine right after it.
template<class Z> Exit say(Z& z, std::uint16_t at) { z.load_hl(at); return call_at(static_cast<std::uint16_t>(at+3)); }

// ---- The field menu: 00:1BE1-1C79 --------------------------------------------------------------
// 00:1BE1: opening. The result byte of the last command (C29D) and the "what
// to do next" byte (C2D8) start at zero; the command window (00:36DD) and the
// party window (00:2ED9) are then drawn by the windows part.
template<class Z> Exit field_menu_open(Z& z) {
    z.xor_a(0x1BE1);
    z.store_a_direct(0x1BE2);                                        // C29D
    z.store_a_direct(0x1BE5);                                        // C2D8
    return call_at(0x1BE8);                                          // 00:36DD
}
// 00:1C15-1C23: leaving with the result in A, which waits on the stack. Unless the
// result is 5 (the palette effect needs the party's picture), the first object
// slot's state (C800) is cleared and the first sprite's Y byte (C900) is set to
// the end marker, so the party's sprite is not drawn any more. Then the party
// window (00:30B7) and the command window (00:36FB) are closed.
template<class Z> Exit field_menu_leave(Z& z) {
    auto& r=z.r;
    z.push(0x1C15,r.a,r.f);
    z.compare_immediate(0x1C16);
    if (!z.relative_if(0x1C18,z.zero())) {
        z.xor_a(0x1C1A);
        z.store_a_direct(0x1C1B);                                    // C800
        z.load_immediate(0x1C1E,r.a);
        z.store_a_direct(0x1C20);                                    // C900
    }
    return call_at(0x1C23);                                          // 00:30B7, then 00:36FB
}
// 00:1BEE: the top of the loop. A nonzero C2D8 (an item or a spell asks the menu
// to end) leaves at once with that value. Otherwise the cursor goes to the first
// command, the number of commands (minus one) is set and the cursor loop
// (00:2D51) is called.
template<class Z> Exit field_menu_loop(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x1BEE);                                         // C2D8
    z.or_a(0x1BF1);
    if (z.relative_if(0x1BF2,!z.zero())) return field_menu_leave(z);
    z.load_hl(0x1BF4);
    z.store_hl_direct(0x1BF7);                                       // the cursor position
    z.load_immediate(0x1BFA,r.a);
    z.store_a_direct(0x1BFC);                                        // the number of choices
    return call_at(0x1BFF);                                          // 00:2D51
}
// 00:1C02: after the choice. Button 1 leaves with FFh; anything else jumps
// through the command table (STATS, MAGIC, ITEM, SEARCH, SAVE), which the core
// does.
template<class Z> Exit field_menu_chosen(Z& z) {
    auto& r=z.r;
    z.test_bit(0x1C02,button_one,r.c);
    if (z.jump_if(0x1C04,!z.zero())) {
        z.load_immediate(0x1C13,r.a);                                // FFh
        return field_menu_leave(z);
    }
    z.load_hl(0x1C07);
    return call_at(0x1C0A);                                          // GetPtrAndJump on the command table
}
// 00:1C29-1C5B: the windows are closed; what ends the menu decides what comes next.
// FFh and 0-2 return to the game. 3 and 4 open a door (the value goes to C). 5 is
// a palette effect, then a scene. 6 is a sound, a second palette effect and the
// menu opens again. 7 is a scene (the `jp c` at 00:1C5B, which the core decides);
// 8 and up move the party through a map transfer record (00:1C5E, below).
template<class Z> Exit field_menu_result(Z& z) {
    auto& r=z.r;
    z.pop(0x1C29,r.a,r.f);
    z.compare_immediate(0x1C2A);                                     // FFh
    if (z.zero()) return mk::returns(0x1C2C,mk::taken_conditional_return_cycles);
    z.return_not_taken(0x1C2C);
    z.compare_immediate(0x1C2D);                                     // 0-2
    if (z.carry()) return mk::returns(0x1C2F,mk::taken_conditional_return_cycles);
    z.return_not_taken(0x1C2F);
    z.compare_immediate(0x1C30);                                     // 3 or 4: the door
    if (!z.relative_if(0x1C32,!z.carry())) {
        z.copy(0x1C34,r.c,r.a);
        return jump_at(0x1C35);                                      // 01:681B
    }
    z.compare_immediate(0x1C38);                                     // 5: the palette effect
    if (!z.relative_if(0x1C3A,!z.carry())) return call_at(0x1C3C);   // 01:7C85
    z.compare_immediate(0x1C45);                                     // 6: a sound request, the second effect
    if (!z.relative_if(0x1C47,!z.carry())) {
        z.load_immediate(0x1C49,r.a);
        return call_at(0x1C4B);                                      // 00:0C97 (sound)
    }
    z.compare_immediate(0x1C59);                                     // 7 jumps to the scene
    return mk::jump_if_exit(0x1C5B);
}
// 00:1C51: after the second palette effect of result 6: C2DC is set to FFh and
// the whole menu opens again (a jump back to 00:1BE1).
template<class Z> Exit field_menu_again(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x1C51,r.a);
    z.store_a_direct(0x1C53);                                        // C2DC
    (void)z.jump(0x1C56);
    return field_menu_open(z);
}
// 00:1C5E: 8 and up. A sound request, the game mode set to 8 (map loading),
// the transport byte (C30E) cleared (on foot), then the three-byte transfer
// record chosen by C317 (three times its value from the table of records)
// is handed to the map code by a jump.
template<class Z> Exit field_menu_transfer(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x1C5E,r.a);
    z.store_a_direct(0x1C60);                                        // the sound request
    z.load_hl(0x1C63);
    z.store_to_hl(0x1C66);                                           // game mode 8
    z.xor_a(0x1C68);
    z.store_a_direct(0x1C69);                                        // C30E
    z.load_a_direct(0x1C6C);                                         // C317
    z.copy(0x1C6F,r.l,r.a);
    z.add_register(0x1C70,r.a);
    z.add_register(0x1C71,r.l);                                      // times three
    z.load_immediate(0x1C72,r.h);
    z.copy(0x1C74,r.l,r.a);
    z.load_de(0x1C75);
    z.add_to_hl(0x1C78,z.de());                                      // into the table of records
    return jump_at(0x1C79);                                          // MapApplyTransferRecord
}

// ---- STATS: 00:1CA1-1CDC -----------------------------------------------------------------------
// Closing is a jump to the member-choice close (00:36BB), which the core runs.
template<class Z> Exit stats_close(Z&) { return jump_at(0x1CDC); }
// 00:1CA4: after the member choice (00:3665). Button 1 closes.
template<class Z> Exit stats_chosen(Z& z) {
    z.test_bit(0x1CA4,button_one,z.r.c);
    if (z.relative_if(0x1CA6,!z.zero())) return stats_close(z);
    return call_at(0x1CA8);                                          // 00:188E: is the member alive?
}
// 00:1CAB: a member who is not alive (the check answered zero and said so itself)
// closes. Otherwise the member number is kept on the stack while the equipment
// window (00:3707) and the status window (00:37CF) are drawn and a button is waited for.
template<class Z> Exit stats_alive(Z& z) {
    if (z.relative_if(0x1CAB,z.zero())) return stats_close(z);
    z.push(0x1CAD,z.r.a,z.r.f);
    return call_at(0x1CAE);                                          // 00:3707
}
// 00:1CB7: after the wait. The member's record is 16 bytes wide: the count of
// battle spells it knows is one byte of it. A member who knows none goes straight
// to closing the status window (00:1CD6). Otherwise the spell list window
// (00:3478) is drawn with A the page start (the member) and B the number of rows;
// the fourth member (number 3) is shown in the third member's row, so a member
// number of 3 becomes 2.
template<class Z> Exit stats_after_wait(Z& z) {
    auto& r=z.r;
    z.pop(0x1CB7,r.a,r.f);
    z.copy(0x1CB8,r.c,r.a);
    z.add_register(0x1CB9,r.a);
    z.add_register(0x1CBA,r.a);
    z.add_register(0x1CBB,r.a);
    z.add_register(0x1CBC,r.a);                                      // member times 16
    z.load_hl(0x1CBD);
    z.add_register(0x1CC0,r.l);
    z.copy(0x1CC1,r.l,r.a);
    z.load_from_hl(0x1CC2,r.a);                                      // battle spells known
    z.or_a(0x1CC3);
    if (z.relative_if(0x1CC4,z.zero())) return call_at(0x1CD6);      // 00:38C1: close the status window
    z.copy(0x1CC6,r.b,r.a);
    z.copy(0x1CC7,r.a,r.c);
    z.compare_immediate(0x1CC8);                                     // the fourth member
    if (!z.relative_if(0x1CCA,!z.zero())) z.decrement(0x1CCC,r.a);
    return call_at(0x1CCD);                                          // 00:3478
}

// ---- SAVE: 00:1CDF-1D4C ------------------------------------------------------------------------
// The messages and windows come first: a text, the slot window (00:39A5), a
// second text, and a yes/no question (00:2D19). Each call below is one
// stretch of that.
// 00:1CDF and 00:1CE8: the first and the second text.
template<class Z> Exit save_first_text(Z& z) { return say(z,0x1CDF); }
template<class Z> Exit save_second_text(Z& z) { return say(z,0x1CE8); }
// 00:1CF1: after the yes/no. "No" closes the slot window and the dialogue
// (00:39DD, then 00:3464). "Yes" keeps the game mode in C316 and shows a third
// text.
template<class Z> Exit save_answer(Z& z) {
    if (z.relative_if(0x1CF1,!z.zero())) return call_at(0x1D3B);     // 00:39DD
    z.load_a_direct(0x1CF3);                                         // the game mode
    z.store_a_direct(0x1CF6);                                        // C316
    return say(z,0x1CF9);
}
// 00:1CFF: after the third text. The slot's base address in cartridge RAM is
// 8000h plus the dialogue number (the slot the window chose) times 400h; DE
// gets it and 00:073A reads the slot's used flag, answering "zero" for unused.
// Then the native stretch 00:1D0E-1D32 clears the flag, copies the 1,024 bytes
// at C300h into the slot and sets the flag again (the cartridge RAM is switched
// in and out for each write, which the automap's backup watches).
template<class Z> Exit save_slot_address(Z& z) {
    auto& r=z.r;
    z.push(0x1CFF,r.b,r.c);
    z.load_a_direct(0x1D00);                                         // the slot (dialogue number)
    z.copy(0x1D03,r.h,r.a);
    z.load_immediate(0x1D04,r.l);
    z.add_to_hl(0x1D06,z.hl());
    z.add_to_hl(0x1D07,z.hl());                                      // times 400h
    z.set_bit(0x1D08,7,r.h);                                         // plus 8000h
    z.exchange_de_hl(0x1D0A);
    return call_at(0x1D0B);                                          // 00:073A
}
// 00:1D33: after the copy; the zero flag is still the one the used-flag read left.
// A slot that was in use shows a fourth text and closes the slot window and the
// dialogue. A slot that was unused ends the field menu for good: C780 is
// cleared, the game mode is set to 10h, and the routine drops its own return
// address and the menu's before returning.
template<class Z> Exit leave_saved_game(Z& z) {
    auto& r=z.r;
    z.xor_a(0x1D41);
    z.store_a_direct(0x1D42);                                        // C780
    z.load_hl(0x1D45);
    z.store_to_hl(0x1D48);                                           // game mode 10h
    z.pop(0x1D4A,r.h,r.l);
    z.pop(0x1D4B,r.h,r.l);
    return return_at(0x1D4C);
}

template<class Z> Exit save_done(Z& z) {
    return z.relative_if(0x1D33,z.zero()) ? leave_saved_game(z) : say(z,0x1D35);
}

// ---- MAGIC: 00:1D4D-1DDB -----------------------------------------------------------------------
// The ends. 00:1DBA closes the member choice window and redraws the party window
// (the core does both); 00:1DB7 closes the spell list first. A message that
// refuses the choice is shown (the call at 00:1DC8) and the dialogue and the
// member window are closed after it by the core.
template<class Z> Exit magic_leave(Z&) { return call_at(0x1DBA); }
template<class Z> Exit magic_close_list(Z&) { return call_at(0x1DB7); }
// 00:1D50: after the member choice (00:3665). Button 1 leaves.
template<class Z> Exit magic_chosen(Z& z) {
    z.test_bit(0x1D50,button_one,z.r.c);
    if (z.jump_if(0x1D52,!z.zero())) return magic_leave(z);
    return call_at(0x1D55);                                          // 00:188E: is the member alive?
}
// 00:1D58: after the alive check. A dead member leaves. Member 2 (Odin) is told
// he cannot cast (the text at 00:1DC5). For anyone else the member is
// remembered (C2C2 and C267), and the count of spells for the map, one byte of
// the member's 16-byte record, decides: none gives another text (00:1DC0).
// Otherwise the list window (00:3478) opens with B the count, A the page plus 3
// and C the page (the fourth member is drawn in the third member's row, so 3
// becomes 2); the record pointer and the page are held across the call.
template<class Z> Exit magic_member(Z& z) {
    auto& r=z.r;
    if (z.jump_if(0x1D58,z.zero())) return magic_leave(z);
    z.compare_immediate(0x1D5B);                                     // member 2
    if (z.jump_if(0x1D5D,z.zero())) {
        z.load_hl(0x1DC5);
        return call_at(0x1DC8);                                      // the text; the core closes the windows
    }
    z.copy(0x1D60,r.c,r.a);
    z.store_a_direct(0x1D61);                                        // C2C2, the current character
    z.store_a_direct(0x1D64);                                        // C267
    z.add_register(0x1D67,r.a);
    z.add_register(0x1D68,r.a);
    z.add_register(0x1D69,r.a);
    z.add_register(0x1D6A,r.a);                                      // member times 16
    z.load_hl(0x1D6B);
    z.add_register(0x1D6E,r.l);
    z.copy(0x1D6F,r.l,r.a);
    z.load_from_hl(0x1D70,r.a);                                      // the count of map spells
    z.or_a(0x1D71);
    if (z.jump_if(0x1D72,z.zero())) {                                // none
        z.load_hl(0x1DC0);
        z.relative(0x1DC3);
        return call_at(0x1DC8);
    }
    z.copy(0x1D75,r.b,r.a);
    z.copy(0x1D76,r.a,r.c);
    z.compare_immediate(0x1D77);                                     // the fourth member
    if (!z.relative_if(0x1D79,!z.zero())) z.decrement(0x1D7B,r.a);
    z.copy(0x1D7C,r.c,r.a);
    z.add_immediate(0x1D7D);
    z.push(0x1D7F,r.b,r.c);
    z.push(0x1D80,r.h,r.l);
    return call_at(0x1D81);                                          // 00:3478
}
// 00:1D84: the list is open. The cursor goes to its first row, and the number of
// choices is the member's count of map spells (from the record pointer held on
// the stack) minus one; the cursor loop (00:2D51) runs.
template<class Z> Exit magic_list_open(Z& z) {
    auto& r=z.r;
    z.load_hl(0x1D84);
    z.store_hl_direct(0x1D87);                                       // the cursor position
    z.pop(0x1D8A,r.h,r.l);
    z.load_from_hl(0x1D8B,r.a);
    z.decrement(0x1D8C,r.a);
    z.store_a_direct(0x1D8D);                                        // the number of choices
    return call_at(0x1D90);
}
// 00:1D93: the spell is chosen. Button 1 closes the list. Otherwise the choice
// (in A) and the member's page (the popped C) index the table of spells for the
// map, five slots to a member: slot = page times five plus the choice. The low
// five bits of the byte are the spell number (the upper three are ignored); B
// gets it and 00:1B87 answers the member's magic points (C267) minus its cost
// in A, with carry when they are not enough (it changes nothing).
template<class Z> Exit magic_spell_chosen(Z& z) {
    auto& r=z.r;
    z.pop(0x1D93,r.h,r.l);
    z.test_bit(0x1D94,button_one,r.c);
    if (z.jump_if(0x1D96,!z.zero())) return magic_close_list(z);
    z.copy(0x1D99,r.h,r.a);                                          // the choice
    z.copy(0x1D9A,r.a,r.l);                                          // the page
    z.add_register(0x1D9B,r.a);
    z.add_register(0x1D9C,r.a);
    z.add_register(0x1D9D,r.l);
    z.add_register(0x1D9E,r.h);
    z.copy(0x1D9F,r.l,r.a);
    z.load_immediate(0x1DA0,r.h);
    z.load_de(0x1DA2);
    z.add_to_hl(0x1DA5,z.de());                                      // into the table of spells
    z.load_from_hl(0x1DA6,r.a);
    z.and_immediate(0x1DA7);                                         // the low five bits
    z.copy(0x1DA9,r.b,r.a);
    return call_at(0x1DAA);                                          // 00:1B87
}
// 00:1DAD: too few points shows a text (00:1DD1) and then closes the list;
// otherwise the spell's own routine runs through the jump table (the core's
// dispatcher at 00:00E6, then the registered spell-effect family).
template<class Z> Exit magic_cost_checked(Z& z) {
    auto& r=z.r;
    if (z.jump_if(0x1DAD,z.carry())) {
        z.load_hl(0x1DD1);
        return call_at(0x1DD4);
    }
    z.copy(0x1DB0,r.a,r.b);
    z.load_hl(0x1DB1);
    return call_at(0x1DB4);
}
// 00:1DDA: after the "not enough points" text is closed, the list is closed too
// (a short jump to 00:1DB7).
template<class Z> Exit magic_after_refusal(Z& z) {
    z.relative(0x1DDA);
    return magic_close_list(z);
}

// ---- ITEM: 00:2168-21F5, and USE 00:2201 -------------------------------------------------------
// 00:2168: the number of items (C4E2). With none, the list window is opened and
// closed again without a choice (the call at 00:216F); otherwise the item list
// (00:34D5) opens and the player chooses.
template<class Z> Exit item_start(Z& z) {
    z.load_a_direct(0x2168);
    z.or_a(0x216B);
    if (z.jump_if(0x216C,!z.zero())) return call_at(0x2175);
    return call_at(0x216F);
}
// 00:21C3: the members are tried from number 0 to 3 (B counts 4 down to 1, so
// A = 4 - B) with the routine that tests whether a member is alive; it answers
// nonzero for a living member.
template<class Z> Exit item_next_member(Z& z) {
    auto& r=z.r;
    z.copy(0x21C3,r.a,r.b);
    z.subtract_immediate(0x21C4);
    z.negate(0x21C6);
    return call_at(0x21C8);
}
template<class Z> Exit item_first_member(Z& z) {                     // 00:21C1
    z.load_immediate(0x21C1,z.r.b);
    return item_next_member(z);
}
// 00:2178: after the item is chosen. Button 1 closes the list. An item from 21h to
// 23h is a vehicle. If the party travels in that vehicle (the transport byte
// C30E is four times the vehicle number plus four, kept in B) the menu looks
// for a place to put it down: the map code (01:7656 for the ground vehicles,
// 01:7732 when the transport byte is 8, the hovercraft) is called. Every other
// case goes on to choose who uses the item.
template<class Z> Exit item_chosen(Z& z) {
    auto& r=z.r;
    z.test_bit(0x2178,button_one,r.c);
    if (z.jump_if(0x217A,!z.zero())) return call_at(0x21F5);         // 00:3656: close the list
    z.load_a_direct(0x217D);                                         // CurrentItem
    z.compare_immediate(0x2180);
    if (z.relative_if(0x2182,z.carry())) return item_first_member(z);
    z.compare_immediate(0x2184);
    if (z.relative_if(0x2186,!z.carry())) return item_first_member(z);
    z.subtract_immediate(0x2188);
    z.add_register(0x218A,r.a);
    z.add_register(0x218B,r.a);
    z.add_immediate(0x218C);
    z.copy(0x218E,r.b,r.a);                                          // the transport byte for that vehicle
    z.load_a_direct(0x218F);                                         // C30E
    z.or_a(0x2192);
    if (z.relative_if(0x2193,z.zero())) return item_first_member(z);   // on foot
    z.compare_register(0x2195,r.b);
    if (z.relative_if(0x2196,!z.zero())) return item_first_member(z);  // in another vehicle
    z.compare_immediate(0x2198);                                     // 8: the hovercraft
    if (z.relative_if(0x219A,z.zero())) {
        z.push(0x21A3,r.b,r.c);
        return call_at(0x21A4);                                      // 01:7732
    }
    z.push(0x219C,r.b,r.c);
    return call_at(0x219D);                                          // 01:7656
}
// 00:21A8: after the search for a spot, whose zero flag says it found one. With a
// spot the party is put on foot (C30E cleared), the menu is told to end (C2D8 set
// to FFh) and the "got out" text (00:21B5) is chosen; without one the "no room"
// text (00:21A8) stays. The core shows it and closes the list.
template<class Z> Exit item_vehicle_result(Z& z) {
    auto& r=z.r;
    z.load_hl(0x21A8);
    if (!z.relative_if(0x21AB,!z.zero())) {
        z.xor_a(0x21AD);
        z.store_a_direct(0x21AE);                                    // C30E
        z.decrement(0x21B1,r.a);
        z.store_a_direct(0x21B2);                                    // C2D8
        z.load_hl(0x21B5);
    }
    return call_at(0x21B8);
}
template<class Z> Exit item_ground_vehicle_done(Z& z) {              // 00:21A0, after 01:7656
    z.pop(0x21A0,z.r.b,z.r.c);
    z.relative(0x21A1);
    return item_vehicle_result(z);
}
template<class Z> Exit item_hovercraft_done(Z& z) {                  // 00:21A7, after 01:7732
    z.pop(0x21A7,z.r.b,z.r.c);
    return item_vehicle_result(z);
}
// 00:21CB: after the alive test of member A. A living one becomes the current
// character (C2C2) and the item action window (00:3759) opens. When none of the
// four lives, the list is closed (00:21F5).
template<class Z> Exit item_member_found(Z& z) {
    if (z.jump_if(0x21CB,!z.zero())) {
        z.store_a_direct(0x21D3);
        return call_at(0x21D6);                                      // 00:3759
    }
    if (z.decrement_jump(0x21CE)) return item_next_member(z);
    (void)z.jump(0x21D0);
    return call_at(0x21F5);
}
// 00:21D9: the action window is open; the cursor goes to its first row, three
// choices (use, equip, drop; the count is the last index), and the cursor loop
// runs.
template<class Z> Exit item_action_window(Z& z) {
    z.load_hl(0x21D9);
    z.store_hl_direct(0x21DC);                                       // the cursor position
    z.load_immediate(0x21DF,z.r.a);
    z.store_a_direct(0x21E1);                                        // the number of choices
    return call_at(0x21E4);                                          // 00:2D51
}
// 00:21E7: button 1 closes the action window (00:376B) without acting;
// otherwise the choice goes through the table (use 00:2201, equip 00:26C8, drop
// 00:2752), run by the core, which comes back to close the window.
template<class Z> Exit item_action_chosen(Z& z) {
    z.test_bit(0x21E7,button_one,z.r.c);
    if (z.jump_if(0x21E9,!z.zero())) return call_at(0x21F2);
    z.load_hl(0x21EC);
    return call_at(0x21EF);
}
// 00:2201: USE. Only a jump through the table of the 64 item effects, indexed by
// the item number; the effect families own the selected action.
template<class Z> Exit item_use(Z& z) {
    z.load_a_direct(0x2201);
    z.load_hl(0x2204);
    return jump_at(0x2207);
}

// ---- EQUIP: 00:26C8-2750 and DROP: 00:2752-2773 ------------------------------------------------
// The property byte of the chosen item, in A, with HL pointing at it: slot 2
// switched to bank 2 (the byte at FFFFh), then the table indexed by the item.
// Its upper four bits say which members may equip it (one bit each), the low two
// bits which of the weapon, armour and shield bytes of the record it goes to,
// and bit 2 means it cannot be dropped. `at` is the address of the first
// instruction; equip, drop and the swap in the inventory each have the same
// ten instructions.
template<class Z> void item_property(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.load_hl(at);
    z.store_to_hl(at+3);
    z.load_a_direct(at+5);                                           // CurrentItem
    z.load_hl(at+8);
    z.add_register(at+11,r.l);
    z.copy(at+12,r.l,r.a);
    z.add_with_carry(at+13,r.h);
    z.subtract_register(at+14,r.l);
    z.copy(at+15,r.h,r.a);
    z.load_from_hl(at+16,r.a);
}
// 00:26C8: the members that may equip this item are the upper four bits. None
// shows a text; otherwise they go to D, and D and the property pointer are held
// while the member choice (00:3665) runs.
template<class Z> Exit equip_start(Z& z) {
    auto& r=z.r;
    item_property(z,0x26C8);
    z.rotate_right_circular_a(0x26D9);
    z.rotate_right_circular_a(0x26DA);
    z.rotate_right_circular_a(0x26DB);
    z.rotate_right_circular_a(0x26DC);
    z.and_immediate(0x26DD);
    if (z.jump_if(0x26DF,!z.zero())) {
        z.copy(0x26EB,r.d,r.a);
        z.push(0x26EC,r.d,r.e);
        z.push(0x26ED,r.h,r.l);
        return call_at(0x26EE);                                      // 00:3665
    }
    z.load_hl(0x26E2);
    return call_at(0x26E5);
}
// 00:26F1: the member is chosen; button 1 ends the equip (00:2741). Otherwise
// the alive check 00:188E is called.
template<class Z> Exit equip_member_chosen(Z& z) {
    auto& r=z.r;
    z.pop(0x26F1,r.h,r.l);
    z.pop(0x26F2,r.d,r.e);
    z.test_bit(0x26F3,button_one,r.c);
    if (z.jump_if(0x26F5,!z.zero())) return call_at(0x2741);         // the end of equip: UpdateCharStats
    return call_at(0x26F8);
}
// 00:26FB: a dead member ends it. The member (C2C2) is tested against the
// allowed set by rotating the set right member + 1 times: the bit that falls
// into carry is that member's. Clear means a text saying so (00:2747).
// Otherwise DE points at the member's equipment byte (the record's weapon byte
// plus the item's low two property bits): the old item there goes into the
// slot the new one came from (the pointer C29B) and the new item takes its
// place; the old item is held on the stack across the "equipped" text.
template<class Z> Exit equip_member_alive(Z& z) {
    auto& r=z.r;
    if (z.jump_if(0x26FB,z.zero())) return call_at(0x2741);
    z.store_a_direct(0x26FE);                                        // C2C2
    z.copy(0x2701,r.c,r.a);
    z.increment(0x2702,r.a);
    z.copy(0x2703,r.b,r.a);
    z.copy(0x2704,r.a,r.d);
    do { z.rotate_right_circular_a(0x2705); } while (z.decrement_jump(0x2706));
    if (z.jump_if(0x2708,!z.carry())) return say(z,0x2747);          // the member cannot use it
    z.copy(0x270B,r.a,r.c);
    z.add_register(0x270C,r.a);
    z.add_register(0x270D,r.a);
    z.add_register(0x270E,r.a);
    z.add_register(0x270F,r.a);                                      // member times 16
    z.load_de(0x2710);
    z.add_register(0x2713,r.e);
    z.copy(0x2714,r.e,r.a);
    z.add_with_carry(0x2715,r.d);
    z.subtract_register(0x2716,r.e);
    z.copy(0x2717,r.d,r.a);
    z.load_from_hl(0x2718,r.a);                                      // the property byte
    z.and_immediate(0x2719);                                         // which equipment byte
    z.add_register(0x271B,r.e);
    z.copy(0x271C,r.e,r.a);
    z.load_a_from_pair(0x271D,z.de());                               // the old item
    z.load_hl_direct(0x271E);                                        // C29B
    z.store_register_to_hl(0x2721,r.a);                              // into the slot of the new one
    z.push(0x2722,r.a,r.f);
    z.load_a_direct(0x2723);                                         // CurrentItem
    z.store_a_to_pair(0x2726,z.de());                                // equipped
    z.load_hl(0x2727);
    return call_at(0x272A);
}
// 00:272D: the equipment window (00:3707) of the current character.
template<class Z> Exit equip_show(Z& z) {
    z.load_a_direct(0x272D);
    return call_at(0x2730);
}
// 00:273C: the equip is done and the dialogue closed. If the old item was
// nothing (0), the slot it left in the list is removed (00:277C, the core).
template<class Z> Exit equip_old_item(Z& z) {
    z.pop(0x273C,z.r.a,z.r.f);
    z.or_a(0x273D);
    return call_if_at(0x273E,z.zero());
}
// 00:2750: after the refusal text. Then the common end: the member's figures
// are recomputed (00:2741) and the member choice closed.
template<class Z> Exit equip_after_refusal(Z& z) {
    z.relative(0x2750);
    return call_at(0x2741);
}
// 00:2752: DROP. An item whose property bit 2 is set cannot be dropped (the
// text at 00:2767); any other shows another (00:2770) and the core then
// removes it (00:277C).
template<class Z> Exit drop_item(Z& z) {
    item_property(z,0x2752);
    z.and_immediate(0x2763);
    if (z.relative_if(0x2765,z.zero())) return say(z,0x2770);
    return say(z,0x2767);
}

// ---- The inventory helpers: 00:277C-2838 -------------------------------------------------------
// Removing the item at HL (00:277F; 00:277C first loads HL from the pointer C29B,
// the selected item's slot). The slots after it slide down one place (the length
// is the end of the 24 slots minus the low byte of HL, masked to five bits; a
// zero length copies nothing), the count (C4E2) is lowered and the new last slot
// cleared.
template<class Z> Exit remove_item(Z& z) {
    auto& r=z.r;
    z.push(0x277F,r.b,r.c);
    z.copy(0x2780,r.e,r.l);
    z.copy(0x2781,r.d,r.h);
    z.increment_pair(0x2782,r.h,r.l);
    z.load_immediate(0x2783,r.a);
    z.subtract_register(0x2785,r.e);
    z.and_immediate(0x2786);
    if (!z.relative_if(0x2788,z.zero())) {
        z.copy(0x278A,r.c,r.a);
        z.load_immediate(0x278B,r.b);
        z.block_copy(0x278D);
    }
    z.load_hl(0x278F);                                               // the first slot
    z.load_a_direct(0x2792);                                         // the count
    z.decrement(0x2795,r.a);
    z.store_a_direct(0x2796);
    z.add_register(0x2799,r.l);
    z.copy(0x279A,r.l,r.a);
    z.store_to_hl(0x279B);                                           // zero into the new last slot
    z.pop(0x279D,r.b,r.c);
    return return_at(0x279E);
}
template<class Z> Exit remove_selected_item(Z& z) {                  // 00:277C
    z.load_hl_direct(0x277C);
    return remove_item(z);
}
// Adding the current item (00:279F). A list with room (fewer than 24) takes it at
// the end, raises the count and asks for the pick-up sound. A full list tells
// so and asks whether to swap (a text, then yes/no, then a second yes/no); the
// questions are the core's, and the answers pick up below.
// 00:27BC: the full list's first text.
template<class Z> Exit add_item_full(Z& z) { return say(z,0x27BC); }
template<class Z> Exit add_item(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x279F);                                         // the count
    z.compare_immediate(0x27A2);
    if (z.relative_if(0x27A4,!z.carry())) return add_item_full(z);
    z.load_hl(0x27A6);
    z.add_register(0x27A9,r.l);
    z.copy(0x27AA,r.l,r.a);
    z.load_a_direct(0x27AB);                                         // CurrentItem
    z.store_register_to_hl(0x27AE,r.a);                              // at the end of the list
    z.load_a_direct(0x27AF);
    z.increment(0x27B2,r.a);
    z.store_a_direct(0x27B3);                                        // the count plus one
    z.load_immediate(0x27B6,r.a);
    z.store_a_direct(0x27B8);                                        // the sound request
    return return_at(0x27BB);
}
// 00:27D8: swapping. The item to add is held on the stack, a text asks which item
// to give up, and the item list (00:34D5, closed by 00:3656) is shown by the
// core.
template<class Z> Exit add_item_replace(Z& z) {
    z.load_a_direct(0x27D8);                                         // CurrentItem
    z.push(0x27DB,z.r.a,z.r.f);
    return say(z,0x27DC);
}
// 00:27C5: after the first yes/no. "Yes" swaps; "no" shows another text and
// asks again with a second yes/no.
template<class Z> Exit add_item_first_answer(Z& z) {
    if (z.relative_if(0x27C5,z.zero())) return add_item_replace(z);
    return say(z,0x27C7);
}
// 00:27D0: after the second yes/no. "No" (nonzero) swaps anyway; "yes" gives the
// item up with a last text (a jump to the dialogue routine).
template<class Z> Exit add_item_second_answer(Z& z) {
    if (z.relative_if(0x27D0,!z.zero())) return add_item_replace(z);
    z.load_hl(0x27D2);
    return jump_at(0x27D5);
}
// 00:27E8: after the player chose which item to give up. Button 1 puts the
// item to add back and starts again from the first text. Otherwise the chosen
// item's property bit 2 decides: set, it cannot be given up (a text says so);
// clear, another text and the swap.
template<class Z> Exit add_item_replace_chosen(Z& z) {
    auto& r=z.r;
    z.test_bit(0x27E8,button_one,r.c);
    if (z.relative_if(0x27EA,!z.zero())) {
        z.pop(0x2827,r.a,r.f);
        z.store_a_direct(0x2828);                                    // CurrentItem
        (void)z.jump(0x282B);
        return add_item_full(z);
    }
    item_property(z,0x27EC);
    z.and_immediate(0x27FD);
    if (z.relative_if(0x27FF,z.zero())) return say(z,0x280E);
    return say(z,0x2801);
}
// 00:2807: the item chosen cannot be given up: the item to add is restored and
// the choice starts again.
template<class Z> Exit add_item_not_droppable(Z& z) {
    z.pop(0x2807,z.r.a,z.r.f);
    z.store_a_direct(0x2808);                                        // CurrentItem
    (void)z.jump(0x280B);
    return add_item_replace(z);
}
// 00:2814: the swap is done. The item to add is restored, put in the chosen slot
// (the pointer C29B), the pick-up sound is requested and a last text is shown
// by a jump.
template<class Z> Exit add_item_swapped(Z& z) {
    auto& r=z.r;
    z.pop(0x2814,r.a,r.f);
    z.store_a_direct(0x2815);                                        // CurrentItem
    z.load_hl_direct(0x2818);                                        // C29B
    z.store_register_to_hl(0x281B,r.a);
    z.load_immediate(0x281C,r.a);
    z.store_a_direct(0x281E);                                        // the sound request
    z.load_hl(0x2821);
    return jump_at(0x2824);
}
// 00:282E: does the party carry the item in A? The 24 slots are compared one by
// one; it returns zero as soon as one matches (a taken `ret z`) and nonzero after
// the last. Despite what its use suggests, it does not look for a free slot.
template<class Z> Exit find_item(Z& z) {
    auto& r=z.r;
    z.load_hl(0x282E);
    z.load_immediate(0x2831,r.b);
    do {
        z.compare_from_hl(0x2833);
        if (z.zero()) return mk::returns(0x2834,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x2834);
        z.increment_pair(0x2835,r.h,r.l);
    } while (z.decrement_jump(0x2836));
    return return_at(0x2838);
}

// ---- SEARCH: 00:2839-28D5 ----------------------------------------------------------------------
// 00:28C5: nothing found. The scene byte (C2DB) decides: A2h and A3h are two scenes
// (jumps that the core decides, at 00:28CA and 00:28CF); anything else shows the
// text "nothing here" (00:28D2).
template<class Z> Exit search_nothing(Z& z) {
    z.load_a_direct(0x28C5);
    z.compare_immediate(0x28C8);
    return mk::jump_if_exit(0x28CA);
}
template<class Z> Exit search_second_scene(Z& z) {                   // 00:28CD
    z.compare_immediate(0x28CD);
    return mk::jump_if_exit(0x28CF);
}
template<class Z> Exit search_nothing_text(Z& z) { return say(z,0x28D2); }
// 00:28B1: an item was found (A): it becomes the current item and the party is
// asked whether it already carries it (00:282E).
template<class Z> Exit search_found(Z& z) {
    z.store_a_direct(0x28B1);
    return call_at(0x28B4);
}
// 00:28B7: after that check. Already carried is "nothing found"; otherwise the
// text of a find (00:28B9), and then the add routine (the core).
template<class Z> Exit search_found_checked(Z& z) {
    if (z.relative_if(0x28B7,z.zero())) return search_nothing(z);
    return say(z,0x28B9);
}
// 00:2839: a chest in front of the party (object state C800 = 0Eh) is not a
// search: the party window is closed (00:30B7), the search text is shown and
// the chest routine runs. Otherwise the party's cell is worked out from the
// camera words C305 and C301 with the arithmetic the map code uses (the
// position, corrected where it passes the edge of the map, times 16, in L and
// H), and two fixed places can give an item:
//   location 7 (C309), cell L=28h, H=1Eh: the story byte C507 must be set and
//     not already FFh; it is set to FFh and the item is 26h.
//   location 1, cell L=30h, H=48h: the story byte C509 must be set and the
//     third member's shield byte must not already be the item (1Fh).
// Anywhere else, or when a condition fails, it is "nothing found".
template<class Z> Exit search_start(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x2839);                                         // C800
    z.compare_immediate(0x283C);
    if (!z.relative_if(0x283E,!z.zero())) return call_at(0x2840);    // the chest: 00:30B7
    z.load_hl_direct(0x284F);                                        // C305
    z.copy(0x2852,r.a,r.l);
    z.add_immediate(0x2853);
    bool wrap=z.relative_if(0x2855,z.carry());
    if (!wrap) {
        z.compare_immediate(0x2857);
        wrap=!z.relative_if(0x2859,z.carry());
    }
    if (wrap) {
        z.add_immediate(0x285B);
        z.increment(0x285D,r.h);
    }
    z.copy(0x285E,r.l,r.a);
    z.add_to_hl(0x285F,z.hl());
    z.add_to_hl(0x2860,z.hl());
    z.add_to_hl(0x2861,z.hl());
    z.add_to_hl(0x2862,z.hl());
    z.copy(0x2863,r.a,r.h);
    z.load_hl_direct(0x2864);                                        // C301
    z.load_bc(0x2867);
    z.add_to_hl(0x286A,z.bc());
    z.add_to_hl(0x286B,z.hl());
    z.add_to_hl(0x286C,z.hl());
    z.add_to_hl(0x286D,z.hl());
    z.add_to_hl(0x286E,z.hl());
    z.copy(0x286F,r.l,r.a);                                          // the cell, in H and L
    z.load_a_direct(0x2870);                                         // C309, the location
    z.compare_immediate(0x2873);
    if (!z.relative_if(0x2875,!z.zero())) {                          // location 7
        z.copy(0x2877,r.a,r.l);
        z.compare_immediate(0x2878);
        if (z.relative_if(0x287A,!z.zero())) return search_nothing(z);
        z.copy(0x287C,r.a,r.h);
        z.compare_immediate(0x287D);
        if (z.relative_if(0x287F,!z.zero())) return search_nothing(z);
        z.load_a_direct(0x2881);                                     // C507
        z.or_a(0x2884);
        if (z.relative_if(0x2885,z.zero())) return search_nothing(z);
        z.compare_immediate(0x2887);
        if (z.relative_if(0x2889,z.zero())) return search_nothing(z);
        z.load_immediate(0x288B,r.a);
        z.store_a_direct(0x288D);                                    // C507 = FFh
        z.load_immediate(0x2890,r.a);                                // the item
        z.relative(0x2892);
        return search_found(z);
    }
    z.compare_immediate(0x2894);                                     // location 1
    if (z.relative_if(0x2896,!z.zero())) return search_nothing(z);
    z.copy(0x2898,r.a,r.l);
    z.compare_immediate(0x2899);
    if (z.relative_if(0x289B,!z.zero())) return search_nothing(z);
    z.copy(0x289D,r.a,r.h);
    z.compare_immediate(0x289E);
    if (z.relative_if(0x28A0,!z.zero())) return search_nothing(z);
    z.load_a_direct(0x28A2);                                         // C509
    z.or_a(0x28A5);
    if (z.relative_if(0x28A6,z.zero())) return search_nothing(z);
    z.load_a_direct(0x28A8);                                         // the third member's shield
    z.copy(0x28AB,r.b,r.a);
    z.load_immediate(0x28AC,r.a);                                    // the item
    z.compare_register(0x28AE,r.b);
    if (z.relative_if(0x28AF,z.zero())) return search_nothing(z);
    return search_found(z);
}
template<class Z> Exit search_chest_text(Z& z) { return say(z,0x2843); }   // 00:2843, then the core runs the chest

// ---- CHESTS: 00:28DB-294D, the trap 00:2950-2960 and its member step 00:2966-2979 ----------------
// 00:28DB: asks "open it?" (a text, then the yes/no window 00:37A3).
template<class Z> Exit chest_ask(Z& z) { return say(z,0x28DB); }
// 00:28E4: the answer is kept while the yes/no window is closed (00:37C3).
template<class Z> Exit chest_hold_answer(Z& z) {
    z.push(0x28E4,z.r.a,z.r.f);
    return call_at(0x28E5);
}
// 00:28EB: after the dialogue is closed. Anything but "yes" returns. "Yes"
// requests the chest-opening sound, marks the chest done by writing FFh through
// the pointer C2E1, sets C80A to 1 (the object engine's request to play the
// opening) and waits for it (00:18B9).
template<class Z> Exit chest_open(Z& z) {
    auto& r=z.r;
    z.pop(0x28EB,r.a,r.f);
    z.or_a(0x28EC);
    if (!z.zero()) return mk::returns(0x28ED,mk::taken_conditional_return_cycles);
    z.return_not_taken(0x28ED);
    z.load_immediate(0x28EE,r.a);
    z.store_a_direct(0x28F0);                                        // the sound request
    z.load_hl_direct(0x28F3);                                        // C2E1
    z.store_to_hl(0x28F6);                                           // FFh
    z.load_immediate(0x28F8,r.a);
    z.store_a_direct(0x28FA);                                        // C80A
    z.push(0x28FD,r.b,r.c);
    return call_at(0x28FE);                                          // 00:18B9
}
// 00:2901: the trap runs unless the object left C80F at 3Dh (a safe chest).
template<class Z> Exit chest_trap_test(Z& z) {
    z.pop(0x2901,z.r.b,z.r.c);
    z.load_a_direct(0x2902);                                         // C80F
    z.compare_immediate(0x2905);
    return call_if_at(0x2907,!z.zero());                             // 00:2950
}
// The end of a chest: the first sprite's Y byte is set to the end marker (the party
// is drawn again by the map), and the dialogue is closed by a jump (00:3464).
// The empty chest (00:291D) and the reward (00:2948) each have their own copy.
template<class Z> Exit chest_end_empty(Z& z) {                       // 00:291D
    z.load_immediate(0x291D,z.r.a);
    z.store_a_direct(0x291F);                                        // C900
    return jump_at(0x2922);
}
template<class Z> Exit chest_end_reward(Z& z) {                      // 00:2948
    z.load_immediate(0x2948,z.r.a);
    z.store_a_direct(0x294A);                                        // C900
    return jump_at(0x294D);
}
// 00:290A: the reward is the money (C2DD) and the item (C2DF) the object left. Both
// zero shows the "empty" text (00:2917). Otherwise the money, also kept as the
// dialogue number, goes to the party (00:297A, the core).
template<class Z> Exit chest_reward(Z& z) {
    z.load_hl_direct(0x290A);
    z.copy(0x290D,z.r.a,z.r.h);
    z.or_register(0x290E,z.r.l);
    if (!z.relative_if(0x290F,!z.zero())) {
        z.load_a_direct(0x2911);
        z.or_a(0x2914);
        if (!z.relative_if(0x2915,!z.zero())) return say(z,0x2917);
    }
    z.load_hl_direct(0x2925);
    z.store_hl_direct(0x2928);                                       // the dialogue number
    return call_at(0x292B);
}
// 00:292E: the amount (still in HL) is announced unless it is zero.
template<class Z> Exit chest_money_text(Z& z) {
    z.copy(0x292E,z.r.a,z.r.h);
    z.or_register(0x292F,z.r.l);
    z.load_hl(0x2930);
    return call_if_at(0x2933,!z.zero());
}
// 00:2936: the item (C2DF) becomes the current one; none skips to the end.
// Otherwise it is announced (00:293F) and added (00:279F, the core).
template<class Z> Exit chest_item(Z& z) {
    z.load_a_direct(0x2936);
    z.store_a_direct(0x2939);
    z.or_a(0x293C);
    if (z.relative_if(0x293D,z.zero())) return chest_end_reward(z);
    return say(z,0x293F);
}

// ---- The trap: 00:2950-2960, and its step for one member: 00:2966-2979 -----------------------
// C80F = 3Eh hurts all four members, counting B from 4 down (the member is B-1);
// any other value (3Dh never gets here) hurts one member picked by the random
// generator (00:05B1) masked to two bits. The step itself starts at 00:2966.
template<class Z> Exit trap_member(Z& z) {                           // 00:2959
    z.copy(0x2959,z.r.a,z.r.b);
    z.decrement(0x295A,z.r.a);
    return call_at(0x295B);
}
template<class Z> Exit trap_start(Z& z) {
    z.load_a_direct(0x2950);                                         // C80F
    z.compare_immediate(0x2953);
    if (z.relative_if(0x2955,!z.zero())) return call_at(0x2961);     // one member, by chance
    z.load_immediate(0x2957,z.r.b);
    return trap_member(z);
}
template<class Z> Exit trap_next_member(Z& z) {                      // 00:295E
    if (z.decrement_jump(0x295E)) return trap_member(z);
    return return_at(0x2960);
}
template<class Z> Exit trap_random_member(Z& z) {                    // 00:2964
    z.and_immediate(0x2964);
    return call_at(0x2966);
}
// 00:2969: after the alive test. A dead member is not hurt (a taken `ret z`).
// Otherwise the member's current hit points (the byte after the status byte) are
// multiplied by 256 and divided by 3 (the division routine, E = 3), so the high byte is a
// third rounded down.
template<class Z> Exit trap_hurt(Z& z) {
    auto& r=z.r;
    if (z.zero()) return mk::returns(0x2969,mk::taken_conditional_return_cycles);
    z.return_not_taken(0x2969);
    z.increment_pair(0x296A,r.h,r.l);
    z.push(0x296B,r.h,r.l);
    z.load_from_hl(0x296C,r.h);
    z.load_immediate(0x296D,r.l);
    z.load_immediate(0x296F,r.e);
    return call_at(0x2971);
}
// 00:2974: that third is taken off the member's hit points, so the trap never
// kills (a third of the points is always less than all of them).
template<class Z> Exit trap_apply(Z& z) {
    auto& r=z.r;
    z.pop(0x2974,r.d,r.e);
    z.exchange_de_hl(0x2975);
    z.load_from_hl(0x2976,r.a);
    z.subtract_register(0x2977,r.d);
    z.store_register_to_hl(0x2978,r.a);
    return return_at(0x2979);
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address. The exits, the stack the entry may hold or has
// inherited, and the permissions are the plan's (menu::field_segments); each is
// checked against the original on every call. A note names the call or jump
// the entry follows.
void register_all(RoutineRegistry& registry) {
    using menu::at0;
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return mk::returned_at(at,stack); };
    auto C=[](std::uint16_t at, std::int8_t stack=0) { return mk::conditional_at(at,stack); };
    auto add=[&](std::uint16_t entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,at0(entry),mk::segment(unit,std::move(exits),permissions),logic);
    };

    // ---- the field menu ----
    const auto menu_unit=menu::field_menu_unit;
    add(0x1BE1,menu_unit,{F(0x1BE8)},{},[](auto& z) -> Exit { return field_menu_open(z); });
    add(0x1BEE,menu_unit,{F(0x1BFF),F(0x1C23,-2)},{.scratch_stack=2},[](auto& z) -> Exit { return field_menu_loop(z); });   // after the windows are drawn (00:2ED9) or a command returns (00:36EF)
    add(0x1C02,menu_unit,{F(0x1C0A),F(0x1C23,-2)},{.scratch_stack=2},[](auto& z) -> Exit { return field_menu_chosen(z); });  // after the cursor loop (00:2D51)
    add(0x1C29,menu_unit,{R(0x1C2C,2),R(0x1C2F,2),F(0x1C35,2),F(0x1C3C,2),F(0x1C4B,2),C(0x1C5B,2)},{.inherited_stack=2},
        [](auto& z) -> Exit { return field_menu_result(z); });                                                              // after both windows are closed (00:36FB)
    add(0x1C51,menu_unit,{F(0x1BE8)},{},[](auto& z) -> Exit { return field_menu_again(z); });                               // after the second palette effect (01:7CBB)
    add(0x1C5E,menu_unit,{F(0x1C79)},{},[](auto& z) -> Exit { return field_menu_transfer(z); });                            // the jp c at 00:1C5B was not taken

    // ---- STATS ----
    const auto stats=menu::stats_unit;
    add(0x1CA4,stats,{F(0x1CA8),F(0x1CDC)},{},[](auto& z) -> Exit { return stats_chosen(z); });                             // after the member choice (00:3665)
    add(0x1CAB,stats,{F(0x1CAE,-2),F(0x1CDC)},{.scratch_stack=2},[](auto& z) -> Exit { return stats_alive(z); });            // after the alive check (00:188E)
    add(0x1CB7,stats,{F(0x1CCD,2),F(0x1CD6,2)},{.inherited_stack=2},[](auto& z) -> Exit { return stats_after_wait(z); });   // after the wait (00:2D25)

    // ---- SAVE ----
    const auto save=menu::save_unit;
    add(0x1CDF,save,{F(0x1CE2)},{},[](auto& z) -> Exit { return save_first_text(z); });
    add(0x1CE8,save,{F(0x1CEB)},{},[](auto& z) -> Exit { return save_second_text(z); });                                    // after the slot window (00:39A5)
    add(0x1CF1,save,{F(0x1CFC),F(0x1D3B)},{},[](auto& z) -> Exit { return save_answer(z); });                               // after the yes/no (00:2D19)
    add(0x1CFF,save,{F(0x1D0B,-2)},{.scratch_stack=2},[](auto& z) -> Exit { return save_slot_address(z); });                // after the third text
    add(0x1D33,save,{F(0x1D38),R(0x1D4C,4)},{.inherited_stack=4},[](auto& z) -> Exit { return save_done(z); });             // after the copy (00:1D0E-1D32, save_menu.cpp)

    // When the publication segment executes the branch, dispatch starts at its destination.
    add(0x1D35,save,{F(0x1D38)},{},[](auto& z) -> Exit { return say(z,0x1D35); });
    add(0x1D41,save,{R(0x1D4C,4)},{.inherited_stack=4},[](auto& z) -> Exit { return leave_saved_game(z); });

    // ---- MAGIC ----
    const auto magic=menu::magic_unit;
    add(0x1D50,magic,{F(0x1D55),F(0x1DBA)},{},[](auto& z) -> Exit { return magic_chosen(z); });                             // after the member choice (00:3665)
    add(0x1D58,magic,{F(0x1D81,-4),F(0x1DBA),F(0x1DC8)},{.scratch_stack=4},[](auto& z) -> Exit { return magic_member(z); });  // after the alive check (00:188E)
    add(0x1D84,magic,{F(0x1D90,2)},{.inherited_stack=2},[](auto& z) -> Exit { return magic_list_open(z); });                // after the list window (00:3478)
    add(0x1D93,magic,{F(0x1DAA,2),F(0x1DB7,2)},{.inherited_stack=2},[](auto& z) -> Exit { return magic_spell_chosen(z); });  // after the cursor loop (00:2D51)
    add(0x1DAD,magic,{F(0x1DB4),F(0x1DD4)},{},[](auto& z) -> Exit { return magic_cost_checked(z); });                       // after the cost check (00:1B87)
    add(0x1DDA,magic,{F(0x1DB7)},{},[](auto& z) -> Exit { return magic_after_refusal(z); });                                // after the "not enough" text is closed (00:3464)

    // ---- ITEM and USE ----
    const auto item=menu::item_unit;
    add(0x2168,item,{F(0x216F),F(0x2175)},{},[](auto& z) -> Exit { return item_start(z); });
    add(0x2178,item,{F(0x219D,-2),F(0x21A4,-2),F(0x21C8),F(0x21F5)},{.scratch_stack=2},[](auto& z) -> Exit { return item_chosen(z); });   // after the item list (00:34D5)
    add(0x21A0,item,{F(0x21B8,2)},{.inherited_stack=2},[](auto& z) -> Exit { return item_ground_vehicle_done(z); });        // after 01:7656
    add(0x21A7,item,{F(0x21B8,2)},{.inherited_stack=2},[](auto& z) -> Exit { return item_hovercraft_done(z); });            // after 01:7732
    add(0x21CB,item,{F(0x21C8),F(0x21D6),F(0x21F5)},{},[](auto& z) -> Exit { return item_member_found(z); });               // after the alive test
    add(0x21D9,item,{F(0x21E4)},{},[](auto& z) -> Exit { return item_action_window(z); });                                  // after the action window (00:3759)
    add(0x21E7,item,{F(0x21EF),F(0x21F2)},{},[](auto& z) -> Exit { return item_action_chosen(z); });                        // after the cursor loop (00:2D51)
    add(0x2201,menu::item_use_unit,{F(0x2207)},{},[](auto& z) -> Exit { return item_use(z); });

    // ---- EQUIP ----
    const auto equip=menu::equip_unit;
    add(0x26C8,equip,{F(0x26E5),F(0x26EE,-4)},{.slot2_mapper=true,.scratch_stack=4},[](auto& z) -> Exit { return equip_start(z); });
    add(0x26F1,equip,{F(0x26F8,4),F(0x2741,4)},{.inherited_stack=4},[](auto& z) -> Exit { return equip_member_chosen(z); });  // after the member choice (00:3665)
    add(0x26FB,equip,{F(0x272A,-2),F(0x2741),F(0x274A)},{.scratch_stack=2},[](auto& z) -> Exit { return equip_member_alive(z); });   // after the alive check (00:188E)
    add(0x272D,equip,{F(0x2730)},{},[](auto& z) -> Exit { return equip_show(z); });                                         // after the "equipped" text
    add(0x273C,equip,{F(0x273E,2)},{.inherited_stack=2},[](auto& z) -> Exit { return equip_old_item(z); });                 // after the dialogue is closed (00:3464)
    add(0x2750,equip,{F(0x2741)},{},[](auto& z) -> Exit { return equip_after_refusal(z); });                                // after the dialogue is closed (00:3464)

    // ---- DROP ----
    add(0x2752,menu::drop_unit,{F(0x276A),F(0x2773)},{.slot2_mapper=true},[](auto& z) -> Exit { return drop_item(z); });

    // ---- the inventory helpers ----
    add(0x277C,menu::remove_selected_item_unit,{R(0x279E)},{.scratch_stack=2},[](auto& z) -> Exit { return remove_selected_item(z); });
    add(0x277F,menu::remove_item_unit,{R(0x279E)},{.scratch_stack=2},[](auto& z) -> Exit { return remove_item(z); });
    const auto add_unit=menu::add_item_unit;
    add(0x279F,add_unit,{R(0x27BB),F(0x27BF)},{},[](auto& z) -> Exit { return add_item(z); });
    add(0x27C5,add_unit,{F(0x27CA),F(0x27DF,-2)},{.scratch_stack=2},[](auto& z) -> Exit { return add_item_first_answer(z); });      // after the first yes/no (00:2D19)
    add(0x27D0,add_unit,{F(0x27D5),F(0x27DF,-2)},{.scratch_stack=2},[](auto& z) -> Exit { return add_item_second_answer(z); });     // after the second yes/no (00:2D19)
    add(0x27E8,add_unit,{F(0x27BF,2),F(0x2804),F(0x2811)},{.slot2_mapper=true,.inherited_stack=2},
        [](auto& z) -> Exit { return add_item_replace_chosen(z); });                                                        // after the item list is closed (00:3656)
    add(0x2807,add_unit,{F(0x27DF)},{.inherited_stack=2},[](auto& z) -> Exit { return add_item_not_droppable(z); });        // after the "cannot give up" text
    add(0x2814,add_unit,{F(0x2824,2)},{.inherited_stack=2},[](auto& z) -> Exit { return add_item_swapped(z); });            // after the swap text
    add(0x282E,menu::find_item_unit,{R(0x2834),R(0x2838)},{},[](auto& z) -> Exit { return find_item(z); });

    // ---- SEARCH ----
    const auto search=menu::search_unit;
    add(0x2839,search,{F(0x2840),F(0x28B4),C(0x28CA)},{},[](auto& z) -> Exit { return search_start(z); });
    add(0x2843,search,{F(0x2846)},{},[](auto& z) -> Exit { return search_chest_text(z); });                                 // after the party window is closed (00:30B7)
    add(0x28B7,search,{F(0x28BC),C(0x28CA)},{},[](auto& z) -> Exit { return search_found_checked(z); });                    // after the carried check (00:282E)
    add(0x28CD,search,{C(0x28CF)},{},[](auto& z) -> Exit { return search_second_scene(z); });                               // the jp z at 00:28CA was not taken
    add(0x28D2,search,{F(0x28D5)},{},[](auto& z) -> Exit { return search_nothing_text(z); });                               // the jp z at 00:28CF was not taken

    // ---- chests ----
    const auto chest=menu::chest_unit;
    add(0x28DB,chest,{F(0x28DE)},{},[](auto& z) -> Exit { return chest_ask(z); });
    add(0x28E4,chest,{F(0x28E5,-2)},{.scratch_stack=2},[](auto& z) -> Exit { return chest_hold_answer(z); });               // after the yes/no window (00:37A3)
    add(0x28EB,chest,{R(0x28ED,2),F(0x28FE)},{.inherited_stack=2},[](auto& z) -> Exit { return chest_open(z); });           // after the dialogue is closed (00:3464)
    add(0x2901,chest,{F(0x2907,2)},{.inherited_stack=2},[](auto& z) -> Exit { return chest_trap_test(z); });                // after the opening is played (00:18B9)
    add(0x290A,chest,{F(0x291A),F(0x292B)},{},[](auto& z) -> Exit { return chest_reward(z); });                             // after the trap
    add(0x291D,chest,{F(0x2922)},{},[](auto& z) -> Exit { return chest_end_empty(z); });                                    // after the "empty" text
    add(0x292E,chest,{F(0x2933)},{},[](auto& z) -> Exit { return chest_money_text(z); });                                   // after the party's money is raised (00:297A)
    add(0x2936,chest,{F(0x2942),F(0x294D)},{},[](auto& z) -> Exit { return chest_item(z); });                               // after the money text
    add(0x2948,chest,{F(0x294D)},{},[](auto& z) -> Exit { return chest_end_reward(z); });                                   // after the item is added (00:279F)

    // ---- the trap ----
    const auto trap=menu::chest_trap_unit;
    add(0x2950,trap,{F(0x295B),F(0x2961)},{},[](auto& z) -> Exit { return trap_start(z); });
    add(0x295E,trap,{F(0x295B),R(0x2960)},{},[](auto& z) -> Exit { return trap_next_member(z); });                          // after one member (00:2966)
    add(0x2964,trap,{F(0x2966)},{},[](auto& z) -> Exit { return trap_random_member(z); });                                  // after the random draw (00:05B1)
    const auto member=menu::chest_trap_member_unit;
    add(0x2969,member,{R(0x2969),F(0x2971,-2)},{.scratch_stack=2},[](auto& z) -> Exit { return trap_hurt(z); });             // after the alive test
    add(0x2974,member,{R(0x2979,2)},{.inherited_stack=2},[](auto& z) -> Exit { return trap_apply(z); });                    // after the division
}
}

void register_menu_field(RoutineRegistry& registry) {
    register_all(registry);
}
}
