// SPDX-License-Identifier: GPL-3.0-or-later
// Execute a queued party action, including blocking status and missing items.
#include "game/joins.h"
#include "game/battle_actor.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;
template <class Z> Exit check_blocked_actor(Z &);
template <class Z> Exit advance_actor_status(Z &);
template <class Z> Exit begin_actor(Z &z) {
    z.store_a_direct(0x1148);
    return z.final_call(0x114B);
}
template <class Z> Exit reject_cancelled_round(Z &z) {
    z.load_a_direct(0x114F);
    z.or_a(0x1152);
    if (auto exit=z.final_return_if(0x1153,k::Condition::nz)) return *exit;
    return check_blocked_actor(z);
}
template <class Z> Exit check_blocked_actor(Z &z) {
    z.push(0x1154, z.r.h, z.r.l);
    z.pop_index(0x1155, k::Index::iy);
    z.load_from_indexed(0x1157, k::Index::iy, z.r.a);
    z.or_a(0x115A);
    if (auto exit=z.final_return_if(0x115B,k::Condition::z)) return *exit;
    return advance_actor_status(z);
}
template <class Z> Exit advance_actor_status(Z &z) {
    z.load_from_indexed(0x115C, k::Index::iy, z.r.a);
    z.or_a(0x115F);
    if (z.relative_if(0x1160, z.zero()))
        return z.final_call(0x1188);
    z.load_a_direct(0x1162);
    z.store_a_direct(0x1165);
    return z.final_call(0x1168);
}
template <class Z> Exit reduce_blocking_status(Z &z) {
    z.and_immediate(0x116B);
    z.increment(0x116D, z.r.a);
    z.copy(0x116E, z.r.b, z.r.a);
    z.load_from_indexed(0x116F, k::Index::iy, z.r.a);
    z.subtract_register(0x1172, z.r.b);
    if (!z.relative_if(0x1173, !z.carry()))
        z.xor_a(0x1175);
    z.store_to_indexed(0x1176, k::Index::iy, z.r.a);
    z.or_a(0x1179);
    z.load_hl(0x117A);
    if (!z.relative_if(0x117D, z.zero()))
        z.load_hl(0x117F);
    return z.final_call(0x1182);
}
template <class Z> Exit identify_attack(Z &z) {
    z.compare_immediate(0x118E);
    return z.final_jump_if(0x1190,k::Condition::nz);
}
template <class Z> Exit group_weapon(Z &z, unsigned at) {
    z.load_immediate(at, z.r.d);
    z.relative(at + 2);
    z.copy(0x11D5, z.r.e, z.r.a);
    return z.final_call(0x11D6);
}
template <class Z> Exit choose_weapon(Z &z) {
    z.load_a_direct(0x1193);
    z.store_a_direct(0x1196);
    z.add_register(0x1199, z.r.a);
    z.add_register(0x119A, z.r.a);
    z.add_register(0x119B, z.r.a);
    z.add_register(0x119C, z.r.a);
    z.load_hl(0x119D);
    z.add_register(0x11A0, z.r.l);
    z.copy(0x11A1, z.r.l, z.r.a);
    z.add_with_carry(0x11A2, z.r.h);
    z.subtract_register(0x11A3, z.r.l);
    z.copy(0x11A4, z.r.h, z.r.a);
    z.load_from_hl(0x11A5, z.r.a);
    z.compare_immediate(0x11A6);
    if (z.relative_if(0x11A8, z.zero()))
        return group_weapon(z, 0x11CB);
    z.compare_immediate(0x11AA);
    if (z.relative_if(0x11AC, z.zero()))
        return group_weapon(z, 0x11CF);
    z.compare_immediate(0x11AE);
    if (z.relative_if(0x11B0, z.zero())) {
        z.load_immediate(0x11D3, z.r.d);
        z.copy(0x11D5, z.r.e, z.r.a);
        return z.final_call(0x11D6);
    }
    return z.final_call(0x11B2);
}
template <class Z> Exit choose_enemy(Z &z) {
    z.and_immediate(0x11B8);
    z.add_immediate(0x11BA);
    return z.final_call(0x11BC);
}
template <class Z> Exit attack_enemy(Z &z) {
    z.push(0x11C2, z.r.h, z.r.l);
    z.pop_index(0x11C3, k::Index::ix);
    return z.final_call(0x11C5);
}
template <class Z> Exit identify_magic(Z &z) {
    z.compare_immediate(0x11DC);
    return z.final_jump_if(0x11DE,k::Condition::nz);
}
template <class Z> Exit cast_queued_magic(Z &z) {
    z.copy(0x11E1, z.r.a, z.r.c);
    z.store_a_direct(0x11E2);
    z.copy(0x11E5, z.r.a, z.r.b);
    z.and_immediate(0x11E6);
    z.load_hl(0x11E8);
    return z.final_call(0x11EB);
}
template <class Z> Exit identify_item(Z &z) {
    z.compare_immediate(0x11F1);
    return z.final_jump_if(0x11F3,k::Condition::nz);
}
template <class Z> Exit find_queued_item(Z &z) {
    z.load_a_direct(0x11F6);
    z.store_a_direct(0x11F9);
    z.copy(0x11FC, z.r.a, z.r.b);
    z.store_a_direct(0x11FD);
    return z.final_call(0x1200);
}
template <class Z> Exit accept_queued_item(Z &z) {
    if (z.relative_if(0x1203, !z.zero())) {
        z.load_hl(0x1212);
        return z.final_call(0x1215);
    }
    z.store_hl_direct(0x1205);
    return z.final_call(0x1208);
}
template <class Z> Exit missing_item_message(Z &z) {
    z.load_hl(0x1212);
    return z.final_call(0x1215);
}
} // namespace
void register_battle_actor(RoutineRegistry &registry) {
    using k::conditional_at;
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, std::initializer_list<SegmentExit> exits, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, 0x1148}, exits, {.scratch_stack = 2}), logic);
    };
    // Instructions that ran between the pieces above: each is a segment of its
    // own. Calls need room for the return address.
    auto single = [&](unsigned entry, std::initializer_list<SegmentExit> exits, k::Permissions permissions,
                      auto logic) {
        auto spec = segment({0, 0x1148}, exits, permissions);
        spec.single_instruction = true;
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)}, std::move(spec), logic);
    };
    // After the first call, a blocked or finished round returns to the caller.
    single(0x114E, {returned_at(0x114E), core_final_at(0x114E)}, {}, [](auto &z) -> Exit {
        if (auto exit = z.final_return_if(0x114E, k::Condition::z))
            return *exit;
        return k::fall_through(0x114E, 0x114F);
    });
    single(0x1185, {core_final_at(0x1185)}, {}, [](auto &z) { return z.final_jump(0x1185); });
    single(0x118B, {core_final_at(0x118B)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x118B); });
    single(0x11B5, {core_final_at(0x11B5)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x11B5); });
    single(0x11BF, {conditional_at(0x11BF)}, {},
           [](auto &z) { return z.final_jump_if(0x11BF, k::Condition::z); });
    single(0x11C8, {core_final_at(0x11C8)}, {}, [](auto &z) { return z.final_jump(0x11C8); });
    single(0x11D9, {core_final_at(0x11D9)}, {}, [](auto &z) { return z.final_jump(0x11D9); });
    single(0x120B, {core_final_at(0x120B)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x120B); });
    single(0x120E, {core_final_at(0x120E)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x120E); });
    single(0x1211, {returned_at(0x1211)}, {}, [](auto &z) { return z.final_return(0x1211); });
    add(0x1148, {core_final_at(0x114B)}, [](auto &z) { return begin_actor(z); });
    add(0x114F,
        {returned_at(0x1153), returned_at(0x115B), core_final_at(0x1188), core_final_at(0x1168)},
        [](auto &z) { return reject_cancelled_round(z); });
    add(0x1154, {returned_at(0x115B), core_final_at(0x1188), core_final_at(0x1168)},
        [](auto &z) { return check_blocked_actor(z); });
    add(0x115C, {core_final_at(0x1188), core_final_at(0x1168)},
        [](auto &z) { return advance_actor_status(z); });
    add(0x116B, {core_final_at(0x1182)}, [](auto &z) { return reduce_blocking_status(z); });
    add(0x118E, {conditional_at(0x1190)}, [](auto &z) { return identify_attack(z); });
    add(0x1193, {core_final_at(0x11D6), core_final_at(0x11B2)},
        [](auto &z) { return choose_weapon(z); });
    add(0x11B8, {core_final_at(0x11BC)}, [](auto &z) { return choose_enemy(z); });
    add(0x11C2, {core_final_at(0x11C5)}, [](auto &z) { return attack_enemy(z); });
    add(0x11DC, {conditional_at(0x11DE)}, [](auto &z) { return identify_magic(z); });
    add(0x11E1, {core_final_at(0x11EB)}, [](auto &z) { return cast_queued_magic(z); });
    add(0x11F1, {conditional_at(0x11F3)}, [](auto &z) { return identify_item(z); });
    add(0x11F6, {core_final_at(0x1200)}, [](auto &z) { return find_queued_item(z); });
    add(0x1203, {core_final_at(0x1215), core_final_at(0x1208)},
        [](auto &z) { return accept_queued_item(z); });
    add(0x1212, {core_final_at(0x1215)}, [](auto &z) { return missing_item_message(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_battle_actor_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x11EE},{0,0x1148},{kit::core_final_at(0x11EE)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x11EE); });
    kit::add_joining(registry,{0,0x1218},{0,0x1148},{kit::core_final_at(0x1218)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1218); });
    kit::add_joining(registry,{0,0x121B},{0,0x1148},{kit::core_final_at(0x121B)},12,0,[](auto& z) { return z.final_relative(0x121B); });
}
} // namespace psmodern::game
