// SPDX-License-Identifier: GPL-3.0-or-later
// Scenes (milestone 13), part quests: the interaction scripts of bank 1 from
// 01:5101 to 01:5742. They are what a townsperson, a guard or a shopkeeper
// does once the interaction dispatcher has chosen them: show a dialogue, ask
// yes or no, look for an item, start a fixed encounter, change a story byte.
// See docs/how-it-works/scenes.md ("Dialogue choices and recruiting Noah",
// "Advancing conversations and exchanging items", "Solo encounters and
// destination requests").
//
// How the code is cut. A script is a chain of calls into shared routines: the
// numbered-dialogue entry (01:575A) and the direct-dialogue entry (00:31CF),
// the yes/no window (00:2D19), the inventory search (00:282E), removal
// (00:277F) and addition (00:279F), the encounter setup, the battle (00:100F)
// and the dialogue-window close (00:3464). Those routines belong to other
// parts, so every call into them is where a segment ends: the segment renders
// the loads, tests and stores between two calls and hands the call (or the
// tail jump that ends a script) to the Z80 core. The instruction after a call
// is the next entry. A script's jumps become if/else and early returns.
//
// No dialogue, item, enemy group, price or flag value is written here: the
// kit reads every operand and cycle cost from the player's ROM at the
// original address passed to it, so the same code serves both releases.
//
// Bytes named below: C2C5 the number a dialogue prints, C2E6 the enemy group
// of the next encounter, C2E9 the travel request, C2F0 the members a damage
// tile knocked out, C309 the location, C500 onwards the story flags (C503,
// C504, C506, C508, C511, C516, C517), C800 the first object slot.
#include "game/scenes.h"
#include "game/menu_kit.h"
#include <vector>

