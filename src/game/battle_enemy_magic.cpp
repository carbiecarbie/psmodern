// SPDX-License-Identifier: GPL-3.0-or-later
// Special enemy actions: blocking, healing, boost, double/group strikes and death.
#include "game/battle_enemy_magic.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::continues;
using k::Exit;
using k::returns;
template <class Z> Exit scan_party_actor(Z &z) {
    z.copy(0x1436, z.r.a, z.r.b);
    z.subtract_immediate(0x1437);
    z.negate(0x1439);
    return continues(0x143B, k::call_cycles);
}
template <class Z> Exit choose_block_action(Z &z) {
    z.and_immediate(0x1389);
    return continues(0x138B, k::jump_cycles);
}
template <class Z> Exit consume_block_protection(Z &z) {
    z.load_a_direct(0x138E);
    z.and_immediate(0x1391);
    return continues(0x1393, z.zero() ? k::untaken_call_cycles : k::call_cycles);
}
template <class Z> Exit check_block_protection(Z &z) {
    z.load_a_direct(0x1396);
    z.and_immediate(0x1399);
    if (z.relative_if(0x139B, z.zero()))
        return continues(0x13A6, k::call_cycles);
    z.load_hl(0x139D);
    return continues(0x13A0, k::call_cycles);
}
template <class Z> Exit draw_block_target(Z &z) {
    z.and_immediate(0x13A9);
    return continues(0x13AB, k::call_cycles);
}
template <class Z> Exit check_target_block(Z &z) {
    z.store_a_direct(0x13B0);
    z.load_immediate(0x13B3, z.r.a);
    z.add_register(0x13B5, z.r.l);
    z.copy(0x13B6, z.r.l, z.r.a);
    z.load_from_hl(0x13B7, z.r.a);
    z.or_a(0x13B8);
    return continues(0x13B9, k::jump_cycles);
}
template <class Z> Exit block_party_target(Z &z) {
    z.store_to_hl(0x13BC);
    z.load_immediate(0x13BE, z.r.a);
    z.store_a_direct(0x13C0);
    z.load_hl(0x13C3);
    return continues(0x13C6, k::call_cycles);
}
template <class Z> Exit heal_enemy(Z &z) {
    z.load_from_indexed(0x13DB, k::Index::iy, z.r.b);
    z.load_from_indexed(0x13DE, k::Index::iy, z.r.a);
    z.add_immediate(0x13E1);
    if (!z.relative_if(0x13E3, !z.carry()))
        z.load_immediate(0x13E5, z.r.a);
    z.compare_register(0x13E7, z.r.b);
    if (!z.relative_if(0x13E8, z.carry()))
        z.copy(0x13EA, z.r.a, z.r.b);
    z.store_to_indexed(0x13EB, k::Index::iy, z.r.a);
    z.load_immediate(0x13EE, z.r.a);
    z.store_a_direct(0x13F0);
    return continues(0x13F3, k::call_cycles);
}
template <class Z> Exit choose_heal_action(Z &z) {
    z.load_from_indexed(0x13CC, k::Index::iy, z.r.a);
    z.compare_immediate(0x13CF);
    if (z.relative_if(0x13D1, z.carry()))
        return heal_enemy(z);
    return continues(0x13D3, k::call_cycles);
}
template <class Z> Exit accept_heal_action(Z &z) {
    z.and_immediate(0x13D6);
    return continues(0x13D8, k::jump_cycles);
}
template <class Z> Exit report_enemy_healing(Z &z) {
    z.load_hl(0x13F6);
    return continues(0x13F9, k::call_cycles);
}
template <class Z> Exit choose_boost_action(Z &z) {
    z.and_immediate(0x1402);
    return continues(0x1404, k::jump_cycles);
}
template <class Z> Exit check_existing_boost(Z &z) {
    z.load_from_indexed(0x1407, k::Index::iy, z.r.a);
    z.and_immediate(0x140A);
    return continues(0x140C, k::jump_cycles);
}
template <class Z> Exit boost_enemy(Z &z) {
    z.set_bit_indexed(0x140F, k::Index::iy, 7);
    z.load_immediate(0x1413, z.r.a);
    z.store_a_direct(0x1415);
    z.load_hl(0x1418);
    return continues(0x141B, k::call_cycles);
}
template <class Z> Exit choose_double_strike(Z &z) {
    z.and_immediate(0x1424);
    return continues(0x1426, k::jump_cycles);
}
template <class Z> Exit consume_strike_protection(Z &z) {
    z.load_a_direct(0x142C);
    z.and_immediate(0x142F);
    return continues(0x1431, z.zero() ? k::untaken_call_cycles : k::call_cycles);
}
template <class Z> Exit begin_living_party_scan(Z &z) {
    z.load_immediate(0x1434, z.r.b);
    return scan_party_actor(z);
}
template <class Z> Exit continue_living_party_scan(Z &z) {
    if (z.decrement_jump(0x1440))
        return scan_party_actor(z);
    return returns(0x1442, k::return_cycles);
}
template <class Z> Exit draw_strike_target(Z &z) {
    z.and_immediate(0x1446);
    return continues(0x1448, k::call_cycles);
}
template <class Z> Exit prepare_strike_target(Z &z) {
    z.store_a_direct(0x144E);
    z.store_a_direct(0x1451);
    return continues(0x1454, k::call_cycles);
}
template <class Z> Exit strike_party_target(Z &z) {
    z.and_immediate(0x145A);
    z.add_immediate(0x145C);
    z.copy(0x145E, z.r.b, z.r.a);
    z.load_a_direct(0x145F);
    z.and_immediate(0x1462);
    z.copy(0x1464, z.r.a, z.r.b);
    return continues(0x1465, z.zero() ? k::call_cycles : k::untaken_call_cycles);
}
template <class Z> Exit animate_party_strike(Z &z) {
    z.load_immediate(0x1468, z.r.a);
    z.store_a_direct(0x146A);
    return continues(0x146D, k::call_cycles);
}
template <class Z> Exit check_struck_actor(Z &z) {
    z.load_a_direct(0x1480);
    return continues(0x1483, k::call_cycles);
}
template <class Z> Exit report_strike_protection(Z &z) {
    z.load_a_direct(0x1470);
    z.and_immediate(0x1473);
    if (z.relative_if(0x1475, z.zero()))
        return check_struck_actor(z);
    z.load_hl(0x1477);
    return continues(0x147A, k::call_cycles);
}
template <class Z> Exit begin_strike_wait(Z &z) {
    z.load_immediate(0x1491, z.r.b);
    z.load_immediate(0x1493, z.r.a);
    return continues(0x1495, k::call_cycles);
}
template <class Z> Exit report_strike_death(Z &z) {
    if (z.relative_if(0x1486, !z.zero()))
        return begin_strike_wait(z);
    z.load_hl(0x1488);
    return continues(0x148B, k::call_cycles);
}
template <class Z> Exit continue_strike_wait(Z &z) {
    if (z.decrement_jump(0x1498)) {
        z.load_immediate(0x1493, z.r.a);
        return continues(0x1495, k::call_cycles);
    }
    return continues(0x149A, k::jump_cycles);
}
template <class Z> Exit choose_group_spell(Z &z) {
    z.and_immediate(0x14A0);
    return continues(0x14A2, k::jump_cycles);
}
template <class Z> Exit consume_group_protection(Z &z) {
    z.push(0x14A9, z.r.b, z.r.c);
    z.load_a_direct(0x14AA);
    z.and_immediate(0x14AD);
    return continues(0x14AF, z.zero() ? k::untaken_call_cycles : k::call_cycles);
}
template <class Z> Exit begin_group_spell(Z &z) {
    z.load_immediate(0x14A7, z.r.b);
    return consume_group_protection(z);
}
template <class Z> Exit begin_fire_spell(Z &z) {
    z.load_immediate(0x14A5, z.r.c);
    return begin_group_spell(z);
}
template <class Z> Exit scan_group_target(Z &z) {
    z.pop(0x14B2, z.r.b, z.r.c);
    z.push(0x14B3, z.r.b, z.r.c);
    z.copy(0x14B4, z.r.a, z.r.b);
    z.subtract_immediate(0x14B5);
    z.negate(0x14B7);
    return continues(0x14B9, k::call_cycles);
}
template <class Z> Exit prepare_group_target(Z &z) {
    z.store_a_direct(0x14BE);
    z.store_a_direct(0x14C1);
    z.push(0x14C4, z.r.b, z.r.c);
    return continues(0x14C5, k::call_cycles);
}
template <class Z> Exit restore_group_damage(Z &z) {
    z.pop(0x14C8, z.r.b, z.r.c);
    return continues(0x14C9, k::call_cycles);
}
template <class Z> Exit animate_group_target(Z &z) {
    z.load_immediate(0x14CC, z.r.a);
    z.store_a_direct(0x14CE);
    return continues(0x14D1, k::call_cycles);
}
template <class Z> Exit check_group_target(Z &z) {
    z.load_a_direct(0x14E4);
    return continues(0x14E7, k::call_cycles);
}
template <class Z> Exit report_group_protection(Z &z) {
    z.load_a_direct(0x14D4);
    z.and_immediate(0x14D7);
    if (z.relative_if(0x14D9, z.zero()))
        return check_group_target(z);
    z.load_hl(0x14DB);
    return continues(0x14DE, k::call_cycles);
}
template <class Z> Exit begin_group_wait(Z &z) {
    z.load_immediate(0x14F5, z.r.b);
    z.load_immediate(0x14F7, z.r.a);
    return continues(0x14F9, k::call_cycles);
}
template <class Z> Exit report_group_death(Z &z) {
    if (z.relative_if(0x14EA, !z.zero()))
        return begin_group_wait(z);
    z.load_hl(0x14EC);
    return continues(0x14EF, k::call_cycles);
}
template <class Z> Exit continue_group_wait(Z &z) {
    if (z.decrement_jump(0x14FC)) {
        z.load_immediate(0x14F7, z.r.a);
        return continues(0x14F9, k::call_cycles);
    }
    return continues(0x14FE, k::call_cycles);
}
template <class Z> Exit continue_group_spell(Z &z) {
    z.pop(0x1501, z.r.b, z.r.c);
    if (z.decrement_jump(0x1502))
        return consume_group_protection(z);
    return returns(0x1504, k::return_cycles);
}
template <class Z> Exit choose_group_damage(Z &z) {
    z.copy(0x1505, z.r.a, z.r.c);
    z.compare_immediate(0x1506);
    return continues(0x1508, k::jump_cycles);
}
template <class Z> Exit check_group_damage_protection(Z &z) {
    z.load_a_direct(0x150B);
    z.and_immediate(0x150E);
    if (!z.zero())
        return returns(0x1510, k::taken_conditional_return_cycles);
    z.return_not_taken(0x1510);
    return continues(0x1511, k::call_cycles);
}
template <class Z> Exit vary_group_damage(Z &z) {
    z.and_immediate(0x1514);
    z.add_register(0x1516, z.r.c);
    return continues(0x1517, k::jump_cycles);
}
template <class Z> Exit consume_lethal_protection(Z &z) {
    z.load_a_direct(0x1528);
    z.or_a(0x152B);
    return continues(0x152C, z.zero() ? k::untaken_call_cycles : k::call_cycles);
}
template <class Z> Exit check_lethal_shield(Z &z) {
    z.load_a_direct(0x151A);
    z.or_a(0x151D);
    if (z.relative_if(0x151E, z.zero()))
        return consume_lethal_protection(z);
    z.load_a_direct(0x1520);
    z.compare_immediate(0x1523);
    return continues(0x1525, k::jump_cycles);
}
template <class Z> Exit draw_lethal_target(Z &z) {
    z.and_immediate(0x1532);
    return continues(0x1534, k::call_cycles);
}
template <class Z> Exit prepare_lethal_target(Z &z) {
    z.store_a_direct(0x153A);
    z.store_a_direct(0x153D);
    z.push(0x1540, z.r.h, z.r.l);
    return continues(0x1541, k::call_cycles);
}
template <class Z> Exit kill_party_target(Z &z) {
    z.pop(0x1544, z.r.h, z.r.l);
    z.xor_a(0x1545);
    z.store_register_to_hl(0x1546, z.r.a);
    z.increment_hl(0x1547);
    z.store_register_to_hl(0x1548, z.r.a);
    return continues(0x1549, k::call_cycles);
}
template <class Z> Exit report_lethal_target(Z &z) {
    z.load_hl(0x154C);
    return continues(0x154F, k::call_cycles);
}
template <class Z> Exit begin_lethal_wait(Z &z) {
    z.load_immediate(0x1555, z.r.b);
    z.load_immediate(0x1557, z.r.a);
    return continues(0x1559, k::call_cycles);
}
template <class Z> Exit continue_lethal_wait(Z &z) {
    if (z.decrement_jump(0x155C)) {
        z.load_immediate(0x1557, z.r.a);
        return continues(0x1559, k::call_cycles);
    }
    return continues(0x155E, k::jump_cycles);
}
template <class Z> Exit find_crystal(Z &z) {
    z.load_immediate(0x1561, z.r.a);
    z.store_a_direct(0x1563);
    return continues(0x1566, k::call_cycles);
}
template <class Z> Exit choose_crystal_damage(Z &z) {
    z.load_immediate(0x1569, z.r.c);
    return continues(0x156B, k::jump_cycles);
}
template <class Z> Exit choose_physical_group_damage(Z &z) {
    z.load_immediate(0x156E, z.r.c);
    return continues(0x1570, k::jump_cycles);
}
} // namespace
void register_battle_enemy_magic(RoutineRegistry &registry) {
    using k::conditional_at;
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, 0x1386}, exits, permissions), logic);
    };
    add(0x1389, {conditional_at(0x138B)}, {}, [](auto &z) { return choose_block_action(z); });
    add(0x138E, {core_final_at(0x1393)}, {}, [](auto &z) { return consume_block_protection(z); });
    add(0x1396, {core_final_at(0x13A6), core_final_at(0x13A0)}, {},
        [](auto &z) { return check_block_protection(z); });
    add(0x13A9, {core_final_at(0x13AB)}, {}, [](auto &z) { return draw_block_target(z); });
    add(0x13B0, {conditional_at(0x13B9)}, {}, [](auto &z) { return check_target_block(z); });
    add(0x13BC, {core_final_at(0x13C6)}, {}, [](auto &z) { return block_party_target(z); });
    add(0x13CC, {core_final_at(0x13F3), core_final_at(0x13D3)}, {},
        [](auto &z) { return choose_heal_action(z); });
    add(0x13D6, {conditional_at(0x13D8)}, {}, [](auto &z) { return accept_heal_action(z); });
    add(0x13DB, {core_final_at(0x13F3)}, {}, [](auto &z) { return heal_enemy(z); });
    add(0x13F6, {core_final_at(0x13F9)}, {}, [](auto &z) { return report_enemy_healing(z); });
    add(0x1402, {conditional_at(0x1404)}, {}, [](auto &z) { return choose_boost_action(z); });
    add(0x1407, {conditional_at(0x140C)}, {}, [](auto &z) { return check_existing_boost(z); });
    add(0x140F, {core_final_at(0x141B)}, {}, [](auto &z) { return boost_enemy(z); });
    add(0x1424, {conditional_at(0x1426)}, {}, [](auto &z) { return choose_double_strike(z); });
    add(0x142C, {core_final_at(0x1431)}, {}, [](auto &z) { return consume_strike_protection(z); });
    add(0x1434, {core_final_at(0x143B)}, {}, [](auto &z) { return begin_living_party_scan(z); });
    add(0x1440, {core_final_at(0x143B), returned_at(0x1442, 0)}, {},
        [](auto &z) { return continue_living_party_scan(z); });
    add(0x1446, {core_final_at(0x1448)}, {}, [](auto &z) { return draw_strike_target(z); });
    add(0x144E, {core_final_at(0x1454)}, {}, [](auto &z) { return prepare_strike_target(z); });
    add(0x145A, {core_final_at(0x1465)}, {}, [](auto &z) { return strike_party_target(z); });
    add(0x1468, {core_final_at(0x146D)}, {}, [](auto &z) { return animate_party_strike(z); });
    add(0x1470, {core_final_at(0x1483), core_final_at(0x147A)}, {},
        [](auto &z) { return report_strike_protection(z); });
    add(0x1480, {core_final_at(0x1483)}, {}, [](auto &z) { return check_struck_actor(z); });
    add(0x1486, {core_final_at(0x1495), core_final_at(0x148B)}, {},
        [](auto &z) { return report_strike_death(z); });
    add(0x1491, {core_final_at(0x1495)}, {}, [](auto &z) { return begin_strike_wait(z); });
    add(0x1498, {core_final_at(0x1495), core_final_at(0x149A)}, {},
        [](auto &z) { return continue_strike_wait(z); });
    add(0x14A0, {conditional_at(0x14A2)}, {}, [](auto &z) { return choose_group_spell(z); });
    add(0x14A5, {core_final_at(0x14AF, -2)}, {.scratch_stack = 2},
        [](auto &z) { return begin_fire_spell(z); });
    add(0x14A7, {core_final_at(0x14AF, -2)}, {.scratch_stack = 2},
        [](auto &z) { return begin_group_spell(z); });
    add(0x14A9, {core_final_at(0x14AF, -2)}, {.scratch_stack = 2},
        [](auto &z) { return consume_group_protection(z); });
    add(0x14B2, {core_final_at(0x14B9)}, {.inherited_stack = 2},
        [](auto &z) { return scan_group_target(z); });
    add(0x14BE, {core_final_at(0x14C5, -2)}, {.scratch_stack = 2, .inherited_stack = 2},
        [](auto &z) { return prepare_group_target(z); });
    add(0x14C8, {core_final_at(0x14C9, 2)}, {.inherited_stack = 4},
        [](auto &z) { return restore_group_damage(z); });
    add(0x14CC, {core_final_at(0x14D1)}, {.inherited_stack = 2},
        [](auto &z) { return animate_group_target(z); });
    add(0x14D4, {core_final_at(0x14E7), core_final_at(0x14DE)}, {.inherited_stack = 2},
        [](auto &z) { return report_group_protection(z); });
    add(0x14E4, {core_final_at(0x14E7)}, {.inherited_stack = 2},
        [](auto &z) { return check_group_target(z); });
    add(0x14EA, {core_final_at(0x14F9), core_final_at(0x14EF)}, {.inherited_stack = 2},
        [](auto &z) { return report_group_death(z); });
    add(0x14F5, {core_final_at(0x14F9)}, {.inherited_stack = 2},
        [](auto &z) { return begin_group_wait(z); });
    add(0x14FC, {core_final_at(0x14F9), core_final_at(0x14FE)}, {.inherited_stack = 2},
        [](auto &z) { return continue_group_wait(z); });
    add(0x1501, {core_final_at(0x14AF), returned_at(0x1504, 2)}, {.inherited_stack = 2},
        [](auto &z) { return continue_group_spell(z); });
    add(0x1505, {conditional_at(0x1508)}, {.inherited_stack = 2},
        [](auto &z) { return choose_group_damage(z); });
    add(0x150B, {returned_at(0x1510, 0), core_final_at(0x1511)}, {.inherited_stack = 2},
        [](auto &z) { return check_group_damage_protection(z); });
    add(0x1514, {core_final_at(0x1517)}, {.inherited_stack = 2},
        [](auto &z) { return vary_group_damage(z); });
    add(0x151A, {core_final_at(0x152C), conditional_at(0x1525)}, {},
        [](auto &z) { return check_lethal_shield(z); });
    add(0x1528, {core_final_at(0x152C)}, {}, [](auto &z) { return consume_lethal_protection(z); });
    add(0x1532, {core_final_at(0x1534)}, {}, [](auto &z) { return draw_lethal_target(z); });
    add(0x153A, {core_final_at(0x1541, -2)}, {.scratch_stack = 2},
        [](auto &z) { return prepare_lethal_target(z); });
    add(0x1544, {core_final_at(0x1549, 2)}, {.inherited_stack = 2},
        [](auto &z) { return kill_party_target(z); });
    add(0x154C, {core_final_at(0x154F)}, {}, [](auto &z) { return report_lethal_target(z); });
    add(0x1555, {core_final_at(0x1559)}, {}, [](auto &z) { return begin_lethal_wait(z); });
    add(0x155C, {core_final_at(0x1559), core_final_at(0x155E)}, {},
        [](auto &z) { return continue_lethal_wait(z); });
    add(0x1561, {core_final_at(0x1566)}, {}, [](auto &z) { return find_crystal(z); });
    add(0x1569, {conditional_at(0x156B)}, {}, [](auto &z) { return choose_crystal_damage(z); });
    add(0x156E, {core_final_at(0x1570)}, {},
        [](auto &z) { return choose_physical_group_damage(z); });
}
} // namespace psmodern::game
