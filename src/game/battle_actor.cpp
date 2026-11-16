// SPDX-License-Identifier: GPL-3.0-or-later
// Execute a queued party action, including blocking status and missing items.
#include "game/battle_actor.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::continues;
using k::Exit;
using k::returns;
template <class Z> Exit check_blocked_actor(Z &);
template <class Z> Exit advance_actor_status(Z &);
template <class Z> Exit begin_actor(Z &z) {
    z.store_a_direct(0x1148);
    return continues(0x114B, k::call_cycles);
}
template <class Z> Exit reject_cancelled_round(Z &z) {
    z.load_a_direct(0x114F);
    z.or_a(0x1152);
    if (!z.zero())
        return returns(0x1153, k::taken_conditional_return_cycles);
    z.return_not_taken(0x1153);
    return check_blocked_actor(z);
}
template <class Z> Exit check_blocked_actor(Z &z) {
    z.push(0x1154, z.r.h, z.r.l);
    z.pop_index(0x1155, k::Index::iy);
    z.load_from_indexed(0x1157, k::Index::iy, z.r.a);
    z.or_a(0x115A);
    if (z.zero())
        return returns(0x115B, k::taken_conditional_return_cycles);
    z.return_not_taken(0x115B);
    return advance_actor_status(z);
}
template <class Z> Exit advance_actor_status(Z &z) {
    z.load_from_indexed(0x115C, k::Index::iy, z.r.a);
    z.or_a(0x115F);
    if (z.relative_if(0x1160, z.zero()))
        return continues(0x1188, k::call_cycles);
    z.load_a_direct(0x1162);
    z.store_a_direct(0x1165);
    return continues(0x1168, k::call_cycles);
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
    return continues(0x1182, k::call_cycles);
}
template <class Z> Exit identify_attack(Z &z) {
    z.compare_immediate(0x118E);
    return continues(0x1190, k::jump_cycles);
}
template <class Z> Exit group_weapon(Z &z, unsigned at) {
    z.load_immediate(at, z.r.d);
    z.relative(at + 2);
    z.copy(0x11D5, z.r.e, z.r.a);
    return continues(0x11D6, k::call_cycles);
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
        return continues(0x11D6, k::call_cycles);
    }
    return continues(0x11B2, k::call_cycles);
}
template <class Z> Exit choose_enemy(Z &z) {
    z.and_immediate(0x11B8);
    z.add_immediate(0x11BA);
    return continues(0x11BC, k::call_cycles);
}
template <class Z> Exit attack_enemy(Z &z) {
    z.push(0x11C2, z.r.h, z.r.l);
    z.pop_index(0x11C3, k::Index::ix);
    return continues(0x11C5, k::call_cycles);
}
template <class Z> Exit identify_magic(Z &z) {
    z.compare_immediate(0x11DC);
    return continues(0x11DE, k::jump_cycles);
}
template <class Z> Exit cast_queued_magic(Z &z) {
    z.copy(0x11E1, z.r.a, z.r.c);
    z.store_a_direct(0x11E2);
    z.copy(0x11E5, z.r.a, z.r.b);
    z.and_immediate(0x11E6);
    z.load_hl(0x11E8);
    return continues(0x11EB, k::call_cycles);
}
template <class Z> Exit identify_item(Z &z) {
    z.compare_immediate(0x11F1);
    return continues(0x11F3, k::jump_cycles);
}
template <class Z> Exit find_queued_item(Z &z) {
    z.load_a_direct(0x11F6);
    z.store_a_direct(0x11F9);
    z.copy(0x11FC, z.r.a, z.r.b);
    z.store_a_direct(0x11FD);
    return continues(0x1200, k::call_cycles);
}
template <class Z> Exit accept_queued_item(Z &z) {
    if (z.relative_if(0x1203, !z.zero())) {
        z.load_hl(0x1212);
        return continues(0x1215, k::call_cycles);
    }
    z.store_hl_direct(0x1205);
    return continues(0x1208, k::call_cycles);
}
template <class Z> Exit missing_item_message(Z &z) {
    z.load_hl(0x1212);
    return continues(0x1215, k::call_cycles);
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
} // namespace psmodern::game
