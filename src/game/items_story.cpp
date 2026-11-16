// SPDX-License-Identifier: GPL-3.0-or-later
// Story-item and dungeon-tool gates, deferred requests and shared effects.
#include "game/items_story.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::continues;
using k::Exit;
using k::returns;
template <class Z> Exit announce_alsulin(Z &);
template <class Z> Exit check_alsulin_battle(Z &);
template <class Z> Exit check_alsulin_scene(Z &);
template <class Z> Exit report_alsulin_wrong_scene(Z &);
template <class Z> Exit check_alsulin_party(Z &);
template <class Z> Exit restore_odin_equipment(Z &);
template <class Z> Exit finish_odin_restoration(Z &);
template <class Z> Exit announce_polymaterial(Z &);
template <class Z> Exit check_polymaterial_battle(Z &);
template <class Z> Exit check_polymaterial_scene(Z &);
template <class Z> Exit report_polymaterial_wrong_scene(Z &);
template <class Z> Exit check_human_records(Z &);
template <class Z> Exit check_polymaterial_party(Z &);
template <class Z> Exit report_polymaterial_success(Z &);
template <class Z> Exit announce_key_outside(Z &);
template <class Z> Exit report_key_outside(Z &);
template <class Z> Exit check_dungeon_key_context(Z &);
template <class Z> Exit find_dungeon_key_door(Z &);
template <class Z> Exit unlock_dungeon_key_door(Z &);
template <class Z> Exit refuse_dungeon_key_door(Z &);
template <class Z> Exit announce_sphere(Z &);
template <class Z> Exit choose_sphere_effect(Z &);
template <class Z> Exit announce_eclipse_torch(Z &);
template <class Z> Exit check_torch_battle(Z &);
template <class Z> Exit check_torch_scene(Z &);
template <class Z> Exit flash_torch_palette(Z &);
template <class Z> Exit find_torch_pot(Z &);
template <class Z> Exit check_torch_pot(Z &);
template <class Z> Exit report_torch_success(Z &);
template <class Z> Exit find_nuts_reward(Z &);
template <class Z> Exit announce_aero_prism(Z &);
template <class Z> Exit check_prism_battle(Z &);
template <class Z> Exit check_prism_scene(Z &);
template <class Z> Exit check_prism_phase(Z &);
template <class Z> Exit request_prism_effect(Z &);
template <class Z> Exit announce_nuts_refusal(Z &);
template <class Z> Exit check_nuts_phase(Z &);
template <class Z> Exit check_nuts_castle(Z &);
template <class Z> Exit check_nuts_conditions(Z &);
template <class Z> Exit report_nuts_refusal(Z &);
template <class Z> Exit request_myau_transform(Z &);
template <class Z> Exit check_hapsby_battle(Z &);
template <class Z> Exit report_battle_hapsby(Z &);
template <class Z> Exit check_compass_battle(Z &);
template <class Z> Exit check_compass_dungeon(Z &);
template <class Z> Exit report_compass_refusal(Z &);
template <class Z> Exit report_compass_direction(Z &);
template <class Z> Exit check_miracle_key_context(Z &);
template <class Z> Exit announce_miracle_key(Z &);
template <class Z> Exit find_miracle_key_door(Z &);
template <class Z> Exit unlock_miracle_key_door(Z &);
template <class Z> Exit refuse_miracle_key_door(Z &);
template <class Z> Exit announce_unusable_item(Z &);
template <class Z> Exit report_unusable_item(Z &);
template <class Z> Exit announce_alsulin(Z &z) {
    z.load_hl(0x242F);
    return continues(0x2432, k::call_cycles);
}
template <class Z> Exit check_alsulin_battle(Z &z) {
    z.load_a_direct(0x2435);
    z.or_a(0x2438);
    if (z.relative_if(0x2439, z.zero()))
        return check_alsulin_scene(z);
    z.load_hl(0x243B);
    return continues(0x243E, k::call_cycles);
}
template <class Z> Exit check_alsulin_scene(Z &z) {
    z.load_a_direct(0x2444);
    z.compare_immediate(0x2447);
    if (z.relative_if(0x2449, z.zero()))
        return continues(0x245C, k::call_cycles);
    return continues(0x244B, k::call_cycles);
}
template <class Z> Exit report_alsulin_wrong_scene(Z &z) {
    z.load_hl(0x244E);
    if (!z.relative_if(0x2451, !z.zero()))
        z.load_hl(0x2453);
    return continues(0x2456, k::call_cycles);
}
template <class Z> Exit check_alsulin_party(Z &z) {
    if (z.relative_if(0x245F, z.zero())) {
        z.load_hl(0x2453);
        return continues(0x2456, k::call_cycles);
    }
    z.load_hl(0x2461);
    return continues(0x2464, k::call_cycles);
}
template <class Z> Exit restore_odin_equipment(Z &z) {
    z.load_index(0x246D, k::Index::iy);
    z.store_immediate_indexed(0x2471, k::Index::iy);
    z.store_immediate_indexed(0x2475, k::Index::iy);
    return continues(0x2479, k::call_cycles);
}
template <class Z> Exit finish_odin_restoration(Z &z) {
    z.load_immediate(0x247C, z.r.a);
    z.store_a_direct(0x247E);
    z.load_hl(0x2481);
    z.store_to_hl(0x2484);
    z.load_hl(0x2486);
    z.store_to_hl(0x2489);
    z.load_immediate(0x248B, z.r.a);
    z.store_a_direct(0x248D);
    return returns(0x2490, k::return_cycles);
}
template <class Z> Exit announce_polymaterial(Z &z) {
    z.load_hl(0x2491);
    return continues(0x2494, k::call_cycles);
}
template <class Z> Exit check_polymaterial_battle(Z &z) {
    z.load_a_direct(0x2497);
    z.or_a(0x249A);
    if (z.relative_if(0x249B, z.zero()))
        return check_polymaterial_scene(z);
    z.load_hl(0x249D);
    return continues(0x24A0, k::call_cycles);
}
template <class Z> Exit check_polymaterial_scene(Z &z) {
    z.load_a_direct(0x24A6);
    z.compare_immediate(0x24A9);
    if (z.relative_if(0x24AB, z.zero()))
        return continues(0x24CC, k::call_cycles);
    return continues(0x24AD, k::call_cycles);
}
template <class Z> Exit report_polymaterial_wrong_scene(Z &z) {
    z.load_hl(0x24B0);
    if (!z.relative_if(0x24B3, z.zero()))
        z.load_hl(0x24B5);
    return continues(0x24B8, k::call_cycles);
}
template <class Z> Exit check_human_records(Z &z) {
    z.load_a_direct(0x24BE);
    z.copy(0x24C1, z.r.d, z.r.a);
    z.load_a_direct(0x24C2);
    z.copy(0x24C5, z.r.e, z.r.a);
    z.load_a_direct(0x24C6);
    z.or_register(0x24C9, z.r.d);
    z.or_register(0x24CA, z.r.e);
    return returns(0x24CB, k::return_cycles);
}
template <class Z> Exit check_polymaterial_party(Z &z) {
    if (z.relative_if(0x24CF, !z.zero())) {
        z.load_hl(0x24DA);
        return continues(0x24DD, k::call_cycles);
    }
    z.load_hl(0x24D1);
    return continues(0x24D4, k::call_cycles);
}
template <class Z> Exit report_polymaterial_success(Z &z) {
    z.load_hl(0x24DA);
    return continues(0x24DD, k::call_cycles);
}
template <class Z> Exit announce_key_outside(Z &z) {
    z.load_hl(0x24EF);
    return continues(0x24F2, k::call_cycles);
}
template <class Z> Exit report_key_outside(Z &z) {
    z.load_hl(0x24F5);
    return continues(0x24F8, k::call_cycles);
}
template <class Z> Exit check_dungeon_key_context(Z &z) {
    z.load_a_direct(0x24E9);
    z.or_a(0x24EC);
    if (z.relative_if(0x24ED, z.zero())) {
        z.load_hl(0x24FE);
        return continues(0x2501, k::call_cycles);
    }
    z.load_hl(0x24EF);
    return continues(0x24F2, k::call_cycles);
}
template <class Z> Exit find_dungeon_key_door(Z &z) {
    z.load_immediate(0x2504, z.r.b);
    return continues(0x2506, k::call_cycles);
}
template <class Z> Exit unlock_dungeon_key_door(Z &z) {
    z.and_immediate(0x2509);
    z.compare_immediate(0x250B);
    if (!z.relative_if(0x250D, !z.zero())) {
        z.test_bit_at_hl(0x250F, 7);
        if (!z.relative_if(0x2511, !z.zero())) {
            z.load_immediate(0x2513, z.r.a);
            z.store_a_direct(0x2515);
            return continues(0x2518, k::jump_cycles);
        }
    }
    z.load_hl(0x251B);
    return continues(0x251E, k::call_cycles);
}
template <class Z> Exit refuse_dungeon_key_door(Z &z) {
    z.load_hl(0x251B);
    return continues(0x251E, k::call_cycles);
}
template <class Z> Exit announce_sphere(Z &z) {
    z.load_hl(0x2527);
    return continues(0x252A, k::call_cycles);
}
template <class Z> Exit choose_sphere_effect(Z &z) {
    z.load_a_direct(0x252D);
    z.or_a(0x2530);
    return continues(0x2531, k::jump_cycles);
}
template <class Z> Exit announce_eclipse_torch(Z &z) {
    z.load_hl(0x2537);
    return continues(0x253A, k::call_cycles);
}
template <class Z> Exit check_torch_battle(Z &z) {
    z.load_a_direct(0x253D);
    z.or_a(0x2540);
    if (z.relative_if(0x2541, z.zero()))
        return check_torch_scene(z);
    z.load_hl(0x2543);
    return continues(0x2546, k::call_cycles);
}
template <class Z> Exit check_torch_scene(Z &z) {
    z.load_a_direct(0x254C);
    z.compare_immediate(0x254F);
    if (z.relative_if(0x2551, z.zero()))
        return flash_torch_palette(z);
    z.load_hl(0x2553);
    return continues(0x2556, k::call_cycles);
}
template <class Z> Exit flash_torch_palette(Z &z) {
    z.push(0x255C, z.r.b, z.r.c);
    return continues(0x255D, k::call_cycles);
}
template <class Z> Exit find_torch_pot(Z &z) {
    z.pop(0x2560, z.r.b, z.r.c);
    z.load_immediate(0x2561, z.r.a);
    return continues(0x2563, k::call_cycles);
}
template <class Z> Exit check_torch_pot(Z &z) {
    if (z.relative_if(0x2566, z.zero()))
        return continues(0x2571, k::call_cycles);
    z.load_hl(0x2568);
    return continues(0x256B, k::call_cycles);
}
template <class Z> Exit report_torch_success(Z &z) {
    z.load_hl(0x2574);
    return continues(0x2577, k::call_cycles);
}
template <class Z> Exit find_nuts_reward(Z &z) {
    z.load_immediate(0x257D, z.r.a);
    z.store_a_direct(0x257F);
    return continues(0x2582, k::call_cycles);
}
template <class Z> Exit announce_aero_prism(Z &z) {
    z.load_hl(0x2589);
    return continues(0x258C, k::call_cycles);
}
template <class Z> Exit check_prism_battle(Z &z) {
    z.load_a_direct(0x258F);
    z.or_a(0x2592);
    if (z.relative_if(0x2593, z.zero()))
        return check_prism_scene(z);
    z.load_hl(0x2595);
    return continues(0x2598, k::call_cycles);
}
template <class Z> Exit check_prism_scene(Z &z) {
    z.load_a_direct(0x259E);
    z.compare_immediate(0x25A1);
    if (z.relative_if(0x25A3, z.zero()))
        return check_prism_phase(z);
    z.load_hl(0x25A5);
    return continues(0x25A8, k::call_cycles);
}
template <class Z> Exit check_prism_phase(Z &z) {
    z.load_a_direct(0x25AE);
    z.compare_immediate(0x25B1);
    if (z.relative_if(0x25B3, z.zero())) {
        z.load_hl(0x25A5);
        return continues(0x25A8, k::call_cycles);
    }
    z.load_hl(0x25B5);
    return continues(0x25B8, k::call_cycles);
}
template <class Z> Exit request_prism_effect(Z &z) {
    z.load_immediate(0x25BB, z.r.a);
    z.store_a_direct(0x25BD);
    return continues(0x25C0, k::jump_cycles);
}
template <class Z> Exit announce_nuts_refusal(Z &z) {
    z.load_hl(0x25D7);
    return continues(0x25DA, k::call_cycles);
}
template <class Z> Exit check_nuts_phase(Z &z) {
    z.load_a_direct(0x25EF);
    z.compare_immediate(0x25F2);
    if (z.relative_if(0x25F4, !z.zero()))
        return announce_nuts_refusal(z);
    z.load_hl(0x25F6);
    return continues(0x25F9, k::call_cycles);
}
template <class Z> Exit check_nuts_castle(Z &z) {
    z.load_a_direct(0x2605);
    z.or_a(0x2608);
    if (z.relative_if(0x2609, z.zero()))
        return announce_nuts_refusal(z);
    z.load_a_direct(0x260B);
    z.or_a(0x260E);
    if (z.relative_if(0x260F, !z.zero()))
        return announce_nuts_refusal(z);
    z.relative(0x2611);
    z.load_hl(0x25F6);
    return continues(0x25F9, k::call_cycles);
}
template <class Z> Exit check_nuts_conditions(Z &z) {
    z.load_a_direct(0x25C3);
    z.or_a(0x25C6);
    if (z.relative_if(0x25C7, z.zero()))
        return announce_nuts_refusal(z);
    z.load_a_direct(0x25C9);
    z.compare_immediate(0x25CC);
    if (z.relative_if(0x25CE, z.zero()))
        return check_nuts_castle(z);
    z.load_a_direct(0x25D0);
    z.compare_immediate(0x25D3);
    if (z.relative_if(0x25D5, z.zero()))
        return check_nuts_phase(z);
    return announce_nuts_refusal(z);
}
template <class Z> Exit report_nuts_refusal(Z &z) {
    z.load_a_direct(0x25DD);
    z.or_a(0x25E0);
    z.load_hl(0x25E1);
    if (!z.relative_if(0x25E4, z.zero()))
        z.load_hl(0x25E6);
    return continues(0x25E9, k::call_cycles);
}
template <class Z> Exit request_myau_transform(Z &z) {
    z.load_immediate(0x25FF, z.r.a);
    z.store_a_direct(0x2601);
    return returns(0x2604, k::return_cycles);
}
template <class Z> Exit check_hapsby_battle(Z &z) {
    z.load_a_direct(0x2613);
    z.or_a(0x2616);
    if (z.relative_if(0x2617, z.zero())) {
        z.load_hl(0x2628);
        return continues(0x262B, k::call_cycles);
    }
    z.load_hl(0x2619);
    return continues(0x261C, k::call_cycles);
}
template <class Z> Exit report_battle_hapsby(Z &z) {
    z.load_hl(0x261F);
    return continues(0x2622, k::call_cycles);
}
template <class Z> Exit check_compass_battle(Z &z) {
    z.load_a_direct(0x264F);
    z.or_a(0x2652);
    if (z.relative_if(0x2653, !z.zero())) {
        z.load_hl(0x2637);
        return continues(0x263A, k::call_cycles);
    }
    z.load_hl(0x2655);
    return continues(0x2658, k::call_cycles);
}
template <class Z> Exit check_compass_dungeon(Z &z) {
    z.load_a_direct(0x2631);
    z.or_a(0x2634);
    if (z.relative_if(0x2635, z.zero()))
        return check_compass_battle(z);
    z.load_hl(0x2637);
    return continues(0x263A, k::call_cycles);
}
template <class Z> Exit report_compass_refusal(Z &z) {
    z.load_a_direct(0x263D);
    z.or_a(0x2640);
    z.load_hl(0x2641);
    if (!z.relative_if(0x2644, z.zero()))
        z.load_hl(0x2646);
    return continues(0x2649, k::call_cycles);
}
template <class Z> Exit report_compass_direction(Z &z) {
    z.load_a_direct(0x265B);
    z.and_immediate(0x265E);
    z.load_hl(0x2660);
    if (!z.relative_if(0x2663, z.zero())) {
        z.compare_immediate(0x2665);
        z.load_hl(0x2667);
        if (!z.relative_if(0x266A, z.zero())) {
            z.compare_immediate(0x266C);
            z.load_hl(0x266E);
            if (!z.relative_if(0x2671, z.zero()))
                z.load_hl(0x2673);
        }
    }
    return continues(0x2676, k::call_cycles);
}
template <class Z> Exit check_miracle_key_context(Z &z) {
    z.load_a_direct(0x267C);
    z.or_a(0x267F);
    return continues(0x2680, k::jump_cycles);
}
template <class Z> Exit announce_miracle_key(Z &z) {
    z.load_hl(0x2683);
    return continues(0x2686, k::call_cycles);
}
template <class Z> Exit find_miracle_key_door(Z &z) {
    z.load_immediate(0x2689, z.r.b);
    return continues(0x268B, k::call_cycles);
}
template <class Z> Exit unlock_miracle_key_door(Z &z) {
    z.test_bit_at_hl(0x268E, 7);
    if (!z.relative_if(0x2690, !z.zero())) {
        z.and_immediate(0x2692);
        z.compare_immediate(0x2694);
        z.load_immediate(0x2696, z.r.b);
        bool accepted = z.relative_if(0x2698, z.zero());
        if (!accepted) {
            z.compare_immediate(0x269A);
            if (!z.relative_if(0x269C, !z.zero())) {
                z.load_immediate(0x269E, z.r.b);
                accepted = true;
            }
        }
        if (accepted) {
            z.copy(0x26A0, z.r.a, z.r.b);
            z.store_a_direct(0x26A1);
            return continues(0x26A4, k::jump_cycles);
        }
    }
    z.load_hl(0x26A7);
    return continues(0x26AA, k::call_cycles);
}
template <class Z> Exit refuse_miracle_key_door(Z &z) {
    z.load_hl(0x26A7);
    return continues(0x26AA, k::call_cycles);
}
template <class Z> Exit announce_unusable_item(Z &z) {
    z.load_hl(0x26B0);
    return continues(0x26B3, k::call_cycles);
}
template <class Z> Exit report_unusable_item(Z &z) {
    z.load_a_direct(0x26B6);
    z.or_a(0x26B9);
    z.load_hl(0x26BA);
    if (!z.relative_if(0x26BD, !z.zero()))
        z.load_hl(0x26BF);
    return continues(0x26C2, k::call_cycles);
}
} // namespace
void register_items_story(RoutineRegistry &registry) {
    using k::conditional_at;
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };
    add(0x242F, 0x242F, {core_final_at(0x2432, 0)}, {},
        [](auto &z) { return announce_alsulin(z); });
    add(0x2435, 0x242F,
        {core_final_at(0x244B, 0), core_final_at(0x245C, 0), core_final_at(0x243E, 0)}, {},
        [](auto &z) { return check_alsulin_battle(z); });
    add(0x2444, 0x242F, {core_final_at(0x245C, 0), core_final_at(0x244B, 0)}, {},
        [](auto &z) { return check_alsulin_scene(z); });
    add(0x244E, 0x242F, {core_final_at(0x2456, 0)}, {},
        [](auto &z) { return report_alsulin_wrong_scene(z); });
    add(0x245F, 0x242F, {core_final_at(0x2456, 0), core_final_at(0x2464, 0)}, {},
        [](auto &z) { return check_alsulin_party(z); });
    add(0x246D, 0x242F, {core_final_at(0x2479, 0)}, {},
        [](auto &z) { return restore_odin_equipment(z); });
    add(0x247C, 0x242F, {returned_at(0x2490)}, {},
        [](auto &z) { return finish_odin_restoration(z); });
    add(0x2491, 0x2491, {core_final_at(0x2494, 0)}, {},
        [](auto &z) { return announce_polymaterial(z); });
    add(0x2497, 0x2491,
        {core_final_at(0x24CC, 0), core_final_at(0x24AD, 0), core_final_at(0x24A0, 0)}, {},
        [](auto &z) { return check_polymaterial_battle(z); });
    add(0x24A6, 0x2491, {core_final_at(0x24CC, 0), core_final_at(0x24AD, 0)}, {},
        [](auto &z) { return check_polymaterial_scene(z); });
    add(0x24B0, 0x2491, {core_final_at(0x24B8, 0)}, {},
        [](auto &z) { return report_polymaterial_wrong_scene(z); });
    add(0x24BE, 0x24BE, {returned_at(0x24CB)}, {}, [](auto &z) { return check_human_records(z); });
    add(0x24CF, 0x2491, {core_final_at(0x24DD, 0), core_final_at(0x24D4, 0)}, {},
        [](auto &z) { return check_polymaterial_party(z); });
    add(0x24DA, 0x2491, {core_final_at(0x24DD, 0)}, {},
        [](auto &z) { return report_polymaterial_success(z); });
    add(0x24E9, 0x24E9, {core_final_at(0x2501, 0), core_final_at(0x24F2, 0)}, {},
        [](auto &z) { return check_dungeon_key_context(z); });
    add(0x24EF, 0x24E9, {core_final_at(0x24F2, 0)}, {},
        [](auto &z) { return announce_key_outside(z); });
    add(0x24F5, 0x24E9, {core_final_at(0x24F8, 0)}, {},
        [](auto &z) { return report_key_outside(z); });
    add(0x2504, 0x24E9, {core_final_at(0x2506, 0)}, {},
        [](auto &z) { return find_dungeon_key_door(z); });
    add(0x2509, 0x24E9, {core_final_at(0x2518, 0), core_final_at(0x251E, 0)}, {},
        [](auto &z) { return unlock_dungeon_key_door(z); });
    add(0x251B, 0x24E9, {core_final_at(0x251E, 0)}, {},
        [](auto &z) { return refuse_dungeon_key_door(z); });
    add(0x2527, 0x2524, {core_final_at(0x252A, 0)}, {}, [](auto &z) { return announce_sphere(z); });
    add(0x252D, 0x2524, {conditional_at(0x2531, 0)}, {},
        [](auto &z) { return choose_sphere_effect(z); });
    add(0x2537, 0x2537, {core_final_at(0x253A, 0)}, {},
        [](auto &z) { return announce_eclipse_torch(z); });
    add(0x253D, 0x2537,
        {core_final_at(0x255D, -2), core_final_at(0x2556, 0), core_final_at(0x2546, 0)},
        {.scratch_stack = 2}, [](auto &z) { return check_torch_battle(z); });
    add(0x254C, 0x2537, {core_final_at(0x255D, -2), core_final_at(0x2556, 0)}, {.scratch_stack = 2},
        [](auto &z) { return check_torch_scene(z); });
    add(0x255C, 0x2537, {core_final_at(0x255D, -2)}, {.scratch_stack = 2},
        [](auto &z) { return flash_torch_palette(z); });
    add(0x2560, 0x2537, {core_final_at(0x2563, 2)}, {.inherited_stack = 2},
        [](auto &z) { return find_torch_pot(z); });
    add(0x2566, 0x2537, {core_final_at(0x2571, 0), core_final_at(0x256B, 0)}, {},
        [](auto &z) { return check_torch_pot(z); });
    add(0x2574, 0x2537, {core_final_at(0x2577, 0)}, {},
        [](auto &z) { return report_torch_success(z); });
    add(0x257D, 0x2537, {core_final_at(0x2582, 0)}, {},
        [](auto &z) { return find_nuts_reward(z); });
    add(0x2589, 0x2589, {core_final_at(0x258C, 0)}, {},
        [](auto &z) { return announce_aero_prism(z); });
    add(0x258F, 0x2589,
        {core_final_at(0x25A8, 0), core_final_at(0x25B8, 0), core_final_at(0x2598, 0)}, {},
        [](auto &z) { return check_prism_battle(z); });
    add(0x259E, 0x2589, {core_final_at(0x25A8, 0), core_final_at(0x25B8, 0)}, {},
        [](auto &z) { return check_prism_scene(z); });
    add(0x25AE, 0x2589, {core_final_at(0x25A8, 0), core_final_at(0x25B8, 0)}, {},
        [](auto &z) { return check_prism_phase(z); });
    add(0x25BB, 0x2589, {core_final_at(0x25C0, 0)}, {},
        [](auto &z) { return request_prism_effect(z); });
    add(0x25C3, 0x25C3, {core_final_at(0x25DA, 0), core_final_at(0x25F9, 0)}, {},
        [](auto &z) { return check_nuts_conditions(z); });
    add(0x25D7, 0x25C3, {core_final_at(0x25DA, 0)}, {},
        [](auto &z) { return announce_nuts_refusal(z); });
    add(0x25DD, 0x25C3, {core_final_at(0x25E9, 0)}, {},
        [](auto &z) { return report_nuts_refusal(z); });
    add(0x25EF, 0x25C3, {core_final_at(0x25DA, 0), core_final_at(0x25F9, 0)}, {},
        [](auto &z) { return check_nuts_phase(z); });
    add(0x25FF, 0x25C3, {returned_at(0x2604)}, {},
        [](auto &z) { return request_myau_transform(z); });
    add(0x2605, 0x25C3, {core_final_at(0x25DA, 0), core_final_at(0x25F9, 0)}, {},
        [](auto &z) { return check_nuts_castle(z); });
    add(0x2613, 0x2613, {core_final_at(0x262B, 0), core_final_at(0x261C, 0)}, {},
        [](auto &z) { return check_hapsby_battle(z); });
    add(0x261F, 0x2613, {core_final_at(0x2622, 0)}, {},
        [](auto &z) { return report_battle_hapsby(z); });
    add(0x2631, 0x2631, {core_final_at(0x263A, 0), core_final_at(0x2658, 0)}, {},
        [](auto &z) { return check_compass_dungeon(z); });
    add(0x263D, 0x2631, {core_final_at(0x2649, 0)}, {},
        [](auto &z) { return report_compass_refusal(z); });
    add(0x264F, 0x2631, {core_final_at(0x263A, 0), core_final_at(0x2658, 0)}, {},
        [](auto &z) { return check_compass_battle(z); });
    add(0x265B, 0x2631, {core_final_at(0x2676, 0)}, {},
        [](auto &z) { return report_compass_direction(z); });
    add(0x267C, 0x267C, {conditional_at(0x2680, 0)}, {},
        [](auto &z) { return check_miracle_key_context(z); });
    add(0x2683, 0x267C, {core_final_at(0x2686, 0)}, {},
        [](auto &z) { return announce_miracle_key(z); });
    add(0x2689, 0x267C, {core_final_at(0x268B, 0)}, {},
        [](auto &z) { return find_miracle_key_door(z); });
    add(0x268E, 0x267C, {core_final_at(0x26A4, 0), core_final_at(0x26AA, 0)}, {},
        [](auto &z) { return unlock_miracle_key_door(z); });
    add(0x26A7, 0x267C, {core_final_at(0x26AA, 0)}, {},
        [](auto &z) { return refuse_miracle_key_door(z); });
    add(0x26B0, 0x26B0, {core_final_at(0x26B3, 0)}, {},
        [](auto &z) { return announce_unusable_item(z); });
    add(0x26B6, 0x26B0, {core_final_at(0x26C2, 0)}, {},
        [](auto &z) { return report_unusable_item(z); });
}
} // namespace psmodern::game
