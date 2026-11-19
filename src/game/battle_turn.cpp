// SPDX-License-Identifier: GPL-3.0-or-later
// Battle rounds: select commands, shuffle the twelve actor IDs, execute an
// actor through its existing entry, then check victory, defeat and escape.
#include "game/battle_turn.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;

// Battle music: encounter 48h requests sound 92h, encounter 49h keeps the
// music already playing, and every other encounter requests 89h. Then the
// twelve actor numbers are set up.
template <class Z> Exit initialize_battle(Z &z) {
    z.load_a_direct(0x100F);
    z.compare_immediate(0x1012);
    z.load_immediate(0x1014, z.r.c);
    bool keep_music = false;
    if (!z.relative_if(0x1016, z.zero())) {
        z.compare_immediate(0x1018);
        keep_music = z.relative_if(0x101A, z.zero());
        if (!keep_music)
            z.load_immediate(0x101C, z.r.c);
    }
    if (!keep_music) {
        z.copy(0x101E, z.r.a, z.r.c);
        z.store_a_direct(0x101F);
    }
    z.load_hl(0x1022);
    z.load_immediate(0x1025, z.r.b);
    do {
        z.copy(0x1027, z.r.a, z.r.b);
        z.decrement(0x1028, z.r.a);
        z.store_register_to_hl(0x1029, z.r.a);
        z.decrement_hl(0x102A);
    } while (z.decrement_jump(0x102B));
    z.xor_a(0x102D);
    z.store_a_direct(0x102E);
    return z.final_call(0x1031);
}
template <class Z> Exit active_candidate(Z &z) {
    z.copy(0x1036, z.r.a, z.r.b);
    z.decrement(0x1037, z.r.a);
    return z.final_call(0x1038);
}
template <class Z> Exit living_candidate(Z &z) {
    z.copy(0x1045, z.r.a, z.r.b);
    z.decrement(0x1046, z.r.a);
    return z.final_call(0x1047);
}
template <class Z> Exit find_active_party(Z &z) {
    z.load_immediate(0x1034, z.r.b);
    return active_candidate(z);
}
template <class Z> Exit accept_active_party(Z &z) {
    if (z.jump_if(0x103B, !z.zero())) {
        z.load_immediate(0x1043, z.r.b);
        return living_candidate(z);
    }
    if (z.decrement_jump(0x103E))
        return active_candidate(z);
    return z.final_jump(0x1040);
}
// Seed one zero and copy it over the fifteen following bytes. Reading each
// newly written byte preserves the original overlapping-clear behavior.
template <class Z> Exit begin_command_round(Z &z) {
    z.load_hl(0x1055);
    z.load_de(0x1058);
    z.load_bc(0x105B);
    z.store_to_hl(0x105E);
    z.block_copy(0x1060);
    z.load_immediate(0x1062, z.r.a);
    z.store_a_direct(0x1064);
    z.xor_a(0x1067);
    z.store_a_direct(0x1068);
    z.store_a_direct(0x106B);
    return z.final_call(0x106E);
}
template <class Z> Exit accept_living_party(Z &z) {
    z.increment_hl(0x104A);
    z.load_from_hl(0x104B, z.r.a);
    z.or_a(0x104C);
    if (z.jump_if(0x104D, !z.zero()))
        return begin_command_round(z);
    if (z.decrement_jump(0x1050))
        return living_candidate(z);
    return z.final_jump(0x1052);
}
template <class Z> Exit skip_actor_command(Z &z) {
    z.load_a_direct(0x1080);
    z.increment(0x1083, z.r.a);
    z.store_a_direct(0x1084);
    return z.final_jump(0x1087);
}
template <class Z> Exit check_actor_status(Z &z) {
    z.load_de(0x108A);
    z.add_to_hl(0x108D, z.de());
    z.load_from_hl(0x108E, z.r.a);
    z.or_a(0x108F);
    if (z.relative_if(0x1090, !z.zero()))
        return skip_actor_command(z);
    return z.final_call(0x1092);
}
template <class Z> Exit check_actor_eligibility(Z &z) {
    if (z.jump_if(0x1077, z.zero()))
        return skip_actor_command(z);
    z.increment_hl(0x107A);
    z.load_from_hl(0x107B, z.r.a);
    z.or_a(0x107C);
    if (z.jump_if(0x107D, !z.zero()))
        return check_actor_status(z);
    return skip_actor_command(z);
}
template <class Z> Exit open_command_cursor(Z &z) {
    z.load_hl(0x109B);
    z.store_hl_direct(0x109E);
    z.load_immediate(0x10A1, z.r.a);
    z.store_a_direct(0x10A3);
    return z.final_call(0x10A6);
}
template <class Z> Exit accept_command_cursor(Z &z) {
    z.test_bit(0x10A9, 4, z.r.c);
    if (z.jump_if(0x10AB, !z.zero()))
        return z.final_call(0x1121);
    z.load_hl(0x10AE);
    return z.final_call(0x10B1);
}
template <class Z> Exit finish_command_phase(Z &z) {
    z.load_a_direct(0x10B7);
    z.compare_immediate(0x10BA);
    if (z.jump_if(0x10BC, z.carry()))
        return z.final_call(0x1074);
    z.compare_immediate(0x10BF);
    return z.final_jump_if(0x10C1,k::Condition::nc);
}
template <class Z> Exit begin_command_execution(Z &z) {
    z.xor_a(0x10C4);
    z.store_a_direct(0x10C5);
    return z.final_call(0x10C8);
}
// Preserve the order-table position and remaining actors across either
// executor. Actor IDs below four are party members; the other eight are foes.
template <class Z> Exit dispatch_actor(Z &z) {
    z.push(0x10D6, z.r.b, z.r.c);
    z.push(0x10D7, z.r.h, z.r.l);
    z.load_from_hl(0x10D8, z.r.a);
    z.compare_immediate(0x10D9);
    if (z.jump_if(0x10DB, !z.carry()))
        return z.final_call(0x10E4);
    return z.final_call(0x10DE);
}
template <class Z> Exit begin_actor_phase(Z &z) {
    z.load_hl(0x10D1);
    z.load_immediate(0x10D4, z.r.b);
    return dispatch_actor(z);
}
template <class Z> Exit finish_actor(Z &z) {
    z.pop(0x1111, z.r.h, z.r.l);
    z.pop(0x1112, z.r.b, z.r.c);
    z.increment_hl(0x1113);
    z.load_a_direct(0x1114);
    z.compare_immediate(0x1117);
    return z.final_jump_if(0x1119,k::Condition::z);
}
template <class Z> Exit next_actor(Z &z) {
    if (z.decrement_jump(0x111C))
        return dispatch_actor(z);
    return z.final_jump(0x111E);
}
template <class Z> Exit check_surviving_party(Z &z) {
    z.load_hl(0x10FC);
    z.load_de(0x10FF);
    z.load_immediate(0x1102, z.r.b);
    do {
        z.load_from_hl(0x1104, z.r.a);
        z.or_a(0x1105);
        if (z.jump_if(0x1106, !z.zero()))
            return finish_actor(z);
        z.add_to_hl(0x1109, z.de());
    } while (z.decrement_jump(0x110A));
    z.pop(0x110C, z.r.h, z.r.l);
    z.pop(0x110D, z.r.b, z.r.c);
    return z.final_jump(0x110E);
}
template <class Z> Exit check_battle_outcome(Z &z) {
    z.load_hl(0x10E7);
    z.load_de(0x10EA);
    z.load_immediate(0x10ED, z.r.b);
    do {
        z.load_from_hl(0x10EF, z.r.a);
        z.or_a(0x10F0);
        if (z.jump_if(0x10F1, !z.zero()))
            return check_surviving_party(z);
        z.add_to_hl(0x10F4, z.de());
    } while (z.decrement_jump(0x10F5));
    z.pop(0x10F7, z.r.h, z.r.l);
    z.pop(0x10F8, z.r.b, z.r.c);
    return z.final_jump(0x10F9);
}
// Backtracking is bounded by the actor index. Actor zero always returns to
// selection; earlier inactive, zero-HP and blocked members are skipped.
template <class Z> Exit previous_actor(Z &z) {
    z.load_a_direct(0x1124);
    z.or_a(0x1127);
    if (!z.relative_if(0x1128, z.zero()))
        z.decrement(0x112A, z.r.a);
    z.store_a_direct(0x112B);
    if (z.jump_if(0x112E, z.zero()))
        return z.final_call(0x1074);
    return z.final_call(0x1131);
}
template <class Z> Exit accept_previous_actor(Z &z) {
    if (z.jump_if(0x1134, z.zero()))
        return previous_actor(z);
    z.increment_hl(0x1137);
    z.load_from_hl(0x1138, z.r.a);
    z.or_a(0x1139);
    if (z.jump_if(0x113A, z.zero()))
        return previous_actor(z);
    z.load_de(0x113D);
    z.add_to_hl(0x1140, z.de());
    z.load_from_hl(0x1141, z.r.a);
    z.or_a(0x1142);
    if (z.relative_if(0x1143, !z.zero()))
        return previous_actor(z);
    return z.final_jump(0x1145);
}
template <class Z> Exit begin_shuffle(Z &z) {
    z.load_hl(0x18F2);
    z.load_immediate(0x18F5, z.r.b);
    return z.final_call(0x18F7);
}
// Twelve successive swaps use rejection sampling over the same twelve slots.
// This is deliberately the cartridge algorithm, not a library permutation.
template <class Z> Exit swap_next_actor(Z &z) {
    z.and_immediate(0x18FA);
    z.compare_immediate(0x18FC);
    if (z.relative_if(0x18FE, !z.carry()))
        return z.final_call(0x18F7);
    z.add_immediate(0x1900);
    z.copy(0x1902, z.r.e, z.r.a);
    z.load_immediate(0x1903, z.r.a);
    z.add_with_carry_immediate(0x1905);
    z.copy(0x1907, z.r.d, z.r.a);
    z.load_from_hl(0x1908, z.r.c);
    z.load_a_from_pair(0x1909, z.de());
    z.exchange_de_hl(0x190A);
    z.store_register_to_hl(0x190B, z.r.c);
    z.store_a_to_pair(0x190C, z.de());
    z.exchange_de_hl(0x190D);
    z.increment_hl(0x190E);
    if (z.decrement_jump(0x190F))
        return z.final_call(0x18F7);
    return z.final_return(0x1911);
}
} // namespace
void register_battle_turn(RoutineRegistry &registry) {
    using k::conditional_at;
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };
    // Instructions that ran between the pieces below: each is a segment of its
    // own. Calls need room for the return address.
    auto single = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                      k::Permissions permissions, auto logic) {
        auto spec = segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions);
        spec.single_instruction = true;
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)}, std::move(spec), logic);
    };
    auto call = [&](unsigned entry, unsigned unit) {
        single(entry, unit, {core_final_at(entry)}, {.scratch_stack = 2},
               [entry](auto &z) { return z.final_call(entry); });
    };
    // The search for a living party member restarts from the full party.
    add(0x1043, 0x100F, {core_final_at(0x1047)}, {}, [](auto &z) {
        z.load_immediate(0x1043, z.r.b);
        return living_candidate(z);
    });
    // Calls and jumps between the command round's pieces (the party checks,
    // the command window, the actor loop and the shuffle).
    call(0x1071, 0x1055);
    call(0x1074, 0x1055);
    call(0x1095, 0x1055);
    call(0x1098, 0x1055);
    call(0x10B4, 0x1055);
    call(0x10CB, 0x1055);
    call(0x10CE, 0x1055);
    single(0x10E1, 0x10D1, {core_final_at(0x10E1)}, {}, [](auto &z) { return z.final_jump(0x10E1); });
    call(0x128C, 0x128C);
    add(0x100F, 0x100F, {core_final_at(0x1031)}, {}, [](auto &z) { return initialize_battle(z); });
    add(0x1034, 0x100F, {core_final_at(0x1038)}, {}, [](auto &z) { return find_active_party(z); });
    add(0x103B, 0x100F, {core_final_at(0x1047), core_final_at(0x1038), core_final_at(0x1040)}, {},
        [](auto &z) { return accept_active_party(z); });
    add(0x104A, 0x100F, {core_final_at(0x106E), core_final_at(0x1047), core_final_at(0x1052)}, {},
        [](auto &z) { return accept_living_party(z); });
    add(0x1055, 0x1055, {core_final_at(0x106E)}, {},
        [](auto &z) { return begin_command_round(z); });
    add(0x1077, 0x1055, {core_final_at(0x1087), core_final_at(0x1092)}, {},
        [](auto &z) { return check_actor_eligibility(z); });
    add(0x1080, 0x1055, {core_final_at(0x1087)}, {}, [](auto &z) { return skip_actor_command(z); });
    add(0x108A, 0x1055, {core_final_at(0x1087), core_final_at(0x1092)}, {},
        [](auto &z) { return check_actor_status(z); });
    add(0x109B, 0x1055, {core_final_at(0x10A6)}, {},
        [](auto &z) { return open_command_cursor(z); });
    add(0x10A9, 0x1055, {core_final_at(0x1121), core_final_at(0x10B1)}, {},
        [](auto &z) { return accept_command_cursor(z); });
    add(0x10B7, 0x1055, {core_final_at(0x1074), conditional_at(0x10C1)}, {},
        [](auto &z) { return finish_command_phase(z); });
    add(0x10C4, 0x1055, {core_final_at(0x10C8)}, {},
        [](auto &z) { return begin_command_execution(z); });
    add(0x10D1, 0x10D1, {core_final_at(0x10DE, -4), core_final_at(0x10E4, -4)},
        {.scratch_stack = 4}, [](auto &z) { return begin_actor_phase(z); });
    add(0x10D6, 0x10D1, {core_final_at(0x10DE, -4), core_final_at(0x10E4, -4)},
        {.scratch_stack = 4}, [](auto &z) { return dispatch_actor(z); });
    add(0x10E7, 0x10D1,
        {core_final_at(0x10F9, 4), core_final_at(0x110E, 4), conditional_at(0x1119, 4)},
        {.inherited_stack = 4}, [](auto &z) { return check_battle_outcome(z); });
    add(0x10FC, 0x10D1, {core_final_at(0x110E, 4), conditional_at(0x1119, 4)},
        {.inherited_stack = 4}, [](auto &z) { return check_surviving_party(z); });
    add(0x1111, 0x10D1, {conditional_at(0x1119, 4)}, {.inherited_stack = 4},
        [](auto &z) { return finish_actor(z); });
    add(0x111C, 0x10D1,
        {core_final_at(0x10DE, -4), core_final_at(0x10E4, -4), core_final_at(0x111E)},
        {.scratch_stack = 4}, [](auto &z) { return next_actor(z); });
    add(0x1124, 0x1124, {core_final_at(0x1074), core_final_at(0x1131)}, {},
        [](auto &z) { return previous_actor(z); });
    add(0x1134, 0x1124, {core_final_at(0x1074), core_final_at(0x1131), core_final_at(0x1145)}, {},
        [](auto &z) { return accept_previous_actor(z); });
    add(0x18F2, 0x18F2, {core_final_at(0x18F7)}, {}, [](auto &z) { return begin_shuffle(z); });
    add(0x18FA, 0x18F2, {core_final_at(0x18F7), returned_at(0x1911)}, {},
        [](auto &z) { return swap_next_actor(z); });
}
} // namespace psmodern::game
