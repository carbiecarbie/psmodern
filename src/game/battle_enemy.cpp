// SPDX-License-Identifier: GPL-3.0-or-later
// Enemy action selection, protection consumption and ordinary attack aftermath.
#include "game/joins.h"
#include "game/battle_enemy.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;
template <class Z> Exit choose_enemy_action(Z &z) {
    z.load_a_direct(0x12B9);
    z.and_immediate(0x12BC);
    z.load_hl(0x12BE);
    return z.final_jump(0x12C1);
}
template <class Z> Exit check_blocked_enemy(Z &z) {
    z.push(0x1290, z.r.h, z.r.l);
    z.pop_index(0x1291, k::Index::iy);
    z.load_from_indexed(0x1293, k::Index::iy, z.r.a);
    z.or_a(0x1296);
    if (z.relative_if(0x1297, z.zero()))
        return choose_enemy_action(z);
    return z.final_call(0x1299);
}
template <class Z> Exit reduce_enemy_block(Z &z) {
    z.and_immediate(0x129C);
    z.increment(0x129E, z.r.a);
    z.copy(0x129F, z.r.b, z.r.a);
    z.load_from_indexed(0x12A0, k::Index::iy, z.r.a);
    z.subtract_register(0x12A3, z.r.b);
    if (!z.relative_if(0x12A4, !z.carry()))
        z.xor_a(0x12A6);
    z.store_to_indexed(0x12A7, k::Index::iy, z.r.a);
    z.or_a(0x12AA);
    z.load_hl(0x12AB);
    if (!z.relative_if(0x12AE, z.zero()))
        z.load_hl(0x12B0);
    return z.final_call(0x12B3);
}
template <class Z> Exit expire_protection(Z &z) {
    z.xor_a(0x12F8);
    z.store_a_direct(0x12F9);
    z.load_hl(0x12FC);
    return z.final_call(0x12FF);
}
template <class Z> Exit check_protection_consumption(Z &z) {
    z.load_a_direct(0x12D4);
    z.and_immediate(0x12D7);
    if (z.relative_if(0x12D9, z.zero()))
        return expire_protection(z);
    return z.final_call(0x12DB);
}
template <class Z> Exit consume_protection(Z &z) {
    z.and_immediate(0x12DE);
    z.copy(0x12E0, z.r.c, z.r.a);
    z.load_a_direct(0x12E1);
    z.copy(0x12E4, z.r.b, z.r.a);
    z.and_immediate(0x12E5);
    z.subtract_register(0x12E7, z.r.c);
    if (!z.relative_if(0x12E8, !z.carry()))
        z.xor_a(0x12EA);
    z.or_a(0x12EB);
    if (z.relative_if(0x12EC, z.zero()))
        return expire_protection(z);
    z.test_bit(0x12EE, 7, z.r.b);
    if (!z.relative_if(0x12F0, z.zero()))
        z.or_immediate(0x12F2);
    z.store_a_direct(0x12F4);
    return z.final_return(0x12F7);
}
template <class Z> Exit begin_physical_enemy_action(Z &z) {
    z.load_a_direct(0x1305);
    z.or_a(0x1308);
    return z.final_call_if(0x1309,k::Condition::nz);
}
template <class Z> Exit draw_party_target(Z &z) {
    z.and_immediate(0x130F);
    return z.final_call(0x1311);
}
template <class Z> Exit prepare_party_target(Z &z) {
    z.store_a_direct(0x1317);
    z.push(0x131A, z.r.h, z.r.l);
    z.pop_index(0x131B, k::Index::ix);
    z.push(0x131D, z.r.a, z.r.f);
    z.store_a_direct(0x131E);
    return z.final_call(0x1321);
}
template <class Z> Exit animate_enemy_hit(Z &z) {
    z.load_a_direct(0x1327);
    z.or_a(0x132A);
    z.push(0x132B, z.r.a, z.r.f);
    return z.final_call(0x132C);
}
template <class Z> Exit restore_target_actor(Z &z) {
    z.pop(0x1344, z.r.a, z.r.f);
    return z.final_call(0x1345);
}
template <class Z> Exit report_enemy_hit(Z &z) {
    z.pop(0x132F, z.r.a, z.r.f);
    if (z.relative_if(0x1330, !z.zero()))
        return restore_target_actor(z);
    z.load_a_direct(0x1332);
    z.or_a(0x1335);
    z.load_hl(0x1336);
    if (!z.relative_if(0x1339, !z.zero()))
        z.load_hl(0x133B);
    return z.final_call(0x133E);
}
template <class Z> Exit begin_enemy_action_wait(Z &z) {
    z.load_immediate(0x1379, z.r.b);
    z.load_immediate(0x137B, z.r.a);
    return z.final_call(0x137D);
}
template <class Z> Exit report_party_death(Z &z) {
    if (z.relative_if(0x1348, !z.zero()))
        return begin_enemy_action_wait(z);
    z.load_hl(0x134A);
    return z.final_call(0x134D);
}
// When enemy 46h kills the first party member, all four members' activity
// bytes are cleared; the extra loss message follows only if any was nonzero.
template <class Z> Exit finish_party_death(Z &z) {
    z.load_a_direct(0x1350);
    z.compare_immediate(0x1353);
    if (z.relative_if(0x1355, !z.zero()))
        return z.final_call(0x1376);
    z.load_a_direct(0x1357);
    z.compare_immediate(0x135A);
    if (z.relative_if(0x135C, !z.zero()))
        return z.final_call(0x1376);
    z.load_hl(0x135E);
    z.load_de(0x1361);
    z.xor_a(0x1364);
    z.load_immediate(0x1365, z.r.b);
    do {
        z.bus().fetch(0x1367);
        z.r.a |= z.bus().read(z.hl());
        kit::logic_flags(z.r, false);
        z.bus().advance(7);
        z.store_to_hl(0x1368);
        z.add_to_hl(0x136A, z.de());
    } while (z.decrement_jump(0x136B));
    z.or_a(0x136D);
    if (z.relative_if(0x136E, z.zero()))
        return z.final_call(0x1376);
    z.load_hl(0x1370);
    return z.final_call(0x1373);
}
template <class Z> Exit continue_enemy_action_wait(Z &z) {
    if (z.decrement_jump(0x1380)) {
        z.load_immediate(0x137B, z.r.a);
        return z.final_call(0x137D);
    }
    return z.final_call(0x1382);
}
} // namespace
void register_battle_enemy(RoutineRegistry &registry) {
    using k::conditional_at;
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, 0x128C}, exits, permissions), logic);
    };
    // Instructions that ran between the pieces below: each is a segment of its
    // own. Calls need room for the return address.
    auto single = [&](unsigned entry, std::initializer_list<SegmentExit> exits, k::Permissions permissions,
                      auto logic) {
        auto spec = segment({0, 0x128C}, exits, permissions);
        spec.single_instruction = true;
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)}, std::move(spec), logic);
    };
    // A physical action draws a random party member, drawing again until it
    // is alive (130C-1314), then resolves the attack (1324).
    single(0x130C, {core_final_at(0x130C)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x130C); });
    single(0x1314, {conditional_at(0x1314)}, {}, [](auto &z) { return z.final_jump_if(0x1314, k::Condition::z); });
    single(0x1324, {core_final_at(0x1324)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x1324); });
    // The wait after the action returns to the battle loop (1385); the next
    // enemy action starts with its own random draw (1386).
    single(0x1385, {returned_at(0x1385)}, {}, [](auto &z) { return z.final_return(0x1385); });
    single(0x1386, {core_final_at(0x1386)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x1386); });
    add(0x1290, {core_final_at(0x12C1), core_final_at(0x1299)}, {.scratch_stack = 2},
        [](auto &z) { return check_blocked_enemy(z); });
    add(0x129C, {core_final_at(0x12B3)}, {}, [](auto &z) { return reduce_enemy_block(z); });
    add(0x12B9, {core_final_at(0x12C1)}, {}, [](auto &z) { return choose_enemy_action(z); });
    add(0x12D4, {core_final_at(0x12FF), core_final_at(0x12DB)}, {},
        [](auto &z) { return check_protection_consumption(z); });
    add(0x12DE, {core_final_at(0x12FF), returned_at(0x12F7)}, {},
        [](auto &z) { return consume_protection(z); });
    add(0x12F8, {core_final_at(0x12FF)}, {}, [](auto &z) { return expire_protection(z); });
    add(0x1305, {core_final_at(0x1309)}, {},
        [](auto &z) { return begin_physical_enemy_action(z); });
    add(0x130F, {core_final_at(0x1311)}, {}, [](auto &z) { return draw_party_target(z); });
    add(0x1317, {core_final_at(0x1321, -2)}, {.scratch_stack = 2},
        [](auto &z) { return prepare_party_target(z); });
    add(0x1327, {core_final_at(0x132C, -2)}, {.scratch_stack = 2, .inherited_stack = 2},
        [](auto &z) { return animate_enemy_hit(z); });
    add(0x132F, {core_final_at(0x1345, 4), core_final_at(0x133E, 2)}, {.inherited_stack = 4},
        [](auto &z) { return report_enemy_hit(z); });
    add(0x1344, {core_final_at(0x1345, 2)}, {.inherited_stack = 2},
        [](auto &z) { return restore_target_actor(z); });
    add(0x1348, {core_final_at(0x137D), core_final_at(0x134D)}, {},
        [](auto &z) { return report_party_death(z); });
    add(0x1350, {core_final_at(0x1376), core_final_at(0x1373)}, {},
        [](auto &z) { return finish_party_death(z); });
    add(0x1379, {core_final_at(0x137D)}, {}, [](auto &z) { return begin_enemy_action_wait(z); });
    add(0x1380, {core_final_at(0x137D), core_final_at(0x1382)}, {},
        [](auto &z) { return continue_enemy_action_wait(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_battle_enemy_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x12B6},{0,0x128C},{kit::core_final_at(0x12B6)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x12B6); });
    kit::add_joining(registry,{0,0x1302},{0,0x128C},{kit::core_final_at(0x1302)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1302); });
    kit::add_joining(registry,{0,0x1341},{0,0x128C},{kit::core_final_at(0x1341)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1341); });
    kit::add_joining(registry,{0,0x1376},{0,0x128C},{kit::core_final_at(0x1376)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1376); });
}
} // namespace psmodern::game