namespace psmodern::game {
namespace {
namespace mk=menu_kit;
using mk::Exit;

// The ways a segment ends. Each names the original's final instruction and
// charges its cycles; the core runs that instruction.
Exit call_at(std::uint16_t at) { return mk::continues(at,mk::call_cycles); }
Exit call_if_at(std::uint16_t at, bool taken) { return mk::continues(at,taken ? mk::call_cycles : mk::untaken_call_cycles); }
Exit jump_at(std::uint16_t at) { return mk::continues(at,mk::jump_cycles); }
Exit return_at(std::uint16_t at) { return mk::returns(at,mk::return_cycles); }
Exit taken_return_at(std::uint16_t at) { return mk::returns(at,mk::taken_conditional_return_cycles); }

// What follows a dialogue selection: a tail jump into the renderer (the script
// ends there) or a call (the script goes on after the text).
enum class Next { jump, call };
Exit next_at(Next next, std::uint16_t at) { return next==Next::call ? call_at(at) : jump_at(at); }
std::uint16_t after(std::uint16_t at, unsigned length) { return static_cast<std::uint16_t>(at+length); }

// ---- Shared shapes ----------------------------------------------------------------------------
// `ld hl,nn` at `at` (the dialogue number or address, read from the ROM), then
// the jump or call into the dialogue renderer right after it.
template<class Z> Exit dialogue(Z& z, std::uint16_t at, Next next) {
    z.load_hl(at);
    return next_at(next,after(at,3));
}
template<class Z> Exit dialogue_then_jump(Z& z, std::uint16_t at) { return dialogue(z,at,Next::jump); }
template<class Z> Exit dialogue_then_call(Z& z, std::uint16_t at) { return dialogue(z,at,Next::call); }
// `ld a,n` at `at`, then a call: the picture for the scene-object loader
// (01:617D) or the item for the inventory search.
template<class Z> Exit number_then_call(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    return call_at(after(at,2));
}
// `ld a,n; ld (nn),a` at `at`, then the call or jump after it: an enemy group
// into C2E6 before the encounter setup, an item into the current-item byte
// before the inventory helpers, or a story flag before a dialogue.
template<class Z> Exit store_number(Z& z, std::uint16_t at, Next next) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(after(at,2));
    return next_at(next,after(at,5));
}
// The reply to a yes/no answer (Z set for yes): `ld hl,nn` with the yes text,
// `jr z` over the second `ld hl,nn` with the no text, then the renderer.
template<class Z> Exit reply_by_answer(Z& z, std::uint16_t at, Next next) {
    z.load_hl(at);
    if (!z.relative_if(after(at,3),z.zero())) z.load_hl(after(at,5));
    return next_at(next,after(at,8));
}
// A `jr cc` that picks one of two dialogue loads, each followed by its own
// jump or call into the renderer.
struct DialogueBranch {
    std::uint16_t at;           // the jr cc
    bool on_zero;               // jr z (true) or jr nz (false)
    std::uint16_t taken;        // the dialogue load the jump goes to
    Next taken_next;
    std::uint16_t otherwise;    // the dialogue load after the jr
    Next otherwise_next;
};
template<class Z> Exit dialogue_branch(Z& z, const DialogueBranch& branch) {
    const bool taken=z.relative_if(branch.at,branch.on_zero ? z.zero() : !z.zero());
    if (taken) return dialogue(z,branch.taken,branch.taken_next);
    return dialogue(z,branch.otherwise,branch.otherwise_next);
}
// `ld a,n; ld (nn),a` at `at` (a story flag), then the dialogue load right
// after it and a tail jump into the renderer.
template<class Z> Exit flag_then_dialogue(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(after(at,2));
    return dialogue_then_jump(z,after(at,5));
}
// After the inventory search for an item the script hands out: `ret z` when
// the party already carries it, else a jump to the inventory addition.
template<class Z> Exit add_unless_carried(Z& z, std::uint16_t at) {
    if (z.zero()) return taken_return_at(at);
    z.return_not_taken(at);
    return jump_at(after(at,1));
}

// ---- The cake seller: 01:510D-5156 ------------------------------------------------------------
// 01:510D: the price is kept in C2C5 so the offer prints it, then the offer
// itself is shown; the yes/no window follows.
template<class Z> Exit cake_offer(Z& z) {
    z.load_hl(0x510D);
    z.store_hl_direct(0x5110);                                       // C2C5
    return dialogue_then_call(z,0x5113);
}
// 01:5124: money minus price (`or a` clears the carry for `sbc`). The result
// stays in HL: it is stored only after the capacity check, so both refusals
// leave the money untouched. Carry (not enough money) goes to the refusal text.
template<class Z> Exit cake_money(Z& z) {
    z.load_de(0x5124);                                               // the price
    z.load_hl_direct(0x5127);                                        // the party's money
    z.or_a(0x512A);
    z.subtract_hl_with_carry(0x512B,z.de());
    return mk::relative_if_exit(0x512D,!z.carry());
}
// 01:511C: after the yes/no. No ends with its own text; yes checks the money.
template<class Z> Exit cake_answer(Z& z) {
    if (!z.relative_if(0x511C,z.zero())) return dialogue_then_jump(z,0x511E);
    return cake_money(z);
}
// 01:5135: a full inventory (the item count not below the limit) is refused.
template<class Z> Exit cake_capacity(Z& z) {
    z.load_a_direct(0x5135);                                         // the item count
    z.compare_immediate(0x5138);
    return mk::relative_if_exit(0x513A,z.carry());
}
// 01:5142: the new balance is stored and the thanks shown.
template<class Z> Exit cake_paid(Z& z) {
    z.store_hl_direct(0x5142);                                       // the party's money
    return dialogue_then_call(z,0x5145);
}

// ---- Recruiting Noah: 01:515F-51B0 ------------------------------------------------------------
// 01:515F: after the encounter setup of 01:5157. The reply depends on whether
// the party-size byte says the fourth member has already joined.
template<class Z> Exit party_reply(Z& z) {
    z.load_a_direct(0x515F);                                         // the party size
    z.compare_immediate(0x5162);
    z.load_hl(0x5164);
    if (!z.relative_if(0x5167,!z.zero())) z.load_hl(0x5169);
    return jump_at(0x516C);
}
// 01:516F: a complete party leaves the interaction (the `jp nc`, decided by the core).
template<class Z> Exit recruit_party_full(Z& z) {
    z.load_a_direct(0x516F);
    z.compare_immediate(0x5172);
    return mk::jump_if_exit(0x5174);
}
// 01:518A: after the search for the letter. Without it (NZ) the script returns;
// with it the letter is removed.
template<class Z> Exit recruit_letter(Z& z) {
    if (!z.zero()) return taken_return_at(0x518A);
    z.return_not_taken(0x518A);
    return call_at(0x518B);                                          // 00:277F
}
// 01:518E: the interaction's return address is dropped, then the dialogue
// window is closed.
template<class Z> Exit recruit_unwind(Z& z) {
    z.pop(0x518E,z.r.h,z.r.l);
    return call_at(0x518F);                                          // 00:3464
}
// 01:5195: the recruitment flag (C506) is set and Noah's record gets its
// starting weapon and armour through IY; then his record is initialized (00:16F1).
template<class Z> Exit recruit_equip(Z& z) {
    z.load_immediate(0x5195,z.r.a);
    z.store_a_direct(0x5197);                                        // C506
    z.load_index(0x519A,mk::Index::iy);                              // Noah's record
    z.store_immediate_indexed(0x519E,mk::Index::iy);                 // weapon
    z.store_immediate_indexed(0x51A2,mk::Index::iy);                 // armour
    return call_at(0x51A6);
}

// ---- Conversation progress: 01:51B1-5217 -----------------------------------------------------
// 01:51B1: a story byte (C504) at or past its second stage leaves the interaction.
template<class Z> Exit progress_gate(Z& z) {
    z.load_hl(0x51B1);                                               // C504
    z.load_from_hl(0x51B4,z.r.a);
    z.compare_immediate(0x51B5);
    return mk::jump_if_exit(0x51B7);
}
// 01:51C2: the reply. At the first stage the byte advances to the second and
// the alternative text is chosen; the dialogue number travels through DE and
// is exchanged into HL for the renderer.
template<class Z> Exit progress_reply(Z& z) {
    z.load_hl(0x51C2);                                               // C504
    z.load_from_hl(0x51C5,z.r.a);
    z.compare_immediate(0x51C6);
    z.load_de(0x51C8);
    if (!z.relative_if(0x51CB,!z.zero())) {
        z.store_to_hl(0x51CD);                                       // the next stage
        z.load_de(0x51CF);
    }
    z.exchange_de_hl(0x51D2);
    return jump_at(0x51D3);
}
// 01:51DC: once C504 has started, this introduction leaves the interaction.
template<class Z> Exit introduction_gate(Z& z) {
    z.load_a_direct(0x51DC);                                         // C504
    z.or_a(0x51DF);
    return mk::jump_if_exit(0x51E0);
}
// 01:51EB: C503 counts the visits. Zero goes on to the first visit (01:51F2).
template<class Z> Exit visit_count(Z& z) {
    z.load_hl(0x51EB);                                               // C503
    z.load_from_hl(0x51EE,z.r.a);
    z.or_a(0x51EF);
    return mk::relative_if_exit(0x51F0,!z.zero());
}
// 01:51F2 and 01:51FD: the first and the second visit count themselves and
// show their own text.
template<class Z> Exit first_visit(Z& z) {
    z.increment_at_hl(0x51F2);
    return dialogue_then_jump(z,0x51F3);
}
template<class Z> Exit second_visit(Z& z) {
    z.increment_at_hl(0x51FD);
    return dialogue_then_jump(z,0x51FE);
}
// 01:51F9: anything but the second visit goes on to the question.
template<class Z> Exit visit_second(Z& z) {
    z.compare_immediate(0x51F9);
    return mk::relative_if_exit(0x51FB,!z.zero());
}
// 01:520D: after the question's yes/no. No keeps the refusal text; yes starts
// the story byte C504 and takes the acceptance text.
template<class Z> Exit introduction_answer(Z& z) {
    z.load_hl(0x520D);
    if (!z.relative_if(0x5210,!z.zero())) {
        z.load_hl(0x5212);                                           // C504
        z.store_to_hl(0x5215);
        z.load_hl(0x5217);
    }
    return jump_at(0x521A);
}

// ---- The road-pass guards: 01:521D-5248 and 01:534B-5394 ------------------------------------
// 01:522E: after the guard's yes/no (Z for yes). No leads to the refusal text
// and the fight (01:523D); yes searches for the road pass.
template<class Z> Exit pass_answer(Z& z) {
    if (z.relative_if(0x522E,!z.zero())) return dialogue_then_call(z,0x523D);
    return number_then_call(z,0x5230);                               // the pass, then 00:282E
}
// 01:5235: without the pass, the same refusal; with it, the guard lets the party by.
template<class Z> Exit pass_carried(Z& z) {
    if (z.relative_if(0x5235,!z.zero())) return dialogue_then_call(z,0x523D);
    return dialogue_then_jump(z,0x5237);
}
// 01:535C: the second guard. No: the refusal and the fight (01:5380); yes: the
// search for the pass.
template<class Z> Exit dungeon_guard_answer(Z& z) {
    if (z.relative_if(0x535C,!z.zero())) return dialogue_then_call(z,0x5380);
    return number_then_call(z,0x535E);
}
// 01:536E: with the pass the party is let through: the interaction's return
// address is dropped, the dungeon position and direction are set and the game
// mode is changed so the dungeon is entered; the script returns to the game.
template<class Z> Exit dungeon_guard_through(Z& z) {
    z.pop(0x536E,z.r.h,z.r.l);
    z.load_hl(0x536F);
    z.store_hl_direct(0x5372);                                       // the dungeon position
    z.load_immediate(0x5375,z.r.a);
    z.store_a_direct(0x5377);                                        // the direction
    z.load_hl(0x537A);
    z.store_to_hl(0x537D);                                           // the game mode
    return return_at(0x537F);
}
// 01:538C: after the battle, a party still standing on the field (the first
// object slot, C800, in use) opens the field menu (00:1BE1).
template<class Z> Exit after_battle_menu(Z& z) {
    z.load_a_direct(0x538C);                                         // C800
    z.or_a(0x538F);
    return call_if_at(0x5390,!z.zero());
}
// 01:5393: the interaction's return address is dropped and the script returns.
template<class Z> Exit after_battle_leave(Z& z) {
    z.pop(0x5393,z.r.h,z.r.l);
    return return_at(0x5394);
}

// ---- The cola trade: 01:5249-526F -------------------------------------------------------------
// 01:5252: after the yes/no. Yes searches for the cola; no ends with its text.
template<class Z> Exit cola_answer(Z& z) {
    if (z.relative_if(0x5252,z.zero())) return number_then_call(z,0x525A);
    return dialogue_then_jump(z,0x5254);
}
// 01:525F: without a cola, its text; with one, the cola is removed (00:277F).
template<class Z> Exit cola_carried(Z& z) {
    if (z.relative_if(0x525F,!z.zero())) return dialogue_then_jump(z,0x526A);
    return call_at(0x5261);
}

// ---- The cake guard: 01:53BF-5400 -------------------------------------------------------------
// 01:53D0: after the yes/no. Yes searches for the cake; no shows its text and
// goes to the shared leave (01:53F7).
template<class Z> Exit cake_guard_answer(Z& z) {
    if (z.relative_if(0x53D0,z.zero())) return number_then_call(z,0x53DA);
    return dialogue_then_call(z,0x53D2);
}
// 01:53DF: without a cake, its text before the leave. With one, BC is kept
// across the removal (the script needs it afterwards).
template<class Z> Exit cake_guard_carried(Z& z) {
    if (z.relative_if(0x53DF,!z.zero())) return dialogue_then_call(z,0x53F1);
    z.push(0x53E1,z.r.b,z.r.c);
    return call_at(0x53E2);                                          // 00:277F
}
// 01:53E5: BC back; the guard's flag (C511) is set and the thanks shown.
template<class Z> Exit cake_guard_paid(Z& z) {
    z.pop(0x53E5,z.r.b,z.r.c);
    z.load_immediate(0x53E6,z.r.a);
    z.store_a_direct(0x53E8);                                        // C511
    return dialogue_then_jump(z,0x53EB);
}
// 01:53F7: leaving: the return address is dropped and the window closed; the
// party then steps back in the dungeon (01:688C).
template<class Z> Exit cake_guard_leave(Z& z) {
    z.pop(0x53F7,z.r.h,z.r.l);
    return call_at(0x53F8);                                          // 00:3464
}

// ---- The item exchange: 01:5401-542F ----------------------------------------------------------
// 01:540A: after the yes/no. Yes searches for the wanted item; no ends with its text.
template<class Z> Exit exchange_answer(Z& z) {
    if (z.relative_if(0x540A,z.zero())) return number_then_call(z,0x5412);
    return dialogue_then_jump(z,0x540C);
}
// 01:5417: without the item, its text; with it, the item is removed (00:277F)
// and the new one is given (01:541C).
template<class Z> Exit exchange_carried(Z& z) {
    if (z.relative_if(0x5417,!z.zero())) return dialogue_then_jump(z,0x542A);
    return call_at(0x5419);
}

// ---- The solo duel: 01:5430-54CD --------------------------------------------------------------
// 01:5438: after the encounter setup. If Noah already wears the prize armour,
// its text; otherwise the inventory is searched for it (A still holds it).
template<class Z> Exit duel_armour_worn(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x5438);                                         // Noah's armour
    z.copy(0x543B,r.b,r.a);
    z.load_immediate(0x543C,r.a);                                    // the prize
    z.compare_register(0x543E,r.b);
    if (z.relative_if(0x543F,z.zero())) return dialogue_then_jump(z,0x5446);
    return call_at(0x5441);                                          // 00:282E
}
// 01:544C: a party without its fourth member gets the yes/no conversation.
template<class Z> Exit duel_party_size(Z& z) {
    z.load_a_direct(0x544C);                                         // the party size
    z.compare_immediate(0x544F);
    return mk::relative_if_exit(0x5451,!z.carry());
}
// 01:5444: a carried prize takes the same text as a worn one.
template<class Z> Exit duel_armour_carried(Z& z) {
    if (z.relative_if(0x5444,!z.zero())) return duel_party_size(z);
    return dialogue_then_jump(z,0x5446);
}
// 01:546D: Noah not alive (his status byte zero) ends with its own text.
template<class Z> Exit duel_noah_status(Z& z) {
    z.load_a_direct(0x546D);                                         // Noah's status
    z.or_a(0x5470);
    return mk::relative_if_exit(0x5471,!z.zero());
}
// 01:5482: Noah fights alone. His HP and the status bytes of the other three
// members are pushed in that order, the three statuses are cleared (they are
// out of the fight) and the battle starts.
template<class Z> Exit duel_start(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x5482);                                         // Noah's HP
    z.push(0x5485,r.a,r.f);
    z.load_a_direct(0x5486);                                         // the first member's status
    z.push(0x5489,r.a,r.f);
    z.load_a_direct(0x548A);                                         // the second member's
    z.push(0x548D,r.a,r.f);
    z.load_a_direct(0x548E);                                         // the third member's
    z.push(0x5491,r.a,r.f);
    z.xor_a(0x5492);
    z.store_a_direct(0x5493);
    z.store_a_direct(0x5496);
    z.store_a_direct(0x5499);
    return call_at(0x549C);                                          // 00:100F
}
// 01:549F: after the battle the three statuses come back in reverse order and
// Noah's old HP goes to B. HP left (nonzero) means he won (01:54C2).
template<class Z> Exit duel_end(Z& z) {
    auto& r=z.r;
    z.pop(0x549F,r.a,r.f);
    z.store_a_direct(0x54A0);                                        // the third member's status
    z.pop(0x54A3,r.a,r.f);
    z.store_a_direct(0x54A4);                                        // the second member's
    z.pop(0x54A7,r.a,r.f);
    z.store_a_direct(0x54A8);                                        // the first member's
    z.pop(0x54AB,r.a,r.f);
    z.copy(0x54AC,r.b,r.a);                                          // Noah's HP before the fight
    z.load_a_direct(0x54AD);                                         // and now
    z.or_a(0x54B0);
    return mk::relative_if_exit(0x54B1,!z.zero());
}
// 01:54B3: a lost duel does not kill him: his old HP and his status are put
// back before the text.
template<class Z> Exit duel_lost(Z& z) {
    auto& r=z.r;
    z.copy(0x54B3,r.a,r.b);
    z.store_a_direct(0x54B4);                                        // Noah's HP
    z.load_immediate(0x54B7,r.a);
    z.store_a_direct(0x54B9);                                        // his status: alive
    return dialogue_then_jump(z,0x54BC);
}

