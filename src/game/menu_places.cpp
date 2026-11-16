// SPDX-License-Identifier: GPL-3.0-or-later
// Menus and windows (milestone 12), part places:
// Places: the hospital 00:298D, the church 00:2A85 with its next-level report,
// the shops 00:2BC0, 00:2C8F, 00:2C98 and selling 00:2CB1, and the full restore
// 00:2A6D.
// Planned segments: menu::places_segments in game/menu.h, rendered with
// game/menu_kit.h.
//
// What the code does. Every place is a conversation: the game shows a line of
// dialogue (the native routine 00:31CF, a bare call that ends a segment), asks
// a yes-or-no question (00:2D19), lets the player pick a party member
// (00:3665) or an item (00:38D9, 00:34D5) and closes the window it opened
// (00:36BB, 00:3464). Those calls all run natively. What the segments here
// render is the decision making between two such calls: which line comes next,
// the arithmetic of prices, and the bookkeeping of the party's records, so a
// segment is short and always ends on the next bare call (or on a jump to the
// common exit 00:3464, which stays native).
//
// The hospital (00:298D) asks whether the party wants healing, lets the player
// pick a member, and says so when the member is dead, already healthy or the
// party is alone. The price is the missing hit points plus the missing magic
// points (record bytes 6 and 7 against 1 and 2 of the member), taken from the
// money if it is enough; the member's points are then set to their maxima and
// the jingle C1h is requested. 00:2A6D restores the whole party in the same
// way, for every living member, with no price.
//
// The church (00:2A85) revives dead members. It remembers one field of the
// party first (C2DB into C317), asks, lets the player pick a member (a living
// one is refused), prices the revival at 20 times the level (the level times
// 4, times 5), takes the money, marks the member alive, refills the points and
// plays the jingle C5h with a 30-frame wait (00:2D33). Declining or an empty
// party ends with the report of 00:2B58: for each of the four characters that
// is alive and below level 30, the experience still missing for the next
// level (00:2B84, read from the level table of bank 3).
//
// The shops (00:2BC0, with a second greeting at 00:2C8F, and the buy-or-sell
// kind at 00:2C98) loop on: pick an item from the shop's list, refuse when the
// item list is full or the money is short, and otherwise pay and add the item
// (00:279F). Items numbered 40h or more are the vehicles: the first two
// purchases (the counter C2EC) only tell what the vehicle needs (dialogues
// 0142h, 0144h) and the third, when a free slot exists for it, pays and adds
// the Road Pass (item ID from the ROM) and says 0146h. Selling (00:2CB1)
// looks the item's price up in a bank-3 table (a zero price means it cannot be
// sold: dialogue BD7Eh), asks, removes the item (00:277C) and adds the money
// (00:297A).
#include "game/menu.h"
#include "game/menu_kit.h"
#include "game/routines.h"
#include <utility>

