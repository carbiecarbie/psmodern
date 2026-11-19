// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_view.h"
#include "game/map_kit.h"

namespace psmodern::game::dungeon_view {
namespace {
namespace k=map_kit;
template<class Logic>
void add(RoutineRegistry& registry,std::uint16_t pc,std::vector<SegmentExit> exits,
         k::Permissions permissions,Logic logic) {
    k::add_segment(registry,{1,pc},k::segment(overlays,std::move(exits),permissions),logic);
}
template<class Z>
k::Exit close_panel(Z& z) {
    auto& r=z.r;
    z.load_hl(0x6E4C);
    z.store_to_hl(0x6E4F);
    z.add_register(0x6E51,r.a);
    z.load_hl(0x6E52);
    z.add_register(0x6E55,r.l);
    z.copy(0x6E56,r.l,r.a);
    z.add_with_carry(0x6E57,r.h);
    z.subtract_register(0x6E58,r.l);
    z.copy(0x6E59,r.h,r.a);
    read_pointer(z,0x6E5A);
    z.load_de(0x6E5E);
    z.load_bc(0x6E61);
    return k::fall_through(0x6E61,0x6E64);
}
}
void register_overlays(RoutineRegistry& registry) {
    // A fake wall can have a banked animation. Each row names its
    // destination and length. Only opaque entries change the tilemap.
    add(registry,0x6E04,{k::returned_at(0x6E08),k::core_final_at(0x6E1F)},
        {.slot2_mapper=true},[](auto& z) {
            z.load_a_direct(0x6E04);
            z.or_a(0x6E07);
            if (auto exit=z.final_return_if(0x6E08,k::Condition::z)) return *exit;
            z.increment(0x6E09,z.r.a);
            z.store_a_direct(0x6E0A);
            z.load_hl(0x6E0D);
            z.store_to_hl(0x6E10);
            z.load_immediate(0x6E12,z.r.h);
            z.copy(0x6E14,z.r.l,z.r.a);
            z.add_to_hl(0x6E15,z.hl());
            z.load_de(0x6E16);
            z.add_to_hl(0x6E19,z.de());
            read_pointer(z,0x6E1A);
            z.load_from_hl(0x6E1E,z.r.b);
            z.increment_hl(0x6E1F);
            return k::fall_through(0x6E1F,0x6E20);
        });
    add(registry,0x6E20,{k::core_final_at(0x6E26,-2)},{.scratch_stack=2},[](auto& z) {
        z.push(0x6E20,z.r.b,z.r.c);
        z.load_from_hl(0x6E21,z.r.e);
        z.increment_hl(0x6E22);
        z.load_from_hl(0x6E23,z.r.d);
        z.increment_hl(0x6E24);
        z.load_from_hl(0x6E25,z.r.b);
        z.increment_hl(0x6E26);
        return k::fall_through(0x6E26,0x6E27);
    });
    auto paint=[](auto& z) {
        z.load_from_hl(0x6E27,z.r.a);
        z.add_immediate(0x6E28);
        z.compare_immediate(0x6E2A);
        if (!z.relative_if(0x6E2C,z.carry())) z.store_a_to_pair(0x6E2E,z.de());
        z.increment_pair(0x6E2F,z.r.d,z.r.e);
        z.increment_pair(0x6E30,z.r.d,z.r.e);
        z.increment_hl(0x6E31);
        return z.final_decrement_jump(0x6E32);
    };
    // The tile store never targets the cartridge control registers: the
    // overlay is drawn into the tilemap buffer in work RAM. DE is a register,
    // so the check holds across a delivery inside the body.
    const k::Guard below_registers{"DE is below the cartridge control registers",[](const Machine& m) {
        return m.cpu().de.uint16_value<0xFFFC;
    }};
    k::add_segment(registry,{1,0x6E27},k::segment(overlays,{k::conditional_at(0x6E32)}),below_registers,paint);
    add(registry,0x6E34,{k::conditional_at(0x6E35,2)},{.inherited_stack=2},[](auto& z) {
        z.pop(0x6E34,z.r.b,z.r.c);
        return z.final_decrement_jump(0x6E35);
    });
    for (auto pc:{0x6E37,0x6E74})
        add(registry,pc,{k::returned_at(pc)},{},[pc](auto& z) { return z.final_return(pc); });
    add(registry,0x6E38,{k::core_final_at(0x6E3A)},{.scratch_stack=2},[](auto& z) {
        z.load_immediate(0x6E38,z.r.b);
        return z.final_call(0x6E3A);
    });
    add(registry,0x6E3D,{k::conditional_at(0x6E41)},{},[](auto& z) {
        z.and_immediate(0x6E3D);
        z.compare_immediate(0x6E3F);
        return z.final_relative_if(0x6E41,k::Condition::z);
    });
    add(registry,0x6E43,{k::returned_at(0x6E45),k::core_final_at(0x6E61)},{.slot2_mapper=true},[](auto& z) {
        z.subtract_immediate(0x6E43);
        if (auto exit=z.final_return_if(0x6E45,k::Condition::c)) return *exit;
        z.test_bit_at_hl(0x6E46,7);
        if (!z.relative_if(0x6E48,z.zero())) z.add_immediate(0x6E4A);
        return close_panel(z);
    });
    add(registry,0x6E4C,{k::core_final_at(0x6E61)},{.slot2_mapper=true},[](auto& z) { return close_panel(z); });
    // Copy each rectangle row with the shared interruptible transfer. The
    // destination advances one 64-byte screen row; the source stays packed.
    add(registry,0x6E64,{k::core_final_at(0x6E66,-4)},{.scratch_stack=4},[](auto& z) {
        z.push(0x6E64,z.r.b,z.r.c);
        z.push(0x6E65,z.r.d,z.r.e);
        z.load_immediate(0x6E66,z.r.b);
        return k::fall_through(0x6E66,0x6E68);
    });
    add(registry,0x6E6A,{k::conditional_at(0x6E72,4)},{.inherited_stack=4},[](auto& z) {
        z.exchange_de_hl(0x6E6A);
        z.pop(0x6E6B,z.r.h,z.r.l);
        z.load_bc(0x6E6C);
        z.add_to_hl(0x6E6F,z.bc());
        z.exchange_de_hl(0x6E70);
        z.pop(0x6E71,z.r.b,z.r.c);
        return z.final_decrement_jump(0x6E72);
    });
}
}