// ---- Fixed encounters and the post-battle check: 01:54D0-553C ---------------------------------
// 01:54E4: after the encounter of 01:54D0, its flag (C517) is set.
template<class Z> Exit encounter_won(Z& z) { return flag_then_dialogue(z,0x54E4); }
// 01:54F2: the second half of the post-battle check, after the battle. Unless
// the battle ended in defeat (game mode 2) the script goes on; a defeat drops
// the two return addresses of the script so the whole interaction is left.
template<class Z> Exit battle_outcome(Z& z) {
    z.load_a_direct(0x54F2);                                         // the game mode
    z.compare_immediate(0x54F5);
    if (!z.zero()) return taken_return_at(0x54F7);
    z.return_not_taken(0x54F7);
    z.pop(0x54F8,z.r.h,z.r.l);
    z.pop(0x54F9,z.r.h,z.r.l);
    return return_at(0x54FA);
}
// 01:54FB: an encounter already won (C516 set) leaves the interaction.
template<class Z> Exit won_gate(Z& z) {
    z.load_a_direct(0x54FB);                                         // C516
    z.or_a(0x54FE);
    return mk::jump_if_exit(0x54FF);
}
// 01:5524: after that encounter, C516 is set.
template<class Z> Exit gate_won(Z& z) { return flag_then_dialogue(z,0x5524); }
// 01:553B: the interaction's return address is dropped and the script returns.
template<class Z> Exit leave_after_field_menu(Z& z) {
    z.pop(0x553B,z.r.h,z.r.l);
    return return_at(0x553C);
}

