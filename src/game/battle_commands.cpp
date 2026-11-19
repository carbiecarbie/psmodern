// SPDX-License-Identifier: GPL-3.0-or-later
// Battle command collection. Window/cursor routines keep their existing owner;
// this module interprets the choice, writes the action record and advances the
// actor. Spell and dialogue tables are always read from the player's ROM.
#include "game/joins.h"
#include "game/battle_commands.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;

// The action record has a four-byte stride; only its three command fields
// are changed. The untouched fourth byte remains as the original left it.
template <class Z> Exit write_action(Z &z) {
    auto &r = z.r;
    z.push(0x1BB9, r.a, r.f);
    z.load_a_direct(0x1BBA);
    z.add_register(0x1BBD, r.a);
    z.add_register(0x1BBE, r.a);
    z.load_hl(0x1BBF);
    z.add_register(0x1BC2, r.l);
    z.copy(0x1BC3, r.l, r.a);
    z.add_with_carry(0x1BC4, r.h);
    z.subtract_register(0x1BC5, r.l);
    z.copy(0x1BC6, r.h, r.a);
    z.pop(0x1BC7, r.a, r.f);
    z.store_register_to_hl(0x1BC8, r.c);
    z.increment_hl(0x1BC9);
    z.store_register_to_hl(0x1BCA, r.b);
    z.increment_hl(0x1BCB);
    z.store_register_to_hl(0x1BCC, r.a);
    return z.final_return(0x1BCD);
}
template <class Z> Exit read_action(Z &z) {
    auto &r = z.r;
    z.load_a_direct(0x1BCE);
    z.add_register(0x1BD1, r.a);
    z.add_register(0x1BD2, r.a);
    z.load_hl(0x1BD3);
    z.add_register(0x1BD6, r.l);
    z.copy(0x1BD7, r.l, r.a);
    z.add_with_carry(0x1BD8, r.h);
    z.subtract_register(0x1BD9, r.l);
    z.copy(0x1BDA, r.h, r.a);
    z.load_from_hl(0x1BDB, r.a);
    z.increment_hl(0x1BDC);
    z.load_from_hl(0x1BDD, r.b);
    z.increment_hl(0x1BDE);
    z.load_from_hl(0x1BDF, r.c);
    return z.final_return(0x1BE0);
}
template <class Z> Exit choose_attack(Z &z) {
    z.load_bc(0x191C);
    z.xor_a(0x191F);
    return z.final_call(0x1920);
}
template <class Z> Exit advance_after_attack(Z &z) {
    z.load_a_direct(0x1923);
    z.increment(0x1926, z.r.a);
    z.store_a_direct(0x1927);
    return z.final_return(0x192A);
}
// Failed communication ends the command phase and records its failure flag.
template <class Z> Exit talk_failure(Z &z) {
    z.load_immediate(0x1951, z.r.a);
    z.store_a_direct(0x1953);
    z.load_immediate(0x1956, z.r.a);
    z.store_a_direct(0x1958);
    z.load_hl(0x195B);
    return z.final_call(0x195E);
}
template <class Z> Exit talk_success_message(Z &z) {
    z.load_hl(0x1964);
    return z.final_call(0x1967);
}
template <class Z> Exit introduce_talker(Z &z) {
    z.load_a_direct(0x1934);
    z.store_a_direct(0x1937);
    z.load_hl(0x193A);
    return z.final_call(0x193D);
}
template <class Z> Exit check_talk_permission(Z &z) {
    z.load_a_direct(0x1940);
    z.and_immediate(0x1943);
    if (z.relative_if(0x1945, z.zero()))
        return talk_failure(z);
    z.load_a_direct(0x1947);
    z.copy(0x194A, z.r.b, z.r.a);
    z.load_a_direct(0x194B);
    z.compare_register(0x194E, z.r.b);
    if (z.relative_if(0x194F, !z.carry()))
        return talk_success_message(z);
    return talk_failure(z);
}
// Draw rejection is preserved: values outside the table retry the RNG call.
template <class Z> Exit choose_talk_reply(Z &z) {
    z.and_immediate(0x196D);
    z.compare_immediate(0x196F);
    if (z.relative_if(0x1971, !z.carry()))
        return z.final_call(0x196A);
    z.copy(0x1973, z.r.l, z.r.a);
    z.load_immediate(0x1974, z.r.h);
    z.add_to_hl(0x1976, z.hl());
    z.load_de(0x1977);
    z.add_to_hl(0x197A, z.de());
    z.load_from_hl(0x197B, z.r.a);
    z.increment_hl(0x197C);
    z.load_from_hl(0x197D, z.r.h);
    z.copy(0x197E, z.r.l, z.r.a);
    return z.final_call(0x197F);
}
template <class Z> Exit finish_talk(Z &z) {
    z.load_immediate(0x1982, z.r.a);
    z.store_a_direct(0x1984);
    return z.final_jump(0x1987);
}
template <class Z> Exit escape_success(Z &z) {
    z.load_immediate(0x19BA, z.r.a);
    z.store_a_direct(0x19BC);
    z.load_immediate(0x19BF, z.r.a);
    z.store_a_direct(0x19C1);
    return z.final_return(0x19C4);
}
template <class Z> Exit escape_failure_message(Z &z) {
    z.load_a_direct(0x19C5);
    z.store_a_direct(0x19C8);
    z.load_hl(0x19CB);
    return z.final_call(0x19CE);
}
template <class Z> Exit draw_escape_chance(Z &z) {
    z.load_a_direct(0x19A5);
    z.copy(0x19A8, z.r.b, z.r.a);
    return z.final_call(0x19A9);
}
template <class Z> Exit check_escape_chance(Z &z) {
    z.compare_register(0x19AC, z.r.b);
    if (z.relative_if(0x19AD, !z.carry()))
        return escape_failure_message(z);
    z.load_a_direct(0x19AF);
    z.or_a(0x19B2);
    if (z.relative_if(0x19B3, !z.zero()))
        return escape_success(z);
    return z.final_call(0x19B5);
}
template <class Z> Exit check_escape_route(Z &z) {
    if (z.relative_if(0x19B8, !z.zero()))
        return escape_failure_message(z);
    return escape_success(z);
}
template <class Z> Exit finish_failed_escape(Z &z) {
    z.load_immediate(0x19D1, z.r.a);
    z.store_a_direct(0x19D3);
    z.load_immediate(0x19D6, z.r.a);
    z.store_a_direct(0x19D8);
    return z.final_jump(0x19DB);
}
// The third party member cannot cast. The others get their ROM-defined list;
// the fourth member's list is drawn in the third display row.
template <class Z> Exit no_known_spells(Z &z) {
    z.load_hl(0x1A42);
    return z.final_call(0x1A45);
}
template <class Z> Exit insufficient_magic_points(Z &z) {
    z.load_hl(0x1A4B);
    return z.final_call(0x1A4E);
}
template <class Z> Exit choose_magic(Z &z) {
    auto &r = z.r;
    z.load_a_direct(0x19DE);
    z.store_a_direct(0x19E1);
    z.compare_immediate(0x19E4);
    if (!z.jump_if(0x19E6, !z.zero())) {
        z.load_hl(0x19E9);
        return z.final_call(0x19EC);
    }
    z.copy(0x19F2, r.c, r.a);
    for (unsigned at = 0x19F3; at <= 0x19F6; ++at)
        z.add_register(at, r.a);
    z.load_hl(0x19F7);
    z.add_register(0x19FA, r.l);
    z.copy(0x19FB, r.l, r.a);
    z.load_from_hl(0x19FC, r.a);
    z.or_a(0x19FD);
    if (z.jump_if(0x19FE, z.zero()))
        return no_known_spells(z);
    z.copy(0x1A01, r.b, r.a);
    z.copy(0x1A02, r.a, r.c);
    z.compare_immediate(0x1A03);
    if (!z.relative_if(0x1A05, !z.zero()))
        z.decrement(0x1A07, r.a);
    z.push(0x1A08, r.a, r.f);
    z.push(0x1A09, r.h, r.l);
    return z.final_call(0x1A0A);
}
template <class Z> Exit show_magic_cursor(Z &z) {
    z.load_hl(0x1A0D);
    z.store_hl_direct(0x1A10);
    z.pop(0x1A13, z.r.h, z.r.l);
    z.load_from_hl(0x1A14, z.r.a);
    z.decrement(0x1A15, z.r.a);
    z.store_a_direct(0x1A16);
    return z.final_call(0x1A19);
}
template <class Z> Exit accept_magic_choice(Z &z) {
    auto &r = z.r;
    z.pop(0x1A1C, r.h, r.l);
    z.test_bit(0x1A1D, 4, r.c);
    if (z.jump_if(0x1A1F, !z.zero()))
        return z.final_jump(0x1A3F);
    z.copy(0x1A22, r.l, r.a);
    z.copy(0x1A23, r.a, r.h);
    z.add_register(0x1A24, r.a);
    z.add_register(0x1A25, r.a);
    z.add_register(0x1A26, r.h);
    z.add_register(0x1A27, r.l);
    z.copy(0x1A28, r.l, r.a);
    z.load_immediate(0x1A29, r.h);
    z.load_de(0x1A2B);
    z.add_to_hl(0x1A2E, z.de());
    z.load_from_hl(0x1A2F, r.a);
    z.and_immediate(0x1A30);
    z.copy(0x1A32, r.b, r.a);
    return z.final_call(0x1A33);
}
template <class Z> Exit check_magic_balance(Z &z) {
    if (z.relative_if(0x1A36, z.carry()))
        return insufficient_magic_points(z);
    z.copy(0x1A38, z.r.a, z.r.b);
    z.load_hl(0x1A39);
    return z.final_call(0x1A3C);
}
// Some spells ask for a living recipient before queuing the action. The two
// original entry families have distinct addresses but the same decision flow.
template <class Z> Exit choose_spell_recipient(Z &z, unsigned at) {
    z.push(at, z.r.b, z.r.c);
    return z.final_call(at + 1);
}
template <class Z> Exit accept_spell_recipient(Z &z, unsigned at, unsigned close, unsigned check) {
    z.pop(at, z.r.d, z.r.e);
    z.test_bit(at + 1, 4, z.r.c);
    if (z.relative_if(at + 3, !z.zero()))
        return z.final_call(close);
    return z.final_call(check);
}
template <class Z> Exit queue_spell_for_recipient(Z &z, unsigned at, unsigned close) {
    if (z.relative_if(at, z.zero()))
        return z.final_call(close);
    z.load_immediate(at + 2, z.r.c);
    z.copy(at + 4, z.r.b, z.r.d);
    return z.final_call(at + 5);
}
template <class Z> Exit advance_spell_recipient(Z &z, unsigned at, unsigned close) {
    z.load_a_direct(at);
    z.increment(at + 3, z.r.a);
    z.store_a_direct(at + 4);
    return z.final_call(close);
}
template <class Z> Exit queue_untargeted_spell(Z &z) {
    z.load_immediate(0x1AD0, z.r.c);
    z.xor_a(0x1AD2);
    return z.final_call(0x1AD3);
}
template <class Z> Exit finish_untargeted_spell(Z &z) {
    z.load_a_direct(0x1AD6);
    z.increment(0x1AD9, z.r.a);
    z.store_a_direct(0x1ADA);
    return z.final_return(0x1ADD);
}
template <class Z> Exit queue_current_recipient_spell(Z &z) {
    z.load_immediate(0x1AFD, z.r.c);
    z.load_a_direct(0x1AFF);
    return z.final_call(0x1B02);
}
template <class Z> Exit finish_current_recipient_spell(Z &z) {
    z.load_a_direct(0x1B05);
    z.increment(0x1B08, z.r.a);
    z.store_a_direct(0x1B09);
    return z.final_return(0x1B0C);
}
template <class Z> Exit start_communication_spell(Z &z, unsigned at) {
    z.copy(at, z.r.a, z.r.b);
    return z.final_call(at + 1);
}
// Communication spells share the talk outcomes, and one variant also compares
// the leader's attack with the enemy's threshold before choosing the reply.
template <class Z> Exit improved_communication_message(Z &z) {
    z.load_immediate(0x1B43, z.r.a);
    z.store_a_direct(0x1B45);
    z.load_hl(0x1B48);
    return z.final_call(0x1B4B);
}
template <class Z> Exit check_communication_spell(Z &z, bool compare_attack) {
    const unsigned at = compare_attack ? 0x1B17 : 0x1B36;
    z.load_a_direct(at);
    z.and_immediate(at + 3);
    if (z.jump_if(at + 5, z.zero()))
        return talk_failure(z);
    z.and_immediate(at + 8);
    if (z.jump_if(at + 10, z.zero()))
        return talk_success_message(z);
    if (!compare_attack)
        return improved_communication_message(z);
    z.load_a_direct(0x1B24);
    z.copy(0x1B27, z.r.b, z.r.a);
    z.load_a_direct(0x1B28);
    z.compare_register(0x1B2B, z.r.b);
    if (z.relative_if(0x1B2C, !z.carry()))
        return improved_communication_message(z);
    return z.final_jump(0x1B2E);
}
template <class Z> Exit commit_communication_spell(Z &z, bool compare_attack) {
    if (compare_attack) {
        z.store_a_to_pair(0x1B11, z.de());
        z.load_immediate(0x1B12, z.r.a);
        z.store_a_direct(0x1B14);
    } else
        z.store_a_to_pair(0x1B35, z.de());
    return check_communication_spell(z, compare_attack);
}
template <class Z> Exit choose_improved_reply(Z &z) {
    z.and_immediate(0x1B51);
    z.compare_immediate(0x1B53);
    if (z.relative_if(0x1B55, !z.carry()))
        return z.final_call(0x1B4E);
    z.copy(0x1B57, z.r.l, z.r.a);
    z.load_immediate(0x1B58, z.r.h);
    z.add_to_hl(0x1B5A, z.hl());
    z.load_de(0x1B5B);
    z.add_to_hl(0x1B5E, z.de());
    z.load_from_hl(0x1B5F, z.r.a);
    z.increment_hl(0x1B60);
    z.load_from_hl(0x1B61, z.r.h);
    z.copy(0x1B62, z.r.l, z.r.a);
    return z.final_call(0x1B63);
}
template <class Z> Exit finish_improved_talk(Z &z) {
    z.load_immediate(0x1B66, z.r.a);
    z.store_a_direct(0x1B68);
    z.load_immediate(0x1B6B, z.r.a);
    z.store_a_direct(0x1B6D);
    return z.final_jump(0x1B70);
}
template <class Z> Exit accept_item_choice(Z &z) {
    z.test_bit(0x1BA4, 4, z.r.c);
    if (auto exit=z.final_return_if(0x1BA6,k::Condition::nz)) return *exit;
    z.load_a_direct(0x1BA7);
    z.copy(0x1BAA, z.r.b, z.r.a);
    z.load_immediate(0x1BAB, z.r.c);
    z.xor_a(0x1BAD);
    return z.final_call(0x1BAE);
}
template <class Z> Exit finish_item_choice(Z &z) {
    z.load_a_direct(0x1BB1);
    z.increment(0x1BB4, z.r.a);
    z.store_a_direct(0x1BB5);
    return z.final_return(0x1BB8);
}
} // namespace
void register_battle_commands(RoutineRegistry &registry) {
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };
    add(0x191C, 0x191C, {core_final_at(0x1920)}, {}, [](auto &z) { return choose_attack(z); });
    add(0x1923, 0x191C, {returned_at(0x192A)}, {}, [](auto &z) { return advance_after_attack(z); });
    add(0x1934, 0x192B, {core_final_at(0x193D)}, {}, [](auto &z) { return introduce_talker(z); });
    add(0x1940, 0x192B, {core_final_at(0x195E), core_final_at(0x1967)}, {},
        [](auto &z) { return check_talk_permission(z); });
    add(0x1951, 0x192B, {core_final_at(0x195E)}, {}, [](auto &z) { return talk_failure(z); });
    add(0x1964, 0x192B, {core_final_at(0x1967)}, {},
        [](auto &z) { return talk_success_message(z); });
    add(0x196D, 0x192B, {core_final_at(0x196A), core_final_at(0x197F)}, {},
        [](auto &z) { return choose_talk_reply(z); });
    add(0x1982, 0x192B, {core_final_at(0x1987)}, {}, [](auto &z) { return finish_talk(z); });
    add(0x19A5, 0x199C, {core_final_at(0x19A9)}, {}, [](auto &z) { return draw_escape_chance(z); });
    add(0x19AC, 0x199C, {core_final_at(0x19CE), core_final_at(0x19B5), returned_at(0x19C4)}, {},
        [](auto &z) { return check_escape_chance(z); });
    add(0x19B8, 0x199C, {core_final_at(0x19CE), returned_at(0x19C4)}, {},
        [](auto &z) { return check_escape_route(z); });
    add(0x19C5, 0x199C, {core_final_at(0x19CE)}, {},
        [](auto &z) { return escape_failure_message(z); });
    add(0x19D1, 0x199C, {core_final_at(0x19DB)}, {},
        [](auto &z) { return finish_failed_escape(z); });
    add(0x19DE, 0x19DE, {core_final_at(0x19EC), core_final_at(0x1A45), core_final_at(0x1A0A, -4)},
        {.scratch_stack = 4}, [](auto &z) { return choose_magic(z); });
    add(0x1A0D, 0x19DE, {core_final_at(0x1A19, 2)}, {.inherited_stack = 2},
        [](auto &z) { return show_magic_cursor(z); });
    add(0x1A1C, 0x19DE, {core_final_at(0x1A3F, 2), core_final_at(0x1A33, 2)},
        {.inherited_stack = 2}, [](auto &z) { return accept_magic_choice(z); });
    add(0x1A36, 0x19DE, {core_final_at(0x1A4E), core_final_at(0x1A3C)}, {},
        [](auto &z) { return check_magic_balance(z); });
    add(0x1A42, 0x19DE, {core_final_at(0x1A45)}, {}, [](auto &z) { return no_known_spells(z); });
    // After the "no spells" message the window is closed by a jump.
    auto close_window = segment({0, 0x19DE}, {core_final_at(0x1A48)});
    close_window.single_instruction = true;
    k::add_segment(registry, {0, 0x1A48}, std::move(close_window),
                   [](auto &z) { return z.final_jump(0x1A48); });
    add(0x1A4B, 0x19DE, {core_final_at(0x1A4E)}, {},
        [](auto &z) { return insufficient_magic_points(z); });
    for (unsigned entry : {0x1AB1u, 0x1ADEu})
        add(entry, entry, {core_final_at(entry + 1, -2)}, {.scratch_stack = 2},
            [entry](auto &z) { return choose_spell_recipient(z, entry); });
    add(0x1AB5, 0x1AB1, {core_final_at(0x1ACC, 2), core_final_at(0x1ABA, 2)},
        {.inherited_stack = 2},
        [](auto &z) { return accept_spell_recipient(z, 0x1AB5, 0x1ACC, 0x1ABA); });
    add(0x1ABD, 0x1AB1, {core_final_at(0x1ACC), core_final_at(0x1AC2)}, {},
        [](auto &z) { return queue_spell_for_recipient(z, 0x1ABD, 0x1ACC); });
    add(0x1AC5, 0x1AB1, {core_final_at(0x1ACC)}, {},
        [](auto &z) { return advance_spell_recipient(z, 0x1AC5, 0x1ACC); });
    add(0x1AD0, 0x1AD0, {core_final_at(0x1AD3)}, {},
        [](auto &z) { return queue_untargeted_spell(z); });
    add(0x1AD6, 0x1AD0, {returned_at(0x1ADD)}, {},
        [](auto &z) { return finish_untargeted_spell(z); });
    add(0x1AE2, 0x1ADE, {core_final_at(0x1AF9, 2), core_final_at(0x1AE7, 2)},
        {.inherited_stack = 2},
        [](auto &z) { return accept_spell_recipient(z, 0x1AE2, 0x1AF9, 0x1AE7); });
    add(0x1AEA, 0x1ADE, {core_final_at(0x1AF9), core_final_at(0x1AEF)}, {},
        [](auto &z) { return queue_spell_for_recipient(z, 0x1AEA, 0x1AF9); });
    add(0x1AF2, 0x1ADE, {core_final_at(0x1AF9)}, {},
        [](auto &z) { return advance_spell_recipient(z, 0x1AF2, 0x1AF9); });
    add(0x1AFD, 0x1AFD, {core_final_at(0x1B02)}, {},
        [](auto &z) { return queue_current_recipient_spell(z); });
    add(0x1B05, 0x1AFD, {returned_at(0x1B0C)}, {},
        [](auto &z) { return finish_current_recipient_spell(z); });
    add(0x1B0D, 0x1B0D, {core_final_at(0x1B0E)}, {},
        [](auto &z) { return start_communication_spell(z, 0x1B0D); });
    const auto communication_exits = std::initializer_list<SegmentExit>{
        core_final_at(0x195E), core_final_at(0x1967), core_final_at(0x1B4B), core_final_at(0x1B2E)};
    add(0x1B11, 0x1B0D, communication_exits, {},
        [](auto &z) { return commit_communication_spell(z, true); });
    add(0x1B17, 0x1B0D, communication_exits, {},
        [](auto &z) { return check_communication_spell(z, true); });
    add(0x1B31, 0x1B31, {core_final_at(0x1B32)}, {},
        [](auto &z) { return start_communication_spell(z, 0x1B31); });
    const auto simple_exits = std::initializer_list<SegmentExit>{
        core_final_at(0x195E), core_final_at(0x1967), core_final_at(0x1B4B)};
    add(0x1B35, 0x1B31, simple_exits, {},
        [](auto &z) { return commit_communication_spell(z, false); });
    add(0x1B36, 0x1B31, simple_exits, {},
        [](auto &z) { return check_communication_spell(z, false); });
    add(0x1B43, 0x1B31, {core_final_at(0x1B4B)}, {},
        [](auto &z) { return improved_communication_message(z); });
    add(0x1B51, 0x1B31, {core_final_at(0x1B4E), core_final_at(0x1B63)}, {},
        [](auto &z) { return choose_improved_reply(z); });
    add(0x1B66, 0x1B31, {core_final_at(0x1B70)}, {},
        [](auto &z) { return finish_improved_talk(z); });
    add(0x1BA4, 0x1B9E, {returned_at(0x1BA6), core_final_at(0x1BAE)}, {},
        [](auto &z) { return accept_item_choice(z); });
    add(0x1BB1, 0x1B9E, {returned_at(0x1BB8)}, {}, [](auto &z) { return finish_item_choice(z); });
    add(0x1BB9, 0x1BB9, {returned_at(0x1BCD)}, {.scratch_stack = 2},
        [](auto &z) { return write_action(z); });
    add(0x1BCE, 0x1BCE, {returned_at(0x1BE0)}, {}, [](auto &z) { return read_action(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_battle_commands_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x192B},{0,0x192B},{kit::core_final_at(0x192B)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x192B); });
    kit::add_joining(registry,{0,0x192E},{0,0x192B},{kit::core_final_at(0x192E)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x192E); });
    kit::add_joining(registry,{0,0x1931},{0,0x192B},{kit::core_final_at(0x1931)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1931); });
    kit::add_joining(registry,{0,0x1961},{0,0x192B},{kit::core_final_at(0x1961)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1961); });
    kit::add_joining(registry,{0,0x196A},{0,0x192B},{kit::core_final_at(0x196A)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x196A); });
    kit::add_joining(registry,{0,0x199C},{0,0x199C},{kit::core_final_at(0x199C)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x199C); });
    kit::add_joining(registry,{0,0x199F},{0,0x199C},{kit::core_final_at(0x199F)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x199F); });
    kit::add_joining(registry,{0,0x19A2},{0,0x199C},{kit::core_final_at(0x19A2)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x19A2); });
    kit::add_joining(registry,{0,0x19EF},{0,0x19DE},{kit::core_final_at(0x19EF)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x19EF); });
    kit::add_joining(registry,{0,0x1A3F},{0,0x19DE},{kit::core_final_at(0x1A3F)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1A3F); });
    kit::add_joining(registry,{0,0x1A51},{0,0x19DE},{kit::core_final_at(0x1A51)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1A51); });
    kit::add_joining(registry,{0,0x1A54},{0,0x19DE},{kit::core_final_at(0x1A54)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1A54); });
    kit::add_joining(registry,{0,0x1AAE},{0,0x1AAE},{kit::core_final_at(0x1AAE)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1AAE); });
    kit::add_joining(registry,{0,0x1ACF},{0,0x1AB1},{kit::returned_at(0x1ACF)},kit::return_cycles,0,[](auto& z) { return z.final_return(0x1ACF); });
    kit::add_joining(registry,{0,0x1AFC},{0,0x1ADE},{kit::returned_at(0x1AFC)},kit::return_cycles,0,[](auto& z) { return z.final_return(0x1AFC); });
    kit::add_joining(registry,{0,0x1B4E},{0,0x1B0D},{kit::core_final_at(0x1B4E)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1B4E); });
}
} // namespace psmodern::game