namespace psmodern::game {
namespace {
namespace mk=menu_kit;
namespace menu=game::menu;
using mk::Exit;
using mk::Index;

constexpr unsigned button_one=4;   // `bit 4,c`: the pressed-button byte's first-button bit (the ROM's constant)

Exit call_at(std::uint16_t at) { return mk::continues(at,mk::call_cycles); }
Exit call_if_at(std::uint16_t at, bool taken) { return mk::continues(at,taken ? mk::call_cycles : mk::untaken_call_cycles); }
Exit jump_at(std::uint16_t at) { return mk::continues(at,mk::jump_cycles); }
// `ld hl,nn` at `at` and the bare call of the dialogue routine right after it.
template<class Z> Exit say(Z& z, std::uint16_t at) { z.load_hl(at); return call_at(static_cast<std::uint16_t>(at+3)); }

// ---- The hospital: 00:298D-2A6A --------------------------------------------------------------
// The member's record (HL from the native alive check, copied to IY): bytes 1-2 are the
// current points, bytes 6-7 their maxima; the price is the sum of what is missing.
template<class Z> Exit hospital_after_alive(Z& z) {                // 00:29BC-29FB, after the jr nz at 29B1
    auto& r=z.r;
    z.push(0x29BC,r.h,r.l);
    z.pop_index(0x29BD,Index::iy);
    z.load_from_indexed(0x29BF,Index::iy,r.a);
    z.compare_indexed(0x29C2,Index::iy);
    bool differ=z.relative_if(0x29C5,!z.zero());
    if (!differ) {
        z.load_from_indexed(0x29C7,Index::iy,r.a);
        z.compare_indexed(0x29CA,Index::iy);
        differ=z.relative_if(0x29CD,!z.zero());
    }
    if (!differ) return say(z,0x29CF);                              // nothing to heal
    z.load_from_indexed(0x29E4,Index::iy,r.a);
    z.subtract_indexed(0x29E7,Index::iy);
    z.copy(0x29EA,r.b,r.a);
    z.load_from_indexed(0x29EB,Index::iy,r.a);
    z.subtract_indexed(0x29EE,Index::iy);
    z.add_register(0x29F1,r.b);
    z.copy(0x29F2,r.l,r.a);
    z.load_immediate(0x29F3,r.h);
    z.store_hl_direct(0x29F5);                                      // the price, for the dialogue
    return say(z,0x29F8);
}
// 00:2999-29A0: the greeting for a party of more than one is skipped when the party is alone.
template<class Z> Exit hospital_choose(Z& z) {
    z.load_a_direct(0x2999);
    z.or_a(0x299C);
    z.load_hl(0x299D);
    return call_if_at(0x29A0,!z.zero());
}

// ---- The church: 00:2A85-2B4F ----------------------------------------------------------------
template<class Z> Exit church_choose(Z& z) {                       // 00:2A98-2AA2
    z.load_a_direct(0x2A98);
    z.or_a(0x2A9B);
    if (z.jump_if(0x2A9C,z.zero())) {                               // alone: 00:2B31
        z.store_a_direct(0x2B31);
        return say(z,0x2B34);
    }
    return say(z,0x2A9F);
}

// ---- The next-level report: 00:2B58-2BBD -------------------------------------------------------
// One character: the record is IY, DE the level table. A dead character is skipped (a taken
// `ret z`); a character at the top level gets one line, anyone else the experience missing
// for the next level (the table's entry 8 bytes per level, its word 4-5 against the
// character's experience at bytes 3-4).
template<class Z> Exit next_level_member(Z& z) {                    // 00:2B84-2BBD
    auto& r=z.r;
    z.store_a_direct(0x2B84);
    z.test_bit_indexed(0x2B87,Index::iy,0);
    if (z.zero()) return mk::returns(0x2B8B,mk::taken_conditional_return_cycles);
    z.return_not_taken(0x2B8B);
    z.load_from_indexed(0x2B8C,Index::iy,r.a);
    z.compare_immediate(0x2B8F);
    if (!z.relative_if(0x2B91,z.carry())) {
        z.load_hl(0x2B93);
        return jump_at(0x2B96);                                      // jp 31CF: the dialogue routine ends the report
    }
    z.load_hl(0x2B99);
    z.store_to_hl(0x2B9C);                                           // bank 3 into slot 2
    z.copy(0x2B9E,r.l,r.a);
    z.load_immediate(0x2B9F,r.h);
    z.add_to_hl(0x2BA1,z.hl());
    z.add_to_hl(0x2BA2,z.hl());
    z.add_to_hl(0x2BA3,z.hl());
    z.add_to_hl(0x2BA4,z.de());
    z.push(0x2BA5,r.h,r.l);
    z.pop_index(0x2BA6,Index::ix);
    z.load_from_indexed(0x2BA8,Index::iy,r.e);
    z.load_from_indexed(0x2BAB,Index::iy,r.d);
    z.load_from_indexed(0x2BAE,Index::ix,r.l);
    z.load_from_indexed(0x2BB1,Index::ix,r.h);
    z.or_a(0x2BB4);
    z.subtract_hl_with_carry(0x2BB5,z.de());
    z.store_hl_direct(0x2BB7);
    z.load_hl(0x2BBA);
    return jump_at(0x2BBD);
}
// 00:2B58-2B84: one character of the four, up to the call of 00:2B84.
template<class Z> Exit next_level_load(Z& z, std::uint16_t at, bool first_load_is_xor) {
    z.load_index(at,Index::iy);
    z.load_de(static_cast<std::uint16_t>(at+4));
    if (first_load_is_xor) z.xor_a(static_cast<std::uint16_t>(at+7));
    else z.load_immediate(static_cast<std::uint16_t>(at+7),z.r.a);
    return call_at(static_cast<std::uint16_t>(first_load_is_xor ? at+8 : at+9));
}

// ---- The shops: 00:2BC0-2D16 -------------------------------------------------------------------
// 00:2C46: a vehicle (item 40h or more). The counter C2EC counts purchases tried; below 2 the
// shop only explains (dialogue 0142h, then 0144h); from the third the Road Pass is bought when
// a free slot exists, and the counter is cleared (the free slot call, then 00:2C74 goes on).
template<class Z> Exit shop_vehicle(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x2C46);
    z.compare_immediate(0x2C49);
    if (z.relative_if(0x2C4B,!z.carry())) {                          // 00:2C67
        z.xor_a(0x2C67);
        z.store_a_direct(0x2C68);
        z.push(0x2C6B,r.h,r.l);
        z.load_immediate(0x2C6C,r.a);
        z.store_a_direct(0x2C6E);
        return call_at(0x2C71);
    }
    z.compare_immediate(0x2C4D);
    z.load_hl(0x2C4F);
    if (!z.relative_if(0x2C52,z.carry())) z.load_hl(0x2C54);
    z.increment(0x2C57,r.a);
    z.store_a_direct(0x2C58);
    return call_at(0x2C5B);
}
// 00:2CEA: the goodbye line, then the native close (00:3464).
template<class Z> Exit goodbye(Z& z) { return say(z,0x2CEA); }

void register_all(RoutineRegistry& registry) {
    using menu::at0;
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return mk::returned_at(at,stack); };
    auto add=[&](std::uint16_t entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,at0(entry),mk::segment(unit,std::move(exits),permissions),logic);
    };
    auto fixed=[&](std::uint16_t entry, RoutineEntry unit, std::vector<SegmentExit> exits, auto logic) {
        add(entry,unit,std::move(exits),{},logic);
    };

    // ---- hospital ----
    const auto h=menu::hospital_unit;
    fixed(0x298D,h,{F(0x2990)},[](auto& z) -> Exit { return say(z,0x298D); });
    fixed(0x2996,h,{F(0x29A0),F(0x2A58)},[](auto& z) -> Exit {
        if (z.jump_if(0x2996,!z.zero())) return say(z,0x2A55);
        return hospital_choose(z);
    });
    fixed(0x29A6,h,{F(0x29AE),F(0x2A52)},[](auto& z) -> Exit {
        z.test_bit(0x29A6,button_one,z.r.c);
        if (z.jump_if(0x29A8,!z.zero())) return call_at(0x2A52);
        z.store_a_direct(0x29AB);
        return call_at(0x29AE);
    });
    add(0x29B1,h,{F(0x29B6),F(0x29D2),F(0x29FB)},{.scratch_stack=2},[](auto& z) -> Exit {
        if (z.relative_if(0x29B1,!z.zero())) return hospital_after_alive(z);
        return say(z,0x29B3);
    });
    fixed(0x29D5,h,{F(0x29DE),F(0x2A48)},[](auto& z) -> Exit {
        z.load_a_direct(0x29D5);
        z.or_a(0x29D8);
        if (z.relative_if(0x29D9,!z.zero())) return call_at(0x2A48);
        return say(z,0x29DB);
    });
    add(0x2A04,h,{F(0x2A05,-2)},{.scratch_stack=2},[](auto& z) -> Exit {
        z.push(0x2A04,z.r.a,z.r.f);
        return call_if_at(0x2A05,!z.zero());
    });
    add(0x2A08,h,{F(0x2A1A,2),F(0x2A48,2),F(0x2A5E,2)},{.inherited_stack=2},[](auto& z) -> Exit {
        auto& r=z.r;
        z.pop(0x2A08,r.a,r.f);
        if (z.relative_if(0x2A09,!z.zero())) return call_at(0x2A48);
        z.load_pair_direct(0x2A0B,r.d,r.e);
        z.load_hl_direct(0x2A0F);
        z.or_a(0x2A12);
        z.subtract_hl_with_carry(0x2A13,z.de());
        if (z.relative_if(0x2A15,z.carry())) return call_at(0x2A5E);
        z.store_hl_direct(0x2A17);
        return call_at(0x2A1A);
    });
    fixed(0x2A1D,h,{F(0x2A31)},[](auto& z) -> Exit {
        auto& r=z.r;
        z.load_immediate(0x2A1D,r.a);
        z.store_a_direct(0x2A1F);                                    // sound request C1h
        z.load_from_indexed(0x2A22,Index::iy,r.a);
        z.store_to_indexed(0x2A25,Index::iy,r.a);
        z.load_from_indexed(0x2A28,Index::iy,r.a);
        z.store_to_indexed(0x2A2B,Index::iy,r.a);
        return say(z,0x2A2E);
    });
    fixed(0x2A37,h,{F(0x2A40),F(0x2A6A)},[](auto& z) -> Exit {
        z.load_a_direct(0x2A37);
        z.or_a(0x2A3A);
        if (z.relative_if(0x2A3B,z.zero())) return jump_at(0x2A6A);
        return say(z,0x2A3D);
    });
    fixed(0x2A46,h,{F(0x2A48),F(0x2A52)},[](auto& z) -> Exit {
        if (z.relative_if(0x2A46,!z.zero())) return call_at(0x2A52);
        return call_at(0x2A48);
    });
    fixed(0x2A4B,h,{F(0x29A0),F(0x2A52)},[](auto& z) -> Exit {
        z.load_a_direct(0x2A4B);
        z.or_a(0x2A4E);
        if (z.jump_if(0x2A4F,!z.zero())) return hospital_choose(z);
        return call_at(0x2A52);
    });
    fixed(0x2A55,h,{F(0x2A58)},[](auto& z) -> Exit { return say(z,0x2A55); });
    fixed(0x2A64,h,{F(0x2A67)},[](auto& z) -> Exit { return say(z,0x2A64); });

    // ---- the full restore ----
    const auto restore=menu::restore_all_unit;
    fixed(0x2A6D,restore,{F(0x2A72)},[](auto& z) -> Exit {
        z.load_bc(0x2A6D);
        z.copy(0x2A70,z.r.a,z.r.b);
        z.decrement(0x2A71,z.r.a);
        return call_at(0x2A72);
    });
    fixed(0x2A75,restore,{F(0x2A72),R(0x2A84)},[](auto& z) -> Exit {
        auto& r=z.r;
        if (!z.relative_if(0x2A75,z.zero())) {
            z.load_immediate(0x2A77,r.a);
            z.add_register(0x2A79,r.l);
            z.copy(0x2A7A,r.e,r.a);
            z.copy(0x2A7B,r.d,r.h);
            z.exchange_de_hl(0x2A7C);
            z.increment_pair(0x2A7D,r.d,r.e);
            z.load_increment(0x2A7E);
            z.load_increment(0x2A80);
        }
        if (z.decrement_jump(0x2A82)) {
            z.copy(0x2A70,r.a,r.b);
            z.decrement(0x2A71,r.a);
            return call_at(0x2A72);
        }
        return mk::returns(0x2A84,mk::return_cycles);
    });

    // ---- the church ----
    const auto c=menu::church_unit;
    fixed(0x2A85,c,{F(0x2A8E)},[](auto& z) -> Exit {
        z.load_a_direct(0x2A85);
        z.store_a_direct(0x2A88);
        return say(z,0x2A8B);
    });
    fixed(0x2A94,c,{F(0x2AA2),F(0x2B37),F(0x2B49)},[](auto& z) -> Exit {
        z.or_a(0x2A94);
        if (z.jump_if(0x2A95,!z.zero())) return say(z,0x2B46);
        return church_choose(z);
    });
    fixed(0x2A98,c,{F(0x2AA2),F(0x2B37)},[](auto& z) -> Exit { return church_choose(z); });
    fixed(0x2AA8,c,{F(0x2AAD),F(0x2B43)},[](auto& z) -> Exit {
        z.test_bit(0x2AA8,button_one,z.r.c);
        if (z.jump_if(0x2AAA,!z.zero())) return call_at(0x2B43);
        return call_at(0x2AAD);
    });
    add(0x2AB0,c,{F(0x2ACB),F(0x2B37)},{.scratch_stack=2},[](auto& z) -> Exit {
        auto& r=z.r;
        if (z.relative_if(0x2AB0,!z.zero())) {
            z.store_a_direct(0x2B31);
            return say(z,0x2B34);
        }
        z.store_a_direct(0x2AB2);
        z.push(0x2AB5,r.h,r.l);
        z.pop_index(0x2AB6,Index::iy);
        z.load_from_indexed(0x2AB8,Index::iy,r.a);
        z.add_register(0x2ABB,r.a);
        z.add_register(0x2ABC,r.a);
        z.copy(0x2ABD,r.l,r.a);
        z.load_immediate(0x2ABE,r.h);
        z.copy(0x2AC0,r.e,r.l);
        z.copy(0x2AC1,r.d,r.h);
        z.add_to_hl(0x2AC2,z.hl());
        z.add_to_hl(0x2AC3,z.hl());
        z.add_to_hl(0x2AC4,z.de());
        z.store_hl_direct(0x2AC5);                                   // the price: level x 20
        return say(z,0x2AC8);
    });
    add(0x2AD4,c,{F(0x2AD5,-2)},{.scratch_stack=2},[](auto& z) -> Exit {
        z.push(0x2AD4,z.r.a,z.r.f);
        return call_if_at(0x2AD5,!z.zero());
    });
    add(0x2AD8,c,{F(0x2ADE,2),F(0x2B18,2)},{.inherited_stack=2},[](auto& z) -> Exit {
        z.pop(0x2AD8,z.r.a,z.r.f);
        if (z.relative_if(0x2AD9,!z.zero())) return say(z,0x2B15);
        return say(z,0x2ADB);
    });
    fixed(0x2AE1,c,{F(0x2AF1),F(0x2B29)},[](auto& z) -> Exit {
        auto& r=z.r;
        z.load_pair_direct(0x2AE1,r.d,r.e);
        z.load_hl_direct(0x2AE5);
        z.or_a(0x2AE8);
        z.subtract_hl_with_carry(0x2AE9,z.de());
        if (z.jump_if(0x2AEB,z.carry())) return say(z,0x2B26);
        z.store_hl_direct(0x2AEE);
        return call_at(0x2AF1);
    });
    fixed(0x2AF4,c,{F(0x2B07)},[](auto& z) -> Exit {
        auto& r=z.r;
        z.store_immediate_indexed(0x2AF4,Index::iy);                 // the member is alive again
        z.load_from_indexed(0x2AF8,Index::iy,r.a);
        z.store_to_indexed(0x2AFB,Index::iy,r.a);
        z.load_from_indexed(0x2AFE,Index::iy,r.a);
        z.store_to_indexed(0x2B01,Index::iy,r.a);
        return say(z,0x2B04);
    });
    fixed(0x2B0A,c,{F(0x2B0F)},[](auto& z) -> Exit {
        z.load_immediate(0x2B0A,z.r.a);
        z.store_a_direct(0x2B0C);                                    // sound request C5h
        return call_at(0x2B0F);
    });
    fixed(0x2B15,c,{F(0x2B18)},[](auto& z) -> Exit { return say(z,0x2B15); });
    fixed(0x2B1E,c,{F(0x2B20),F(0x2B43)},[](auto& z) -> Exit {
        if (z.relative_if(0x2B1E,!z.zero())) return call_at(0x2B43);
        return call_at(0x2B20);
    });
    fixed(0x2B2F,c,{F(0x2B18)},[](auto& z) -> Exit {
        z.relative(0x2B2F);
        return say(z,0x2B15);
    });
    fixed(0x2B3A,c,{F(0x2B18),F(0x2B40)},[](auto& z) -> Exit {
        z.load_a_direct(0x2B3A);
        z.or_a(0x2B3D);
        if (z.relative_if(0x2B3E,!z.zero())) return say(z,0x2B15);
        return call_at(0x2B40);
    });
    fixed(0x2B46,c,{F(0x2B49)},[](auto& z) -> Exit { return say(z,0x2B46); });
    fixed(0x2B4C,c,{F(0x2B4F)},[](auto& z) -> Exit { return say(z,0x2B4C); });

    // ---- the next-level report ----
    const auto report=menu::next_level_report_unit;
    fixed(0x2B58,report,{F(0x2B60)},[](auto& z) -> Exit { return next_level_load(z,0x2B58,true); });
    fixed(0x2B63,report,{F(0x2B6C)},[](auto& z) -> Exit { return next_level_load(z,0x2B63,false); });
    fixed(0x2B6F,report,{F(0x2B78)},[](auto& z) -> Exit { return next_level_load(z,0x2B6F,false); });
    add(0x2B7B,report,{R(0x2B8B),F(0x2B96),F(0x2BBD)},{.slot2_mapper=true,.scratch_stack=2},[](auto& z) -> Exit {
        auto& r=z.r;
        z.load_index(0x2B7B,Index::iy);
        z.load_de(0x2B7F);
        z.load_immediate(0x2B82,r.a);
        return next_level_member(z);
    });
    add(0x2B84,menu::next_level_member_unit,{R(0x2B8B),F(0x2B96),F(0x2BBD)},{.slot2_mapper=true,.scratch_stack=2},
        [](auto& z) -> Exit { return next_level_member(z); });

    // ---- the shop ----
    const auto s=menu::shop_unit;
    fixed(0x2BC0,s,{F(0x2BC3)},[](auto& z) -> Exit { return say(z,0x2BC0); });
    add(0x2BC9,s,{F(0x2BCE),F(0x2BD5,-2)},{.scratch_stack=2},[](auto& z) -> Exit {
        if (z.relative_if(0x2BC9,z.zero())) {
            z.push(0x2BD4,z.r.b,z.r.c);
            return call_at(0x2BD5);
        }
        return say(z,0x2BCB);
    });
    add(0x2BDB,s,{F(0x2BDF,2)},{.inherited_stack=2},[](auto& z) -> Exit {
        z.pop(0x2BDB,z.r.b,z.r.c);
        return say(z,0x2BDC);
    });
    add(0x2BE2,s,{F(0x2BE3,-2)},{.scratch_stack=2},[](auto& z) -> Exit {
        z.push(0x2BE2,z.r.b,z.r.c);
        return call_at(0x2BE3);
    });
    add(0x2BE6,s,{F(0x2C0C,2),F(0x2C30,2),F(0x2C5B,2),F(0x2C71)},{.inherited_stack=2},[](auto& z) -> Exit {
        auto& r=z.r;
        z.test_bit(0x2BE6,button_one,r.c);
        z.pop(0x2BE8,r.b,r.c);
        if (z.relative_if(0x2BE9,!z.zero())) return say(z,0x2C2D);   // left the list: the goodbye line
        z.load_a_direct(0x2BEB);
        z.compare_immediate(0x2BEE);
        if (z.relative_if(0x2BF0,!z.carry())) {                      // the item list is full
            z.load_hl(0x2C3C);
            z.relative(0x2C3F);
            return call_at(0x2C30);
        }
        z.load_from_hl(0x2BF2,r.a);
        z.store_a_direct(0x2BF3);
        z.increment_hl(0x2BF6);
        z.load_from_hl(0x2BF7,r.e);
        z.increment_hl(0x2BF8);
        z.load_from_hl(0x2BF9,r.d);
        z.load_hl_direct(0x2BFA);
        z.or_a(0x2BFD);
        z.subtract_hl_with_carry(0x2BFE,z.de());
        if (z.relative_if(0x2C00,z.carry())) {                       // not enough money
            z.load_hl(0x2C41);
            z.relative(0x2C44);
            return call_at(0x2C30);
        }
        z.load_a_direct(0x2C02);
        z.compare_immediate(0x2C05);
        if (z.relative_if(0x2C07,!z.carry())) return shop_vehicle(z);
        z.store_hl_direct(0x2C09);
        return call_at(0x2C0C);
    });
    fixed(0x2C0F,s,{F(0x2C1A),F(0x2C1F)},[](auto& z) -> Exit {
        z.load_a_direct(0x2C0F);
        z.compare_immediate(0x2C12);
        if (z.relative_if(0x2C14,z.carry())) return call_at(0x2C1F);
        z.compare_immediate(0x2C16);
        if (z.relative_if(0x2C18,!z.carry())) return call_at(0x2C1F);
        return call_at(0x2C1A);
    });
    fixed(0x2C1D,s,{F(0x2C1F),F(0x2C25)},[](auto& z) -> Exit {
        if (z.relative_if(0x2C1D,z.zero())) return say(z,0x2C22);
        return call_at(0x2C1F);
    });
    fixed(0x2C22,s,{F(0x2C25)},[](auto& z) -> Exit { return say(z,0x2C22); });
    fixed(0x2C2B,s,{F(0x2BDF),F(0x2C30)},[](auto& z) -> Exit {
        if (z.relative_if(0x2C2B,z.zero())) return say(z,0x2BDC);
        return say(z,0x2C2D);
    });
    add(0x2C74,s,{F(0x2C5B,2),F(0x2C71),F(0x2C7A,2)},{.inherited_stack=2},[](auto& z) -> Exit {
        z.pop(0x2C74,z.r.h,z.r.l);
        if (z.relative_if(0x2C75,z.zero())) return shop_vehicle(z);
        z.store_hl_direct(0x2C77);
        return call_at(0x2C7A);
    });
    fixed(0x2C80,s,{F(0x2C83)},[](auto& z) -> Exit { return say(z,0x2C80); });

    // ---- the second shop kind and the buy-or-sell shop ----
    fixed(0x2C8F,menu::second_shop_unit,{F(0x2C92)},[](auto& z) -> Exit { return say(z,0x2C8F); });
    const auto b=menu::buy_sell_shop_unit;
    fixed(0x2C98,b,{F(0x2C9B)},[](auto& z) -> Exit { return say(z,0x2C98); });
    add(0x2CA1,b,{F(0x2CA3,-4)},{.scratch_stack=4},[](auto& z) -> Exit {
        z.push(0x2CA1,z.r.a,z.r.f);
        z.push(0x2CA2,z.r.b,z.r.c);
        return call_at(0x2CA3);
    });
    add(0x2CA6,b,{F(0x2BD5,2),F(0x2CB4,4),F(0x2CED,4)},{.inherited_stack=4},[](auto& z) -> Exit {
        auto& r=z.r;
        z.pop(0x2CA6,r.b,r.c);
        z.pop(0x2CA7,r.a,r.f);
        z.test_bit(0x2CA8,button_one,r.c);
        if (z.jump_if(0x2CAA,!z.zero())) return goodbye(z);
        z.or_a(0x2CAD);
        if (z.jump_if(0x2CAE,z.zero())) {                            // bought: 00:2BD4
            z.push(0x2BD4,r.b,r.c);
            return call_at(0x2BD5);
        }
        return say(z,0x2CB1);                                        // sold: the sell routine follows
    });

    // ---- selling ----
    const auto sell=menu::sell_unit;
    fixed(0x2CB1,sell,{F(0x2CB4)},[](auto& z) -> Exit { return say(z,0x2CB1); });
    add(0x2CBA,sell,{F(0x2CBD,-2)},{.scratch_stack=2},[](auto& z) -> Exit {
        z.test_bit(0x2CBA,button_one,z.r.c);
        z.push(0x2CBC,z.r.a,z.r.f);
        return call_at(0x2CBD);
    });
    add(0x2CC0,sell,{F(0x2CE4,2),F(0x2CED,2),F(0x2CF6,2)},{.slot2_mapper=true,.inherited_stack=2},[](auto& z) -> Exit {
        auto& r=z.r;
        z.pop(0x2CC0,r.a,r.f);
        if (z.jump_if(0x2CC1,!z.zero())) return goodbye(z);
        z.load_hl(0x2CC4);
        z.store_to_hl(0x2CC7);                                       // bank 3 into slot 2
        z.load_a_direct(0x2CC9);
        z.and_immediate(0x2CCC);
        z.add_register(0x2CCE,r.a);
        z.load_hl(0x2CCF);
        z.add_register(0x2CD2,r.l);
        z.copy(0x2CD3,r.l,r.a);
        z.add_with_carry(0x2CD4,r.h);
        z.subtract_register(0x2CD5,r.l);
        z.copy(0x2CD6,r.h,r.a);
        z.load_from_hl(0x2CD7,r.a);
        z.increment_hl(0x2CD8);
        z.load_from_hl(0x2CD9,r.h);
        z.copy(0x2CDA,r.l,r.a);
        z.store_hl_direct(0x2CDB);                                   // the selling price
        z.or_register(0x2CDE,r.h);
        if (z.relative_if(0x2CDF,!z.zero())) return say(z,0x2CF3);
        return say(z,0x2CE1);                                        // price zero: cannot be sold
    });
    fixed(0x2CFC,sell,{F(0x2CB4),F(0x2D01)},[](auto& z) -> Exit {
        if (z.relative_if(0x2CFC,z.zero())) return call_at(0x2D01);
        z.jump(0x2CFE);
        return say(z,0x2CB1);
    });
    fixed(0x2D04,sell,{F(0x2D07)},[](auto& z) -> Exit {
        z.load_hl_direct(0x2D04);
        return call_at(0x2D07);
    });
    fixed(0x2D0A,sell,{F(0x2D0D)},[](auto& z) -> Exit {
        z.load_hl(0x2D0A);
        return call_at(0x2D0D);
    });
    fixed(0x2D13,sell,{F(0x2CB4),F(0x2CED)},[](auto& z) -> Exit {
        if (z.jump_if(0x2D13,z.zero())) return say(z,0x2CB1);
        z.jump(0x2D16);
        return goodbye(z);
    });
}
}

void register_menu_places(RoutineRegistry& registry) {
    register_all(registry);
}
}