// ---- Hapsby and the vehicles: 01:5546-5594 ----------------------------------------------------
// 01:554E: after the search for Hapsby. Already carried: the shared text
// (01:553D); otherwise the window is closed and Hapsby is woken.
template<class Z> Exit robot_carried(Z& z) {
    if (z.relative_if(0x554E,z.zero())) return dialogue_then_call(z,0x553D);
    return call_at(0x5550);                                          // 00:3464
}
// 01:5553: the second object slot's state (C801) advances; the objects are redrawn.
template<class Z> Exit robot_wakes(Z& z) {
    z.load_hl(0x5553);                                               // C801
    z.increment_at_hl(0x5556);
    return call_at(0x5557);
}
// 01:5566: without the story flag C508, the shared text; with it, the search for Hapsby.
template<class Z> Exit vehicle_flag(Z& z) {
    z.load_a_direct(0x5566);                                         // C508
    z.or_a(0x5569);
    if (z.relative_if(0x556A,z.zero())) return dialogue_then_call(z,0x553D);
    return store_number(z,0x556C,Next::call);
}
// 01:5574: Hapsby must be carried; then the search for the vehicle.
template<class Z> Exit vehicle_robot(Z& z) {
    if (z.relative_if(0x5574,!z.zero())) return dialogue_then_call(z,0x553D);
    return store_number(z,0x5576,Next::call);
}

