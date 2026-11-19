// SPDX-License-Identifier: GPL-3.0-or-later
// Physical attack variance, minimum damage, HP/death application and the
// waits around battle effects. The frame wait and object engine keep their
// existing entries; no rendering or registry semantics change here.
#include "game/battle_damage.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;

template <class Z> Exit choose_attack_strength(Z &z) {
    z.load_from_indexed(0x121D, k::Index::iy, z.r.a);
    z.test_bit_indexed(0x1220, k::Index::iy, 7);
    if (!z.relative_if(0x1224, z.zero())) {
        z.copy(0x1226, z.r.c, z.r.a);
        z.rotate_right_circular_a(0x1227);
        z.and_immediate(0x1228);
        z.add_register(0x122A, z.r.c);
        if (!z.relative_if(0x122B, !z.carry()))
            z.load_immediate(0x122D, z.r.a);
    }
    return z.final_call(0x122F);
}
template <class Z> Exit choose_defense_strength(Z &z) {
    z.copy(0x1232, z.r.c, z.r.a);
    z.load_from_indexed(0x1233, k::Index::ix, z.r.a);
    return z.final_call(0x1236);
}
template <class Z> Exit announce_damage(Z &z) {
    z.push(0x125E, z.r.a, z.r.f);
    z.load_immediate(0x125F, z.r.a);
    z.store_a_direct(0x1261);
    return z.final_call(0x1264);
}
// Negative defense-minus-attack is direct damage. Otherwise the cartridge
// may use its level-bounded minimum-damage draw or report a miss.
template <class Z> Exit resolve_attack_strength(Z &z) {
    z.subtract_register(0x1239, z.r.c);
    if (z.relative_if(0x123A, z.carry()))
        return announce_damage(z);
    z.compare_immediate(0x123C);
    if (z.relative_if(0x123E, z.carry()))
        return z.final_call(0x1251);
    z.rotate_right_circular_a(0x1240);
    if (z.relative_if(0x1241, z.carry()))
        return z.final_call(0x1251);
    z.load_immediate(0x1243, z.r.a);
    z.store_a_direct(0x1245);
    z.load_hl(0x1248);
    return z.final_call(0x124B);
}
template <class Z> Exit accept_minimum_damage(Z &z) {
    z.and_immediate(0x1254);
    z.compare_indexed(0x1256, k::Index::iy);
    if (!z.relative_if(0x1259, z.zero())) {
        if (z.relative_if(0x125B, !z.carry()))
            return z.final_call(0x1251);
    }
    z.complement_a(0x125D);
    return announce_damage(z);
}
// The amount is a negative byte. Carry means HP survived the subtraction;
// otherwise clamp to zero. Death clears activity flags and blocking status.
template <class Z> Exit apply_damage(Z &z) {
    z.pop(0x1267, z.r.a, z.r.f);
    z.add_from_indexed(0x1268, k::Index::ix);
    if (!z.relative_if(0x126B, z.carry()))
        z.xor_a(0x126D);
    z.store_to_indexed(0x126E, k::Index::ix, z.r.a);
    if (auto exit=z.final_return_if(0x1271,k::Condition::nz)) return *exit;
    z.store_to_indexed(0x1272, k::Index::ix, z.r.a);
    z.store_to_indexed(0x1275, k::Index::ix, z.r.a);
    return z.final_return(0x1278);
}
// Split a stat into a half and a quarter before the original RNG call. Its
// product helper supplies the high byte of draw*quarter for the final sum.
template <class Z> Exit begin_stat_variation(Z &z) {
    z.rotate_right_circular_a(0x1279);
    z.and_immediate(0x127A);
    z.copy(0x127C, z.r.b, z.r.a);
    z.rotate_right_circular_a(0x127D);
    z.and_immediate(0x127E);
    z.copy(0x1280, z.r.e, z.r.a);
    return z.final_call(0x1281);
}
template <class Z> Exit multiply_stat_variation(Z &z) {
    z.copy(0x1284, z.r.h, z.r.a);
    return z.final_call(0x1285);
}
template <class Z> Exit finish_stat_variation(Z &z) {
    z.copy(0x1288, z.r.a, z.r.e);
    z.add_register(0x1289, z.r.b);
    z.add_register(0x128A, z.r.h);
    return z.final_return(0x128B);
}
template <class Z> Exit begin_effect_animation(Z &z) {
    z.push_index(0x18A9, k::Index::iy);
    z.store_a_direct(0x18AB);
    z.load_immediate(0x18AE, z.r.a);
    z.store_a_direct(0x18B0);
    return z.final_call(0x18B3);
}
template <class Z> Exit finish_effect_animation(Z &z) {
    z.pop_index(0x18B6, k::Index::iy);
    return z.final_return(0x18B8);
}
template <class Z> Exit wait_effect_frame(Z &z) {
    z.load_immediate(0x18B9, z.r.a);
    return z.final_call(0x18BB);
}
template <class Z> Exit check_effect_animation(Z &z) {
    z.load_a_direct(0x18C1);
    z.or_a(0x18C4);
    if (z.jump_if(0x18C5, !z.zero()))
        return wait_effect_frame(z);
    z.load_immediate(0x18C8, z.r.a);
    return z.final_call(0x18CA);
}
template <class Z> Exit wait_enemy_frame(Z &z) {
    z.load_immediate(0x18DB, z.r.a);
    return z.final_call(0x18DD);
}
template <class Z> Exit begin_enemy_animation(Z &z) {
    z.push_index(0x18CE, k::Index::iy);
    z.load_immediate(0x18D0, z.r.a);
    z.store_a_direct(0x18D2);
    z.load_a_direct(0x18D5);
    z.store_a_direct(0x18D8);
    return wait_enemy_frame(z);
}
template <class Z> Exit check_enemy_animation(Z &z) {
    z.load_a_direct(0x18E3);
    z.or_a(0x18E6);
    if (z.jump_if(0x18E7, !z.zero()))
        return wait_enemy_frame(z);
    z.load_immediate(0x18EA, z.r.a);
    return z.final_call(0x18EC);
}
template <class Z> Exit finish_enemy_animation(Z &z) {
    z.pop_index(0x18EF, k::Index::iy);
    return z.final_return(0x18F1);
}
// Incoming physical attacks share variance with outgoing attacks, but flag 6
// weakens the enemy after the saturating flag-7 boost. Protection bypasses
// damage; C2ED tells the caller whether to display a hit or a miss.
template <class Z> Exit enemy_miss(Z &z) {
    z.xor_a(0x15AD);
    z.store_a_direct(0x15AE);
    return z.final_return(0x15B1);
}
template <class Z> Exit enemy_attack_strength(Z &z) {
    z.load_from_indexed(0x1579, k::Index::iy, z.r.a);
    z.test_bit_indexed(0x157C, k::Index::iy, 7);
    if (!z.relative_if(0x1580, z.zero())) {
        z.copy(0x1582, z.r.c, z.r.a);
        z.rotate_right_circular_a(0x1583);
        z.and_immediate(0x1584);
        z.add_register(0x1586, z.r.c);
        if (!z.relative_if(0x1587, !z.carry()))
            z.load_immediate(0x1589, z.r.a);
    }
    z.test_bit_indexed(0x158B, k::Index::iy, 6);
    if (!z.relative_if(0x158F, z.zero())) {
        z.copy(0x1591, z.r.c, z.r.a);
        z.rotate_right_circular_a(0x1592);
        z.rotate_right_circular_a(0x1593);
        z.and_immediate(0x1594);
        z.copy(0x1596, z.r.b, z.r.a);
        z.copy(0x1597, z.r.a, z.r.c);
        z.subtract_register(0x1598, z.r.b);
    }
    return z.final_call(0x1599);
}
template <class Z> Exit begin_enemy_attack(Z &z) {
    z.load_a_direct(0x1573);
    z.or_a(0x1576);
    if (z.relative_if(0x1577, !z.zero()))
        return enemy_miss(z);
    return enemy_attack_strength(z);
}
template <class Z> Exit enemy_defense_strength(Z &z) {
    z.copy(0x159C, z.r.c, z.r.a);
    z.load_from_indexed(0x159D, k::Index::ix, z.r.a);
    return z.final_call(0x15A0);
}
template <class Z> Exit apply_enemy_damage(Z &z) {
    z.add_from_indexed(0x15C2, k::Index::ix);
    if (!z.relative_if(0x15C5, z.carry()))
        z.xor_a(0x15C7);
    z.store_to_indexed(0x15C8, k::Index::ix, z.r.a);
    if (!z.relative_if(0x15CB, !z.zero())) {
        z.store_to_indexed(0x15CD, k::Index::ix, z.r.a);
        z.store_to_indexed(0x15D0, k::Index::ix, z.r.a);
    }
    z.load_immediate(0x15D3, z.r.a);
    z.store_a_direct(0x15D5);
    return z.final_return(0x15D8);
}
template <class Z> Exit resolve_enemy_attack(Z &z) {
    z.subtract_register(0x15A3, z.r.c);
    if (z.relative_if(0x15A4, z.carry()))
        return apply_enemy_damage(z);
    z.compare_immediate(0x15A6);
    if (z.relative_if(0x15A8, z.carry()))
        return z.final_call(0x15B2);
    z.rotate_right_circular_a(0x15AA);
    if (z.relative_if(0x15AB, z.carry()))
        return z.final_call(0x15B2);
    return enemy_miss(z);
}
template <class Z> Exit accept_enemy_minimum_damage(Z &z) {
    z.and_immediate(0x15B5);
    z.compare_indexed(0x15B7, k::Index::ix);
    if (!z.relative_if(0x15BA, z.zero())) {
        if (z.relative_if(0x15BC, !z.carry()))
            return z.final_call(0x15B2);
    }
    z.rotate_right_circular_a(0x15BE);
    z.and_immediate(0x15BF);
    z.complement_a(0x15C1);
    return apply_enemy_damage(z);
}
} // namespace
void register_battle_damage(RoutineRegistry &registry) {
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
    // After the damage announcement, the round closes the battle window.
    single(0x124E, 0x121D, {core_final_at(0x124E)}, {}, [](auto &z) { return z.final_jump(0x124E); });
    // An enemy action starts with the alive check: a dead actor returns.
    single(0x128F, 0x1279, {returned_at(0x128F), core_final_at(0x128F)}, {}, [](auto &z) -> Exit {
        if (auto exit = z.final_return_if(0x128F, k::Condition::z))
            return *exit;
        return k::fall_through(0x128F, 0x1290);
    });
    // The effect and enemy animation waits each end with a frame call and a return.
    single(0x18BE, 0x18B9, {core_final_at(0x18BE)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x18BE); });
    single(0x18CD, 0x18B9, {returned_at(0x18CD)}, {}, [](auto &z) { return z.final_return(0x18CD); });
    single(0x18E0, 0x18CE, {core_final_at(0x18E0)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x18E0); });
    add(0x1573, 0x1573, {returned_at(0x15B1), core_final_at(0x1599)}, {},
        [](auto &z) { return begin_enemy_attack(z); });
    add(0x1579, 0x1573, {core_final_at(0x1599)}, {},
        [](auto &z) { return enemy_attack_strength(z); });
    add(0x159C, 0x1573, {core_final_at(0x15A0)}, {},
        [](auto &z) { return enemy_defense_strength(z); });
    add(0x15A3, 0x1573, {returned_at(0x15B1), returned_at(0x15D8), core_final_at(0x15B2)}, {},
        [](auto &z) { return resolve_enemy_attack(z); });
    add(0x15B5, 0x1573, {returned_at(0x15D8), core_final_at(0x15B2)}, {},
        [](auto &z) { return accept_enemy_minimum_damage(z); });
    add(0x15C2, 0x1573, {returned_at(0x15D8)}, {}, [](auto &z) { return apply_enemy_damage(z); });
    add(0x121D, 0x121D, {core_final_at(0x122F)}, {},
        [](auto &z) { return choose_attack_strength(z); });
    add(0x1232, 0x121D, {core_final_at(0x1236)}, {},
        [](auto &z) { return choose_defense_strength(z); });
    add(0x1239, 0x121D, {core_final_at(0x1264, -2), core_final_at(0x1251), core_final_at(0x124B)},
        {.scratch_stack = 2}, [](auto &z) { return resolve_attack_strength(z); });
    add(0x1254, 0x121D, {core_final_at(0x1264, -2), core_final_at(0x1251)}, {.scratch_stack = 2},
        [](auto &z) { return accept_minimum_damage(z); });
    add(0x125E, 0x125E, {core_final_at(0x1264, -2)}, {.scratch_stack = 2},
        [](auto &z) { return announce_damage(z); });
    add(0x1267, 0x125E, {returned_at(0x1271, 2), returned_at(0x1278, 2)}, {.inherited_stack = 2},
        [](auto &z) { return apply_damage(z); });
    add(0x1279, 0x1279, {core_final_at(0x1281)}, {},
        [](auto &z) { return begin_stat_variation(z); });
    add(0x1284, 0x1279, {core_final_at(0x1285)}, {},
        [](auto &z) { return multiply_stat_variation(z); });
    add(0x1288, 0x1279, {returned_at(0x128B)}, {},
        [](auto &z) { return finish_stat_variation(z); });
    add(0x18A9, 0x18A9, {core_final_at(0x18B3, -2)}, {.scratch_stack = 2},
        [](auto &z) { return begin_effect_animation(z); });
    add(0x18B6, 0x18A9, {returned_at(0x18B8, 2)}, {.inherited_stack = 2},
        [](auto &z) { return finish_effect_animation(z); });
    add(0x18B9, 0x18B9, {core_final_at(0x18BB)}, {}, [](auto &z) { return wait_effect_frame(z); });
    add(0x18C1, 0x18B9, {core_final_at(0x18BB), core_final_at(0x18CA)}, {},
        [](auto &z) { return check_effect_animation(z); });
    add(0x18CE, 0x18CE, {core_final_at(0x18DD, -2)}, {.scratch_stack = 2},
        [](auto &z) { return begin_enemy_animation(z); });
    add(0x18DB, 0x18CE, {core_final_at(0x18DD)}, {.inherited_stack = 2},
        [](auto &z) { return wait_enemy_frame(z); });
    add(0x18E3, 0x18CE, {core_final_at(0x18DD), core_final_at(0x18EC)}, {.inherited_stack = 2},
        [](auto &z) { return check_enemy_animation(z); });
    add(0x18EF, 0x18CE, {returned_at(0x18F1, 2)}, {.inherited_stack = 2},
        [](auto &z) { return finish_enemy_animation(z); });
}
} // namespace psmodern::game
