// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_kit.h"
#include "game/dungeon_view.h"

namespace psmodern::game::dungeon_view {
namespace {
namespace k=kit;

template<class Logic>
void add(RoutineRegistry& registry, std::uint16_t pc, std::vector<SegmentExit> exits,
         k::Permissions permissions, Logic logic) {
    k::add_segment(registry,{1,pc},k::segment(cone,std::move(exits),permissions),
                   2*Machine::frame_cycles,logic);
}

template<class Z>
k::Exit start_cone(Z& z) {
    if (auto exit=z.final_return_if(0x6C4C,k::Condition::nc)) return *exit;
    auto& r=z.r;
    z.load_hl(0x6C4D);
    z.store_to_hl(0x6C50);
    // Each animated frame owns three sets of 26 bytes of drawing records.
    z.add_register(0x6C52,r.a);
    z.copy(0x6C53,r.l,r.a);
    z.add_register(0x6C54,r.a);
    z.add_register(0x6C55,r.a);
    z.copy(0x6C56,r.h,r.a);
    z.add_register(0x6C57,r.a);
    z.add_register(0x6C58,r.h);
    z.add_register(0x6C59,r.l);
    z.copy(0x6C5A,r.l,r.a);
    z.load_immediate(0x6C5B,r.h);
    z.copy(0x6C5D,r.e,r.l);
    z.copy(0x6C5E,r.d,r.h);
    z.add_to_hl(0x6C5F,z.hl());
    z.add_to_hl(0x6C60,z.de());
    z.load_de(0x6C61);
    z.add_to_hl(0x6C64,z.de());
    z.load_immediate(0x6C65,r.b);
    return z.final_call(0x6C67);
}

// For a blocked center cell the cartridge paints the front, then both
// sides and stops. An open center skips the front records and continues
// to the next distance after drawing its two sides.
template<class Z>
k::Exit open_center(Z& z, std::uint16_t at) {
    z.load_de(at);
    z.add_to_hl(at+3,z.de());
    z.load_immediate(at+4,z.r.b);
    return z.final_call(at+6);
}

template<class Z>
k::Exit left_panel(Z& z, std::uint16_t at) {
    z.load_from_hl(at,z.r.b);
    z.increment_hl(at+1);
    z.load_from_hl(at+2,z.r.c);
    z.increment_hl(at+3);
    z.push(at+4,z.r.b,z.r.c);
    return z.final_call(at+5);
}

void add_depth(RoutineRegistry& registry,std::uint16_t base) {
    const auto open=base+0x1B;
    // The cell reader leaves its zero flag for this branch. Returning to
    // the actual open-panel entry gives the IRQ a boundary before setup.
    add(registry,open,{k::core_final_at(open+6)},{.scratch_stack=2},
        [=](auto& z) { return open_center(z,open); });
    const auto blocked_left=base+5, open_left=open+9;
    add(registry,blocked_left,{k::core_final_at(blocked_left+10,-2)},{.scratch_stack=4},
        [=](auto& z) {
            z.load_immediate(blocked_left,z.r.b);
            if (!k::inline_reader(z,true,blocked_left+2)) return z.final_call(blocked_left+10);
            return left_panel(z,blocked_left+5);
        });
    add(registry,open_left,{k::core_final_at(open_left+5,-2)},{.scratch_stack=4},
        [=](auto& z) { return left_panel(z,open_left); });
    for (bool is_open:{false,true}) {
        const auto right=base+(is_open ? 0x2C : 0x12);
        const auto final=right+6;
        const bool calls=is_open && base!=depth.back();
        add(registry,right,{k::core_final_at(final,2)},{.scratch_stack=4,.inherited_stack=2},
            [=](auto& z) {
                z.load_immediate(right,z.r.b);
                if (k::inline_reader(z,true,right+2)) z.pop(right+5,z.r.b,z.r.c);
                return calls ? z.final_call(final) : z.final_jump(final);
            });
    }
    // Joining stretches: the places the original resumes after a cell reader
    // or a call returned to it, when the stretch before them was not run as
    // one segment. The open-or-blocked decision after the centre cell:
    auto single=[&](std::uint16_t pc,SegmentExit exit,k::Permissions permissions,auto logic) {
        auto spec=k::segment(cone,{exit},permissions);
        spec.single_instruction=true;
        k::add_segment(registry,{1,pc},std::move(spec),2*Machine::frame_cycles,logic);
    };
    single(base,k::conditional_at(base),{},[=](auto& z) { return z.final_relative_if(base,k::Condition::z); });
    // A blocked centre starts drawing: the descriptor of its front panel.
    single(base+2,k::core_final_at(base+2),{.scratch_stack=2},[=](auto& z) { return z.final_call(base+2); });
    // The left panel after its cell reader came back.
    add(registry,blocked_left+5,{k::core_final_at(blocked_left+10,-2)},{.scratch_stack=4},
        [=](auto& z) { return left_panel(z,blocked_left+5); });
    // The right panel's saved size is restored, then its descriptor is drawn.
    add(registry,base+0x17,{k::core_final_at(base+0x18,2)},{.inherited_stack=2},[=](auto& z) {
        z.pop(base+0x17,z.r.b,z.r.c);
        return z.final_jump(base+0x18);
    });
    add(registry,base+0x31,{k::core_final_at(base+0x32,2)},{.inherited_stack=2},[=](auto& z) {
        z.pop(base+0x31,z.r.b,z.r.c);
        return base!=depth.back() ? z.final_call(base+0x32) : z.final_jump(base+0x32);
    });
}

// A panel descriptor supplies its destination, size and pointer to tiles.
// Some paths reuse the size already saved by the left-hand panel.
template<class Z>
k::Exit panel_descriptor(Z& z,std::uint16_t at) {
    z.load_from_hl(at,z.r.e);
    z.increment_hl(at+1);
    z.load_from_hl(at+2,z.r.d);
    z.increment_hl(at+3);
    auto cursor=static_cast<std::uint16_t>(at+4);
    if (at==0x6D21) {
        z.load_from_hl(cursor++,z.r.b);
        z.increment_hl(cursor++);
        z.load_from_hl(cursor++,z.r.c);
        z.increment_hl(cursor++);
    } else if (at==0x6D34) {
        z.increment_hl(cursor++);
        z.increment_hl(cursor++);
    }
    z.load_from_hl(cursor++,z.r.a);
    z.increment_hl(cursor++);
    z.push(cursor++,z.r.h,z.r.l);
    z.load_from_hl(cursor++,z.r.h);
    z.copy(cursor++,z.r.l,z.r.a);
    return at==0x6D21 ? z.final_call_if(cursor,k::Condition::nz) : z.final_call_if(cursor,k::Condition::z);
}
}

void register_cone(RoutineRegistry& registry) {
    add(registry,0x6C46,{k::conditional_at(0x6C49)},{},[](auto& z) {
        z.copy(0x6C46,z.r.a,z.r.c);
        z.compare_immediate(0x6C47);
        return z.final_jump_if(0x6C49,k::Condition::z);
    });
    add(registry,0x6C4C,{k::returned_at(0x6C4C),k::core_final_at(0x6C67)},
        {.slot2_mapper=true,.scratch_stack=2},[](auto& z) { return start_cone(z); });
    for (auto pc:{0x6C9F,0x6CD9}) {
        add(registry,pc,{k::core_final_at(pc+2)},{.scratch_stack=2},[pc](auto& z) {
            z.load_immediate(pc,z.r.b);
            return z.final_call(pc+2);
        });
    }
    for (auto base:depth) add_depth(registry,base);
    add(registry,0x6D13,{k::core_final_at(0x6D14,-2)},{.scratch_stack=4},[](auto& z) {
        z.push(0x6D13,z.r.a,z.r.f);
        return z.final_call(0x6D14);
    });
    add(registry,0x6D17,{k::core_final_at(0x6D2E)},{.scratch_stack=2,.inherited_stack=2},[](auto& z) {
        z.pop(0x6D17,z.r.a,z.r.f);
        z.compare_immediate(0x6D18);
        if (!z.relative_if(0x6D1A,z.zero())) {
            z.compare_immediate(0x6D1C);
            if (!z.relative_if(0x6D1E,!z.carry())) z.xor_a(0x6D20);
        }
        return panel_descriptor(z,0x6D21);
    });
    for (auto pc:{0x6D21,0x6D34,0x6D45}) {
        const auto final=pc==0x6D21 ? 0x6D2E : pc==0x6D34 ? 0x6D3F : 0x6D4E;
        add(registry,pc,{k::core_final_at(final,-2)},{.scratch_stack=4},
            [pc](auto& z) { return panel_descriptor(z,pc); });
    }
    for (auto pc:{0x6D31,0x6D42,0x6D51}) {
        const unsigned advance=pc==0x6D51 ? 3 : 1;
        const auto final=pc+advance+1;
        add(registry,pc,{k::returned_at(final,2)},{.inherited_stack=2},[=](auto& z) {
            z.pop(pc,z.r.h,z.r.l);
            for (unsigned i=1;i<=advance;++i) z.increment_hl(pc+i);
            return z.final_return(final);
        });
    }
}
}