// ---- Other encounters and the transfer: 01:55B1-560F ------------------------------------------
// 01:55B1: after the encounter setup of 01:55A9, CBC3 is cleared before the text.
template<class Z> Exit encounter_clear(Z& z) {
    z.xor_a(0x55B1);
    z.store_a_direct(0x55B2);                                        // CBC3
    return dialogue_then_jump(z,0x55B5);
}
// 01:55C3: the encounter of 01:55BB pays nothing: the return address is
// dropped, the money reward set to zero and the battle entered by a jump.
template<class Z> Exit encounter_without_reward(Z& z) {
    z.pop(0x55C3,z.r.h,z.r.l);
    z.load_hl(0x55C4);
    z.store_hl_direct(0x55C7);                                       // the money reward
    return jump_at(0x55CA);                                          // 00:100F
}
// 01:55EF: C504 below its seventh stage keeps the first text.
template<class Z> Exit transfer_progress(Z& z) {
    z.load_a_direct(0x55EF);                                         // C504
    z.compare_immediate(0x55F2);
    return mk::relative_if_exit(0x55F4,!z.carry());
}
// 01:5602: the transfer record depends on the camera's X position (C301):
// one record from the right half, the other from the left; then the map
// transfer is applied.
template<class Z> Exit transfer_by_side(Z& z) {
    z.load_a_direct(0x5602);                                         // C301
    z.compare_immediate(0x5605);
    z.load_hl(0x5607);
    if (!z.relative_if(0x560A,!z.carry())) z.load_hl(0x560C);
    return call_at(0x560F);
}

// ---- The party-status report: 01:56A2-56CC ----------------------------------------------------
// Each bit of C2F0 (from the first member) names a member a damage tile knocked
// out. One step: the member's number goes to the current character, the next
// bit is shifted into the carry, the mask and counter are kept on the stack,
// and the knocked-out text is shown only when the bit was set.
template<class Z> Exit status_member(Z& z) {
    auto& r=z.r;
    z.copy(0x56A8,r.a,r.e);
    z.store_a_direct(0x56A9);                                        // the current character
    z.rotate_right_through_carry(0x56AC,r.d);
    z.push(0x56AE,r.d,r.e);
    z.load_hl(0x56AF);
    return call_if_at(0x56B2,z.carry());
}
// 01:56A2: D holds the mask, E counts the members from zero.
template<class Z> Exit status_report(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x56A2);                                         // C2F0
    z.copy(0x56A5,r.d,r.a);
    z.load_immediate(0x56A6,r.e);
    return status_member(z);
}
// 01:56BE: the survivor search, from the last member down: is member B-1 alive (00:188E)?
template<class Z> Exit survivor_check(Z& z) {
    z.copy(0x56BE,z.r.a,z.r.b);
    z.decrement(0x56BF,z.r.a);
    return call_at(0x56C0);
}
// 01:56B5: the next member, until all four are done; then the survivor search
// starts with B at four.
template<class Z> Exit status_next(Z& z) {
    auto& r=z.r;
    z.pop(0x56B5,r.d,r.e);
    z.increment(0x56B6,r.e);
    z.copy(0x56B7,r.a,r.e);
    z.compare_immediate(0x56B8);
    if (z.relative_if(0x56BA,!z.zero())) return status_member(z);
    z.load_immediate(0x56BC,r.b);
    return survivor_check(z);
}
// 01:56C3: a survivor closes the window (a jump to 00:3464); when no member
// is left alive, the final jump goes to the defeat.
template<class Z> Exit survivor_found(Z& z) {
    if (z.relative_if(0x56C3,!z.zero())) return jump_at(0x56CA);
    if (z.decrement_jump(0x56C5)) return survivor_check(z);
    return jump_at(0x56C7);
}

