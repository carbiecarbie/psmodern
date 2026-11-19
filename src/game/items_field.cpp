// SPDX-License-Identifier: GPL-3.0-or-later
// Item-use dispatch, vehicles, light, escape and transfer wrappers.
#include "game/joins.h"
#include "game/items_field.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;
template <class Z> Exit announce_ineffective_item(Z &);
template <class Z> Exit report_ineffective_item(Z &);
template <class Z> Exit announce_wand(Z &);
template <class Z> Exit check_wand_battle(Z &);
template <class Z> Exit refuse_field_wand(Z &);
template <class Z> Exit announce_rover(Z &);
template <class Z> Exit find_ground_vehicle_spot(Z &);
template <class Z> Exit prepare_rover(Z &);
template <class Z> Exit enter_ground_vehicle(Z &);
template <class Z> Exit announce_hovercraft(Z &);
template <class Z> Exit find_hovercraft_spot(Z &);
template <class Z> Exit enter_hovercraft(Z &);
template <class Z> Exit announce_ice_digger(Z &);
template <class Z> Exit check_ice_digger_world(Z &);
template <class Z> Exit refuse_ice_digger_world(Z &);
template <class Z> Exit announce_flute(Z &);
template <class Z> Exit play_flute(Z &);
template <class Z> Exit check_flute_dungeon(Z &);
template <class Z> Exit report_battle_flute(Z &);
template <class Z> Exit finish_flute(Z &);
template <class Z> Exit announce_flash_refusal(Z &);
template <class Z> Exit check_existing_flash(Z &);
template <class Z> Exit check_flash_dungeon(Z &);
template <class Z> Exit check_flash_battle(Z &);
template <class Z> Exit report_battle_flash_refusal(Z &);
template <class Z> Exit report_flash_refusal(Z &);
template <class Z> Exit activate_dungeon_flash(Z &);
template <class Z> Exit consume_battle_escaper(Z &);
template <class Z> Exit announce_transfer_item(Z &);
template <class Z> Exit consume_field_transfer(Z &);
template <class Z> Exit check_transfer_battle(Z &);
template <class Z> Exit accept_field_transfer(Z &);
template <class Z> Exit refuse_dungeon_transfer(Z &);
template <class Z> Exit announce_magic_hat(Z &);
template <class Z> Exit check_hat_battle(Z &);
template <class Z> Exit refuse_field_magic_hat(Z &);
template <class Z> Exit announce_ineffective_item(Z &z) {
    z.load_hl(0x228A);
    return z.final_call(0x228D);
}
template <class Z> Exit report_ineffective_item(Z &z) {
    z.load_hl(0x2290);
    return z.final_call(0x2293);
}
template <class Z> Exit announce_wand(Z &z) {
    z.load_hl(0x2299);
    return z.final_call(0x229C);
}
template <class Z> Exit check_wand_battle(Z &z) {
    z.load_a_direct(0x229F);
    z.or_a(0x22A2);
    return z.final_jump_if(0x22A3,k::Condition::nz);
}
template <class Z> Exit refuse_field_wand(Z &z) {
    z.load_hl(0x22A6);
    return z.final_call(0x22A9);
}
template <class Z> Exit announce_rover(Z &z) {
    z.load_hl(0x22AF);
    return z.final_call(0x22B2);
}
template <class Z> Exit find_ground_vehicle_spot(Z &z) {
    z.load_a_direct(0x22B7);
    z.compare_immediate(0x22BA);
    z.load_hl(0x22BC);
    if (z.relative_if(0x22BF, !z.carry()))
        return z.final_call(0x22DF);
    z.load_a_direct(0x22C1);
    z.or_a(0x22C4);
    if (z.relative_if(0x22C5, z.zero()))
        return z.final_call(0x22DF);
    z.push(0x22C7, z.r.b, z.r.c);
    z.push(0x22C8, z.r.d, z.r.e);
    return z.final_call(0x22C9);
}
template <class Z> Exit prepare_rover(Z &z) {
    z.load_immediate(0x22B5, z.r.e);
    return find_ground_vehicle_spot(z);
}
template <class Z> Exit enter_ground_vehicle(Z &z) {
    z.pop(0x22CC, z.r.d, z.r.e);
    z.pop(0x22CD, z.r.b, z.r.c);
    z.load_hl(0x22CE);
    if (!z.relative_if(0x22D1, !z.zero())) {
        z.copy(0x22D3, z.r.a, z.r.e);
        z.store_a_direct(0x22D4);
        z.load_immediate(0x22D7, z.r.a);
        z.store_a_direct(0x22D9);
        z.load_hl(0x22DC);
    }
    return z.final_call(0x22DF);
}
template <class Z> Exit announce_hovercraft(Z &z) {
    z.load_hl(0x22E5);
    return z.final_call(0x22E8);
}
template <class Z> Exit find_hovercraft_spot(Z &z) {
    z.load_a_direct(0x22EB);
    z.compare_immediate(0x22EE);
    z.load_hl(0x22F0);
    if (z.relative_if(0x22F3, !z.carry()))
        return z.final_call(0x2314);
    z.load_a_direct(0x22F5);
    z.or_a(0x22F8);
    if (z.relative_if(0x22F9, z.zero()))
        return z.final_call(0x2314);
    z.push(0x22FB, z.r.b, z.r.c);
    z.push(0x22FC, z.r.d, z.r.e);
    return z.final_call(0x22FD);
}
template <class Z> Exit enter_hovercraft(Z &z) {
    z.pop(0x2300, z.r.d, z.r.e);
    z.pop(0x2301, z.r.b, z.r.c);
    z.load_hl(0x2302);
    if (!z.relative_if(0x2305, !z.zero())) {
        z.load_immediate(0x2307, z.r.a);
        z.store_a_direct(0x2309);
        z.load_immediate(0x230C, z.r.a);
        z.store_a_direct(0x230E);
        z.load_hl(0x2311);
    }
    return z.final_call(0x2314);
}
template <class Z> Exit announce_ice_digger(Z &z) {
    z.load_hl(0x231A);
    return z.final_call(0x231D);
}
template <class Z> Exit check_ice_digger_world(Z &z) {
    z.load_a_direct(0x2320);
    z.compare_immediate(0x2323);
    z.load_immediate(0x2325, z.r.e);
    return z.final_jump_if(0x2327,k::Condition::z);
}
template <class Z> Exit refuse_ice_digger_world(Z &z) {
    z.load_hl(0x232A);
    return z.final_call(0x232D);
}
template <class Z> Exit announce_flute(Z &z) {
    z.load_hl(0x2369);
    return z.final_call(0x236C);
}
template <class Z> Exit play_flute(Z &z) {
    z.load_immediate(0x236F, z.r.a);
    z.store_a_direct(0x2371);
    z.load_a_direct(0x2374);
    z.or_a(0x2377);
    if (z.relative_if(0x2378, !z.zero())) {
        z.load_hl(0x238F);
        return z.final_call(0x2392);
    }
    z.load_hl(0x237A);
    return z.final_call(0x237D);
}
template <class Z> Exit check_flute_dungeon(Z &z) {
    z.load_immediate(0x2380, z.r.a);
    z.store_a_direct(0x2382);
    z.load_a_direct(0x2385);
    z.or_a(0x2388);
    return z.final_jump_if(0x2389,k::Condition::z);
}
template <class Z> Exit report_battle_flute(Z &z) {
    z.load_hl(0x238F);
    return z.final_call(0x2392);
}
template <class Z> Exit finish_flute(Z &z) {
    z.load_immediate(0x2395, z.r.a);
    z.store_a_direct(0x2397);
    return z.final_jump(0x239A);
}
template <class Z> Exit announce_flash_refusal(Z &z) {
    z.load_hl(0x23B8);
    return z.final_call(0x23BB);
}
template <class Z> Exit check_existing_flash(Z &z) {
    z.load_a_direct(0x23C7);
    z.or_a(0x23CA);
    if (z.relative_if(0x23CB, !z.zero()))
        return announce_flash_refusal(z);
    z.load_hl(0x23CD);
    return z.final_call(0x23D0);
}
template <class Z> Exit check_flash_dungeon(Z &z) {
    z.load_a_direct(0x23B2);
    z.or_a(0x23B5);
    if (z.relative_if(0x23B6, z.zero()))
        return check_existing_flash(z);
    return announce_flash_refusal(z);
}
template <class Z> Exit check_flash_battle(Z &z) {
    z.load_a_direct(0x239D);
    z.or_a(0x23A0);
    if (z.relative_if(0x23A1, z.zero()))
        return check_flash_dungeon(z);
    z.load_hl(0x23A3);
    return z.final_call(0x23A6);
}
template <class Z> Exit report_battle_flash_refusal(Z &z) {
    z.load_hl(0x23A9);
    return z.final_call(0x23AC);
}
template <class Z> Exit report_flash_refusal(Z &z) {
    z.load_hl(0x23BE);
    return z.final_call(0x23C1);
}
template <class Z> Exit activate_dungeon_flash(Z &z) {
    z.load_immediate(0x23D9, z.r.a);
    z.store_a_direct(0x23DB);
    z.store_a_direct(0x23DE);
    return z.final_return(0x23E1);
}
template <class Z> Exit consume_battle_escaper(Z &z) {
    z.load_a_direct(0x23E2);
    z.or_a(0x23E5);
    return z.final_call_if(0x23E6,k::Condition::nz);
}
template <class Z> Exit announce_transfer_item(Z &z) {
    z.load_hl(0x23EC);
    return z.final_call(0x23EF);
}
template <class Z> Exit consume_field_transfer(Z &z) {
    z.load_a_direct(0x2401);
    z.or_a(0x2404);
    z.push(0x2405, z.r.a, z.r.f);
    return z.final_call_if(0x2406,k::Condition::nz);
}
template <class Z> Exit check_transfer_battle(Z &z) {
    z.load_a_direct(0x23F2);
    z.or_a(0x23F5);
    if (z.relative_if(0x23F6, z.zero()))
        return consume_field_transfer(z);
    z.load_hl(0x23F8);
    return z.final_call(0x23FB);
}
template <class Z> Exit accept_field_transfer(Z &z) {
    z.pop(0x2409, z.r.a, z.r.f);
    return z.final_jump_if(0x240A,k::Condition::nz);
}
template <class Z> Exit refuse_dungeon_transfer(Z &z) {
    z.load_hl(0x240D);
    return z.final_call(0x2410);
}
template <class Z> Exit announce_magic_hat(Z &z) {
    z.load_hl(0x2419);
    return z.final_call(0x241C);
}
template <class Z> Exit check_hat_battle(Z &z) {
    z.load_a_direct(0x241F);
    z.or_a(0x2422);
    return z.final_jump_if(0x2423,k::Condition::nz);
}
template <class Z> Exit refuse_field_magic_hat(Z &z) {
    z.load_hl(0x2426);
    return z.final_call(0x2429);
}
} // namespace
void register_items_field(RoutineRegistry &registry) {
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
    // The rover and the hovercraft end by closing the message window with a
    // jump; the flash does it with a call and then makes one more call.
    single(0x22E2, 0x22AF, {core_final_at(0x22E2)}, {}, [](auto &z) { return z.final_jump(0x22E2); });
    single(0x2317, 0x22E5, {core_final_at(0x2317)}, {}, [](auto &z) { return z.final_jump(0x2317); });
    single(0x23D3, 0x239D, {core_final_at(0x23D3)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x23D3); });
    single(0x23D6, 0x239D, {core_final_at(0x23D6)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x23D6); });
    add(0x228A, 0x228A, {core_final_at(0x228D, 0)}, {},
        [](auto &z) { return announce_ineffective_item(z); });
    add(0x2290, 0x228A, {core_final_at(0x2293, 0)}, {},
        [](auto &z) { return report_ineffective_item(z); });
    add(0x2299, 0x2299, {core_final_at(0x229C, 0)}, {}, [](auto &z) { return announce_wand(z); });
    add(0x229F, 0x2299, {conditional_at(0x22A3, 0)}, {},
        [](auto &z) { return check_wand_battle(z); });
    add(0x22A6, 0x2299, {core_final_at(0x22A9, 0)}, {},
        [](auto &z) { return refuse_field_wand(z); });
    add(0x22AF, 0x22AF, {core_final_at(0x22B2, 0)}, {}, [](auto &z) { return announce_rover(z); });
    add(0x22B5, 0x22AF, {core_final_at(0x22DF, 0), core_final_at(0x22C9, -4)}, {.scratch_stack = 4},
        [](auto &z) { return prepare_rover(z); });
    add(0x22B7, 0x22AF, {core_final_at(0x22DF, 0), core_final_at(0x22C9, -4)}, {.scratch_stack = 4},
        [](auto &z) { return find_ground_vehicle_spot(z); });
    add(0x22CC, 0x22AF, {core_final_at(0x22DF, 4)}, {.inherited_stack = 4},
        [](auto &z) { return enter_ground_vehicle(z); });
    add(0x22E5, 0x22E5, {core_final_at(0x22E8, 0)}, {},
        [](auto &z) { return announce_hovercraft(z); });
    add(0x22EB, 0x22E5, {core_final_at(0x2314, 0), core_final_at(0x22FD, -4)}, {.scratch_stack = 4},
        [](auto &z) { return find_hovercraft_spot(z); });
    add(0x2300, 0x22E5, {core_final_at(0x2314, 4)}, {.inherited_stack = 4},
        [](auto &z) { return enter_hovercraft(z); });
    add(0x231A, 0x231A, {core_final_at(0x231D, 0)}, {},
        [](auto &z) { return announce_ice_digger(z); });
    add(0x2320, 0x231A, {conditional_at(0x2327, 0)}, {},
        [](auto &z) { return check_ice_digger_world(z); });
    add(0x232A, 0x231A, {core_final_at(0x232D, 0)}, {},
        [](auto &z) { return refuse_ice_digger_world(z); });
    add(0x2369, 0x2369, {core_final_at(0x236C, 0)}, {}, [](auto &z) { return announce_flute(z); });
    add(0x236F, 0x2369, {core_final_at(0x2392, 0), core_final_at(0x237D, 0)}, {},
        [](auto &z) { return play_flute(z); });
    add(0x2380, 0x2369, {conditional_at(0x2389, 0)}, {},
        [](auto &z) { return check_flute_dungeon(z); });
    add(0x238F, 0x2369, {core_final_at(0x2392, 0)}, {},
        [](auto &z) { return report_battle_flute(z); });
    add(0x2395, 0x2369, {core_final_at(0x239A, 0)}, {}, [](auto &z) { return finish_flute(z); });
    add(0x239D, 0x239D,
        {core_final_at(0x23BB, 0), core_final_at(0x23D0, 0), core_final_at(0x23A6, 0)}, {},
        [](auto &z) { return check_flash_battle(z); });
    add(0x23A9, 0x239D, {core_final_at(0x23AC, 0)}, {},
        [](auto &z) { return report_battle_flash_refusal(z); });
    add(0x23B2, 0x239D, {core_final_at(0x23BB, 0), core_final_at(0x23D0, 0)}, {},
        [](auto &z) { return check_flash_dungeon(z); });
    add(0x23B8, 0x239D, {core_final_at(0x23BB, 0)}, {},
        [](auto &z) { return announce_flash_refusal(z); });
    add(0x23BE, 0x239D, {core_final_at(0x23C1, 0)}, {},
        [](auto &z) { return report_flash_refusal(z); });
    add(0x23C7, 0x239D, {core_final_at(0x23BB, 0), core_final_at(0x23D0, 0)}, {},
        [](auto &z) { return check_existing_flash(z); });
    add(0x23D9, 0x239D, {returned_at(0x23E1)}, {},
        [](auto &z) { return activate_dungeon_flash(z); });
    add(0x23E2, 0x23E2, {core_final_at(0x23E6, 0)}, {},
        [](auto &z) { return consume_battle_escaper(z); });
    add(0x23EC, 0x23EC, {core_final_at(0x23EF, 0)}, {},
        [](auto &z) { return announce_transfer_item(z); });
    add(0x23F2, 0x23EC, {core_final_at(0x2406, -2), core_final_at(0x23FB, 0)}, {.scratch_stack = 2},
        [](auto &z) { return check_transfer_battle(z); });
    add(0x2401, 0x23EC, {core_final_at(0x2406, -2)}, {.scratch_stack = 2},
        [](auto &z) { return consume_field_transfer(z); });
    add(0x2409, 0x23EC, {conditional_at(0x240A, 2)}, {.inherited_stack = 2},
        [](auto &z) { return accept_field_transfer(z); });
    add(0x240D, 0x23EC, {core_final_at(0x2410, 0)}, {},
        [](auto &z) { return refuse_dungeon_transfer(z); });
    add(0x2419, 0x2416, {core_final_at(0x241C, 0)}, {},
        [](auto &z) { return announce_magic_hat(z); });
    add(0x241F, 0x2416, {conditional_at(0x2423, 0)}, {},
        [](auto &z) { return check_hat_battle(z); });
    add(0x2426, 0x2416, {core_final_at(0x2429, 0)}, {},
        [](auto &z) { return refuse_field_magic_hat(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_items_field_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x2296},{0,0x228A},{kit::core_final_at(0x2296)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x2296); });
    kit::add_joining(registry,{0,0x22AC},{0,0x2299},{kit::core_final_at(0x22AC)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x22AC); });
    kit::add_joining(registry,{0,0x2330},{0,0x231A},{kit::core_final_at(0x2330)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x2330); });
    kit::add_joining(registry,{0,0x238C},{0,0x2369},{kit::core_final_at(0x238C)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x238C); });
    kit::add_joining(registry,{0,0x23AF},{0,0x239D},{kit::core_final_at(0x23AF)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x23AF); });
    kit::add_joining(registry,{0,0x23C4},{0,0x239D},{kit::core_final_at(0x23C4)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x23C4); });
    kit::add_joining(registry,{0,0x23E9},{0,0x23E2},{kit::core_final_at(0x23E9)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x23E9); });
    kit::add_joining(registry,{0,0x23FE},{0,0x23EC},{kit::core_final_at(0x23FE)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x23FE); });
    kit::add_joining(registry,{0,0x2413},{0,0x23EC},{kit::core_final_at(0x2413)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x2413); });
    kit::add_joining(registry,{0,0x2416},{0,0x2416},{kit::core_final_at(0x2416)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x2416); });
    kit::add_joining(registry,{0,0x242C},{0,0x2416},{kit::core_final_at(0x242C)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x242C); });
}
} // namespace psmodern::game
