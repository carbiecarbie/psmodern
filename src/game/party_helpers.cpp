// SPDX-License-Identifier: GPL-3.0-or-later
// Refresh level/equipment-derived party stats and activity lookup helpers.
#include "game/joins.h"
#include "game/party_helpers.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;
template <class Z> Exit refresh_one_member(Z &z) {
    z.test_bit_indexed(0x17E4, k::Index::iy, 0);
    if (auto exit=z.final_return_if(0x17E8,k::Condition::z)) return *exit;
    z.store_immediate_indexed(0x17E9, k::Index::iy);
    z.store_immediate_indexed(0x17ED, k::Index::iy);
    z.load_from_indexed(0x17F1, k::Index::iy, z.r.l);
    z.load_immediate(0x17F4, z.r.h);
    z.add_to_hl(0x17F6, z.hl());
    z.add_to_hl(0x17F7, z.hl());
    z.add_to_hl(0x17F8, z.hl());
    z.add_to_hl(0x17F9, z.de());
    z.push(0x17FA, z.r.h, z.r.l);
    z.pop_index(0x17FB, k::Index::ix);
    z.load_from_indexed(0x17FD, k::Index::ix, z.r.a);
    z.store_to_indexed(0x1800, k::Index::iy, z.r.a);
    z.load_from_indexed(0x1803, k::Index::iy, z.r.l);
    z.load_immediate(0x1806, z.r.h);
    z.load_de(0x1808);
    z.add_to_hl(0x180B, z.de());
    z.load_from_hl(0x180C, z.r.a);
    z.add_from_indexed(0x180D, k::Index::ix);
    z.store_to_indexed(0x1810, k::Index::iy, z.r.a);
    z.load_from_indexed(0x1813, k::Index::iy, z.r.l);
    z.load_immediate(0x1816, z.r.h);
    z.add_to_hl(0x1818, z.de());
    z.load_from_hl(0x1819, z.r.a);
    z.load_from_indexed(0x181A, k::Index::iy, z.r.l);
    z.load_immediate(0x181D, z.r.h);
    z.add_to_hl(0x181F, z.de());
    z.add_from_hl(0x1820);
    z.add_from_indexed(0x1821, k::Index::ix);
    z.store_to_indexed(0x1824, k::Index::iy, z.r.a);
    z.load_from_indexed(0x1827, k::Index::ix, z.r.a);
    z.store_to_indexed(0x182A, k::Index::iy, z.r.a);
    z.load_from_indexed(0x182D, k::Index::ix, z.r.a);
    z.store_to_indexed(0x1830, k::Index::iy, z.r.a);
    z.load_from_indexed(0x1833, k::Index::ix, z.r.a);
    z.store_to_indexed(0x1836, k::Index::iy, z.r.a);
    return z.final_return(0x1839);
}
template <class Z> Exit refresh_alis(Z &z) {
    z.load_hl(0x17BA);
    z.store_to_hl(0x17BD);
    z.load_index(0x17BF, k::Index::iy);
    z.load_de(0x17C3);
    return z.final_call(0x17C6);
}
template <class Z> Exit refresh_myau(Z &z) {
    z.load_index(0x17C9, k::Index::iy);
    z.load_de(0x17CD);
    return z.final_call(0x17D0);
}
template <class Z> Exit refresh_odin(Z &z) {
    z.load_index(0x17D3, k::Index::iy);
    z.load_de(0x17D7);
    return z.final_call(0x17DA);
}
template <class Z> Exit refresh_noah(Z &z) {
    z.load_index(0x17DD, k::Index::iy);
    z.load_de(0x17E1);
    return refresh_one_member(z);
}
template <class Z> Exit find_actor_record(Z &z) {
    z.push(0x187D, z.r.a, z.r.f);
    z.add_register(0x187E, z.r.a);
    z.add_register(0x187F, z.r.a);
    z.add_register(0x1880, z.r.a);
    z.add_register(0x1881, z.r.a);
    z.load_hl(0x1882);
    z.add_register(0x1885, z.r.l);
    z.copy(0x1886, z.r.l, z.r.a);
    z.add_with_carry(0x1887, z.r.h);
    z.subtract_register(0x1888, z.r.l);
    z.copy(0x1889, z.r.h, z.r.a);
    z.pop(0x188A, z.r.a, z.r.f);
    z.test_bit_at_hl(0x188B, 0);
    return z.final_return(0x188D);
}
template <class Z> Exit find_current_actor_record(Z &z) {
    z.load_a_direct(0x187A);
    return find_actor_record(z);
}
template <class Z> Exit check_actor_warning(Z &z) {
    z.push(0x188E, z.r.h, z.r.l);
    return z.final_call(0x188F);
}
template <class Z> Exit warn_inactive_actor(Z &z) {
    z.pop(0x1892, z.r.h, z.r.l);
    if (auto exit=z.final_return_if(0x1893,k::Condition::nz)) return *exit;
    z.push(0x1894, z.r.a, z.r.f);
    z.push(0x1895, z.r.b, z.r.c);
    z.push(0x1896, z.r.d, z.r.e);
    z.push(0x1897, z.r.h, z.r.l);
    z.store_a_direct(0x1898);
    z.load_hl(0x189B);
    return z.final_call(0x189E);
}
template <class Z> Exit restore_actor_warning(Z &z) {
    z.pop(0x18A4, z.r.h, z.r.l);
    z.pop(0x18A5, z.r.d, z.r.e);
    z.pop(0x18A6, z.r.b, z.r.c);
    z.pop(0x18A7, z.r.a, z.r.f);
    return z.final_return(0x18A8);
}
} // namespace
void register_party_helpers(RoutineRegistry &registry) {
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };
    add(0x17BA, 0x17BA, {core_final_at(0x17C6)}, {.slot2_mapper = true},
        [](auto &z) { return refresh_alis(z); });
    add(0x17C9, 0x17BA, {core_final_at(0x17D0)}, {}, [](auto &z) { return refresh_myau(z); });
    add(0x17D3, 0x17BA, {core_final_at(0x17DA)}, {}, [](auto &z) { return refresh_odin(z); });
    add(0x17DD, 0x17BA, {returned_at(0x17E8), returned_at(0x1839)}, {.scratch_stack = 2},
        [](auto &z) { return refresh_noah(z); });
    add(0x17E4, 0x17BA, {returned_at(0x17E8), returned_at(0x1839)}, {.scratch_stack = 2},
        [](auto &z) { return refresh_one_member(z); });
    add(0x187A, 0x187A, {returned_at(0x188D)}, {.scratch_stack = 2},
        [](auto &z) { return find_current_actor_record(z); });
    add(0x187D, 0x187A, {returned_at(0x188D)}, {.scratch_stack = 2},
        [](auto &z) { return find_actor_record(z); });
    add(0x188E, 0x188E, {core_final_at(0x188F, -2)}, {.scratch_stack = 2},
        [](auto &z) { return check_actor_warning(z); });
    add(0x1892, 0x188E, {returned_at(0x1893, 2), core_final_at(0x189E, -6)},
        {.scratch_stack = 6, .inherited_stack = 2}, [](auto &z) { return warn_inactive_actor(z); });
    add(0x18A4, 0x188E, {returned_at(0x18A8, 8)}, {.inherited_stack = 8},
        [](auto &z) { return restore_actor_warning(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_party_helpers_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x18A1},{0,0x188E},{kit::core_final_at(0x18A1)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x18A1); });
}
} // namespace psmodern::game