// ---- Travel requests and the destination menu: 01:56D9-5751 -----------------------------------
// `ld a,n; ld (nn),a; ret` at `at`: the travel request into C2E9.
template<class Z> Exit request_travel(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(after(at,2));                                   // C2E9
    return return_at(after(at,5));
}
// 01:56E2 and 01:56F2: after a yes/no, no (NZ) returns; yes makes the request.
template<class Z> Exit request_travel_if_yes(Z& z, std::uint16_t at) {
    if (!z.zero()) return taken_return_at(at);
    z.return_not_taken(at);
    return request_travel(z,after(at,1));
}
// 01:56FF: BC is kept across the three-choice window (00:3A21).
template<class Z> Exit destination_menu(Z& z) {
    z.push(0x56FF,z.r.b,z.r.c);
    return call_at(0x5700);
}
// 01:5707: D is the chosen destination; E the current one, bits 3-4 of the
// location (C309). Choosing where the party already is goes to the "already
// here" texts.
template<class Z> Exit destination_compare(Z& z) {
    auto& r=z.r;
    z.copy(0x5707,r.d,r.a);
    z.load_a_direct(0x5708);                                         // C309
    z.rotate_right_circular_a(0x570B);
    z.rotate_right_circular_a(0x570C);
    z.rotate_right_circular_a(0x570D);
    z.and_immediate(0x570E);
    z.copy(0x5710,r.e,r.a);
    z.compare_register(0x5711,r.d);
    return mk::relative_if_exit(0x5712,!z.zero());
}
// 01:5703: button 1 (bit 4 of C) cancels the menu and returns; BC comes back first.
template<class Z> Exit destination_chosen(Z& z) {
    z.test_bit(0x5703,4,z.r.c);
    z.pop(0x5705,z.r.b,z.r.c);
    if (!z.zero()) return taken_return_at(0x5706);
    z.return_not_taken(0x5706);
    return destination_compare(z);
}
// One of three texts by A (0, 1 or anything else): `or a` at `at`, then two
// `jr z` / `dec a` steps over the three `ld hl,nn`.
template<class Z> void text_by_destination(Z& z, std::uint16_t at) {
    z.or_a(at);
    z.load_hl(after(at,1));
    if (!z.relative_if(after(at,4),z.zero())) {
        z.decrement(after(at,6),z.r.a);
        z.load_hl(after(at,7));
        if (!z.relative_if(after(at,10),z.zero())) z.load_hl(after(at,12));
    }
}
// 01:5714: the "already here" text of the current destination; the menu then opens again.
template<class Z> Exit destination_already_here(Z& z) {
    text_by_destination(z,0x5714);
    return call_at(0x5723);
}
// 01:5738: DE (both destinations) is kept across the confirmation text and its yes/no.
template<class Z> Exit destination_hold(Z& z) {
    z.push(0x5738,z.r.d,z.r.e);
    return call_at(0x5739);
}
// 01:5728: the confirmation question of the chosen destination.
template<class Z> Exit destination_question(Z& z) {
    z.copy(0x5728,z.r.a,z.r.d);
    text_by_destination(z,0x5729);
    return destination_hold(z);
}
// 01:5742: the request is read from an eight-byte table at three times the
// current destination plus the chosen one, stored in C2E9, and the script returns.
template<class Z> Exit destination_request(Z& z) {
    auto& r=z.r;
    z.copy(0x5742,r.a,r.e);
    z.add_register(0x5743,r.a);
    z.add_register(0x5744,r.e);                                      // current times three
    z.add_register(0x5745,r.d);                                      // plus chosen
    z.load_immediate(0x5746,r.d);
    z.copy(0x5748,r.e,r.a);
    z.load_hl(0x5749);
    z.add_to_hl(0x574C,z.de());
    z.load_from_hl(0x574D,r.a);
    z.store_a_direct(0x574E);                                        // C2E9
    return return_at(0x5751);
}
// 01:573F: DE back. No (NZ) opens the menu again from its first text (01:56F9).
template<class Z> Exit destination_answer(Z& z) {
    z.pop(0x573F,z.r.d,z.r.e);
    if (z.relative_if(0x5740,!z.zero())) return dialogue_then_call(z,0x56F9);
    return destination_request(z);
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, each checked against the original on every
// call: its unit (the entry itself unless named), the exits and the stack it
// may hold (scratch) or has inherited from an earlier segment of its unit.
void register_all(RoutineRegistry& registry) {
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return mk::returned_at(at,stack); };
    auto C=[](std::uint16_t at, std::int8_t stack=0) { return mk::conditional_at(at,stack); };
    auto add_in=[&](std::uint16_t entry, std::uint16_t unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,RoutineEntry{1,entry},mk::segment(RoutineEntry{1,unit},std::move(exits),permissions),logic);
    };
    auto add=[&](std::uint16_t entry, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        add_in(entry,entry,std::move(exits),permissions,logic);
    };
    // A dialogue load, a number load or a two-instruction store before one
    // call or jump: a single exit and room for the call's return address.
    auto add_load=[&](std::uint16_t entry, std::uint16_t end, auto logic) { add(entry,{F(end)},{.scratch_stack=2},logic); };

    // The dialogue loads that scripts jump or fall into. Each is an entry of
    // its own; the text is read from the cartridge.
    constexpr std::uint16_t jumping_dialogues[]{
        0x5101,0x5107,0x511E,0x512F,0x513C,0x5169,0x51D6,0x51F3,0x51FE,0x5217,0x5237,0x5254,0x5264,0x526A,
        0x527E,0x5284,0x528A,0x529E,0x52AC,0x52B2,0x52B8,0x52BE,0x52C4,0x52CA,0x52DB,0x5314,0x5345,0x5395,
        0x539B,0x53A1,0x53A7,0x53AD,0x53B3,0x53B9,0x53EB,0x540C,0x5424,0x542A,0x5446,0x5461,0x5473,0x54BC,
        0x54E9,0x5529,0x552F,0x559D,0x55A3,0x55B5,0x55DB,0x55E9,0x55F6,0x569C,0x56CD,0x56D3};
    constexpr std::uint16_t calling_dialogues[]{
        0x5113,0x5145,0x517F,0x5204,0x5225,0x523D,0x5249,0x5270,0x5290,0x52D0,0x52E1,0x52E7,0x52F2,0x52F8,
        0x5303,0x5309,0x531A,0x5325,0x5337,0x5353,0x5365,0x5380,0x53C7,0x53D2,0x53F1,0x5401,0x5453,0x5467,
        0x5479,0x54C2,0x54D8,0x550A,0x5518,0x553D,0x555D,0x5580,0x558C,0x55CD,0x55FC,0x568F,0x56D9,0x56E9,
        0x56F9,0x5720};
    for (const auto at:jumping_dialogues) add_load(at,after(at,3),[at](auto& z) -> Exit { return dialogue_then_jump(z,at); });
    for (const auto at:calling_dialogues) add_load(at,after(at,3),[at](auto& z) -> Exit { return dialogue_then_call(z,at); });
    // Each fixed encounter stores its enemy group in C2E6 before the shared setup.
    constexpr std::uint16_t encounters[]{0x5157,0x521D,0x52A4,0x534B,0x53BF,0x5430,0x54D0,0x5502,0x5595,0x55A9,0x55BB,0x55E1};
    for (const auto at:encounters) add_load(at,after(at,5),[at](auto& z) -> Exit { return store_number(z,at,Next::call); });
    // A picture for the scene-object loader (01:617D), or an item for the search.
    constexpr std::uint16_t numbers[]{0x5177,0x5185,0x51BA,0x51E3,0x5230,0x525A,0x535E,0x53DA,0x5412};
    for (const auto at:numbers) add_load(at,after(at,2),[at](auto& z) -> Exit { return number_then_call(z,at); });
    // The current item before a search (or an addition, 01:541C).
    for (const auto at:{std::uint16_t{0x514B},std::uint16_t{0x532B},std::uint16_t{0x541C},std::uint16_t{0x5546},
                        std::uint16_t{0x556C},std::uint16_t{0x5576}})
        add_load(at,after(at,5),[at](auto& z) -> Exit { return store_number(z,at,Next::call); });
    // An item or a flag before a tail jump: the duel's prize (to the addition)
    // and the fourth member's party size (to the shared conversation scene 01:4461).
    for (const auto at:{std::uint16_t{0x51A9},std::uint16_t{0x54C8}})
        add_load(at,after(at,5),[at](auto& z) -> Exit { return store_number(z,at,Next::jump); });
    // The yes/no replies. Every one ends its script with a tail jump except the
    // one at 01:5513, whose script goes on (the window is closed and the
    // post-battle check runs), so its renderer is called.
    for (const auto at:{std::uint16_t{0x5279},std::uint16_t{0x5299},std::uint16_t{0x5340},std::uint16_t{0x545C},std::uint16_t{0x55D6}})
        add_load(at,after(at,8),[at](auto& z) -> Exit { return reply_by_answer(z,at,Next::jump); });
    add_load(0x5513,0x551B,[](auto& z) -> Exit { return reply_by_answer(z,0x5513,Next::call); });
    // The jr cc that choose between two dialogue loads: the crystal riddles
    // (01:52D9-5325), the second guard (01:5363), the duel (01:5471) and the vehicle (01:557E).
    constexpr DialogueBranch branches[]{
        {0x52D9,true, 0x52E1,Next::call,0x52DB,Next::jump},
        {0x52F0,false,0x52DB,Next::jump,0x52F2,Next::call},
        {0x5301,false,0x52DB,Next::jump,0x5303,Next::call},
        {0x5312,false,0x531A,Next::call,0x5314,Next::jump},
        {0x5323,true, 0x52DB,Next::jump,0x5325,Next::call},
        {0x5363,false,0x5380,Next::call,0x5365,Next::call},
        {0x5471,false,0x5479,Next::call,0x5473,Next::jump},
        {0x557E,true, 0x553D,Next::call,0x5580,Next::call}};
    for (const auto& branch:branches)
        add(branch.at,{F(after(branch.taken,3)),F(after(branch.otherwise,3))},{.scratch_stack=2},
            [branch](auto& z) -> Exit { return dialogue_branch(z,branch); });

    // ---- the cake seller ----
    add_load(0x510D,0x5116,[](auto& z) -> Exit { return cake_offer(z); });
    add(0x511C,{F(0x5121),C(0x512D)},{},[](auto& z) -> Exit { return cake_answer(z); });                    // after the yes/no (00:2D19)
    add(0x5124,{C(0x512D)},{},[](auto& z) -> Exit { return cake_money(z); });
    add(0x5135,{C(0x513A)},{},[](auto& z) -> Exit { return cake_capacity(z); });
    add_load(0x5142,0x5148,[](auto& z) -> Exit { return cake_paid(z); });
    add(0x5153,{R(0x5153),F(0x5154)},{},[](auto& z) -> Exit { return add_unless_carried(z,0x5153); });  // after the search (00:282E)

    // ---- recruiting Noah ----
    add(0x515F,{F(0x516C)},{},[](auto& z) -> Exit { return party_reply(z); });                              // after the encounter setup
    add(0x516F,{C(0x5174)},{},[](auto& z) -> Exit { return recruit_party_full(z); });
    add(0x518A,{R(0x518A),F(0x518B)},{.scratch_stack=2},[](auto& z) -> Exit { return recruit_letter(z); }); // after the search (00:282E)
    add(0x518E,{F(0x518F,2)},{.inherited_stack=2},[](auto& z) -> Exit { return recruit_unwind(z); });       // after the removal (00:277F)
    add_load(0x5195,0x51A6,[](auto& z) -> Exit { return recruit_equip(z); });                               // after the button wait

    // ---- conversation progress ----
    add(0x51B1,{C(0x51B7)},{},[](auto& z) -> Exit { return progress_gate(z); });
    add_load(0x51C2,0x51D3,[](auto& z) -> Exit { return progress_reply(z); });                              // after the objects are redrawn
    add(0x51DC,{C(0x51E0)},{},[](auto& z) -> Exit { return introduction_gate(z); });
    add(0x51EB,{C(0x51F0)},{},[](auto& z) -> Exit { return visit_count(z); });                              // after the objects are redrawn
    add_load(0x51F2,0x51F6,[](auto& z) -> Exit { return first_visit(z); });
    add(0x51F9,{C(0x51FB)},{},[](auto& z) -> Exit { return visit_second(z); });
    add_load(0x51FD,0x5201,[](auto& z) -> Exit { return second_visit(z); });
    add_load(0x520D,0x521A,[](auto& z) -> Exit { return introduction_answer(z); });                         // after the yes/no (00:2D19)

    // ---- the road-pass guards ----
    add(0x522E,{F(0x5232),F(0x5240)},{.scratch_stack=2},[](auto& z) -> Exit { return pass_answer(z); });    // after the yes/no (00:2D19)
    add(0x5235,{F(0x523A),F(0x5240)},{.scratch_stack=2},[](auto& z) -> Exit { return pass_carried(z); });   // after the search (00:282E)
    add(0x535C,{F(0x5360),F(0x5383)},{.scratch_stack=2},[](auto& z) -> Exit { return dungeon_guard_answer(z); });
    add(0x536E,{R(0x537F,2)},{.inherited_stack=2},[](auto& z) -> Exit { return dungeon_guard_through(z); }); // after the window is closed (00:3464)
    add(0x538C,{F(0x5390)},{.scratch_stack=2},[](auto& z) -> Exit { return after_battle_menu(z); });       // after the battle (00:100F)
    add(0x5393,{R(0x5394,2)},{.inherited_stack=2},[](auto& z) -> Exit { return after_battle_leave(z); });

    // ---- the cola trade ----
    add(0x5252,{F(0x5257),F(0x525C)},{.scratch_stack=2},[](auto& z) -> Exit { return cola_answer(z); });    // after the yes/no (00:2D19)
    add(0x525F,{F(0x5261),F(0x526D)},{.scratch_stack=2},[](auto& z) -> Exit { return cola_carried(z); });   // after the search (00:282E)

    // ---- the crystal ----
    add(0x5333,{R(0x5333),F(0x5334)},{},[](auto& z) -> Exit { return add_unless_carried(z,0x5333); });     // after the search (00:282E)

    // ---- the cake guard ----
    add(0x53D0,{F(0x53DC),F(0x53D5)},{.scratch_stack=2},[](auto& z) -> Exit { return cake_guard_answer(z); });
    add(0x53DF,{F(0x53E2,-2),F(0x53F4)},{.scratch_stack=4},[](auto& z) -> Exit { return cake_guard_carried(z); });
    add(0x53E5,{F(0x53EE,2)},{.inherited_stack=2},[](auto& z) -> Exit { return cake_guard_paid(z); });     // after the removal (00:277F)
    add(0x53F7,{F(0x53F8,2)},{.inherited_stack=2},[](auto& z) -> Exit { return cake_guard_leave(z); });

    // ---- the item exchange ----
    add(0x540A,{F(0x5414),F(0x540F)},{.scratch_stack=2},[](auto& z) -> Exit { return exchange_answer(z); });
    add(0x5417,{F(0x5419),F(0x542D)},{.scratch_stack=2},[](auto& z) -> Exit { return exchange_carried(z); });

    // ---- the solo duel ----
    add(0x5438,{F(0x5449),F(0x5441)},{.scratch_stack=2},[](auto& z) -> Exit { return duel_armour_worn(z); }); // after the encounter setup
    add(0x5444,{C(0x5451),F(0x5449)},{},[](auto& z) -> Exit { return duel_armour_carried(z); });
    add(0x544C,{C(0x5451)},{},[](auto& z) -> Exit { return duel_party_size(z); });
    add(0x546D,{C(0x5471)},{},[](auto& z) -> Exit { return duel_noah_status(z); });
    add(0x5482,{F(0x549C,-8)},{.scratch_stack=10},[](auto& z) -> Exit { return duel_start(z); });
    add(0x549F,{C(0x54B1,8)},{.inherited_stack=8},[](auto& z) -> Exit { return duel_end(z); });             // after the battle (00:100F)
    add_load(0x54B3,0x54BF,[](auto& z) -> Exit { return duel_lost(z); });

    // ---- fixed encounters and the post-battle check ----
    add_load(0x54E4,0x54EC,[](auto& z) -> Exit { return encounter_won(z); });
    add(0x54F2,{R(0x54F7),R(0x54FA,4)},{.inherited_stack=4},[](auto& z) -> Exit { return battle_outcome(z); }); // after the battle (00:100F)
    add(0x54FB,{C(0x54FF)},{},[](auto& z) -> Exit { return won_gate(z); });
    add_load(0x5524,0x552C,[](auto& z) -> Exit { return gate_won(z); });
    add(0x553B,{R(0x553C,2)},{.inherited_stack=2},[](auto& z) -> Exit { return leave_after_field_menu(z); });      // after the field menu (00:1BE1)

    // ---- Hapsby and the vehicles ----
    add(0x554E,{F(0x5540),F(0x5550)},{.scratch_stack=2},[](auto& z) -> Exit { return robot_carried(z); });   // after the search (00:282E)
    add_load(0x5553,0x5557,[](auto& z) -> Exit { return robot_wakes(z); });
    add(0x5566,{F(0x5540),F(0x5571)},{.scratch_stack=2},[](auto& z) -> Exit { return vehicle_flag(z); });
    add(0x5574,{F(0x5540),F(0x557B)},{.scratch_stack=2},[](auto& z) -> Exit { return vehicle_robot(z); });   // after the search (00:282E)

    // ---- other encounters and the transfer ----
    add_load(0x55B1,0x55B8,[](auto& z) -> Exit { return encounter_clear(z); });
    add(0x55C3,{F(0x55CA,2)},{.inherited_stack=2},[](auto& z) -> Exit { return encounter_without_reward(z); });
    add(0x55EF,{C(0x55F4)},{},[](auto& z) -> Exit { return transfer_progress(z); });
    add_load(0x5602,0x560F,[](auto& z) -> Exit { return transfer_by_side(z); });

    // ---- the party-status report (unit 01:56A2) ----
    add(0x56A2,{F(0x56B2,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return status_report(z); });
    add_in(0x56B5,0x56A2,{F(0x56B2),F(0x56C0,2)},{.scratch_stack=2,.inherited_stack=2},[](auto& z) -> Exit { return status_next(z); });
    add_load(0x56BE,0x56C0,[](auto& z) -> Exit { return survivor_check(z); });
    add_in(0x56C3,0x56A2,{F(0x56CA),F(0x56C7),F(0x56C0)},{.scratch_stack=2},[](auto& z) -> Exit { return survivor_found(z); }); // after the alive check (00:188E)

    // ---- travel requests: each ret nz is its own unit, its request a second entry ----
    for (const auto at:{std::uint16_t{0x56E2},std::uint16_t{0x56F2}}) {
        add(at,{R(at),R(after(at,6))},{},[at](auto& z) -> Exit { return request_travel_if_yes(z,at); });      // after the yes/no (00:2D19)
        add_in(after(at,1),at,{R(after(at,6))},{},[at](auto& z) -> Exit { return request_travel(z,after(at,1)); });
    }

    // ---- the destination menu (unit 01:56FF) ----
    add(0x56FF,{F(0x5700,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return destination_menu(z); });
    add_in(0x5703,0x56FF,{R(0x5706,2),C(0x5712,2)},{.inherited_stack=2},[](auto& z) -> Exit { return destination_chosen(z); }); // after 00:3A21
    add_in(0x5707,0x56FF,{C(0x5712)},{},[](auto& z) -> Exit { return destination_compare(z); });
    add_load(0x5714,0x5723,[](auto& z) -> Exit { return destination_already_here(z); });
    add_in(0x5728,0x56FF,{F(0x5739,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return destination_question(z); });
    add_in(0x5738,0x56FF,{F(0x5739,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return destination_hold(z); });
    add_in(0x573F,0x56FF,{F(0x56FC,2),R(0x5751,2)},{.inherited_stack=2},[](auto& z) -> Exit { return destination_answer(z); }); // after the yes/no (00:2D19)
    add_in(0x5742,0x56FF,{R(0x5751)},{},[](auto& z) -> Exit { return destination_request(z); });
}
}

void register_scene_quests(RoutineRegistry& registry) {
    register_all(registry);
}
}
