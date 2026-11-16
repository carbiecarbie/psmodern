// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_view.h"
#include "game/map_kit.h"

namespace psmodern::game::dungeon_view {
namespace {
namespace k=map_kit;
template<class Logic>
void add(RoutineRegistry& registry,std::uint16_t pc,std::vector<SegmentExit> exits,
         k::Permissions permissions,Logic logic) {
    k::add_segment(registry,{1,pc},k::segment(frame,std::move(exits),permissions),logic);
}
// Six bytes per view frame identify a graphics bank and pointer followed by
// a tilemap bank and pointer. The odd/even frame selects the VRAM half.
template<class Z>
k::Exit select_graphics(Z& z) {
    auto& r=z.r;
    z.copy(0x6AFC,r.c,r.a);
    z.add_register(0x6AFD,r.a);
    z.copy(0x6AFE,r.b,r.a);
    z.add_register(0x6AFF,r.a);
    z.add_register(0x6B00,r.b);
    z.load_hl(0x6B01);
    z.add_register(0x6B04,r.l);
    z.copy(0x6B05,r.l,r.a);
    z.add_with_carry(0x6B06,r.h);
    z.subtract_register(0x6B07,r.l);
    z.copy(0x6B08,r.h,r.a);
    z.push(0x6B09,r.b,r.c);
    z.load_from_hl(0x6B0A,r.a);
    z.store_a_direct(0x6B0B);
    z.increment_hl(0x6B0E);
    z.load_from_hl(0x6B0F,r.a);
    z.increment_hl(0x6B10);
    z.push(0x6B11,r.h,r.l);
    z.load_from_hl(0x6B12,r.h);
    z.copy(0x6B13,r.l,r.a);
    z.rotate_right_through_carry(0x6B14,r.c);
    z.load_immediate(0x6B16,r.d);
    return k::relative_if_exit(0x6B18,!z.carry());
}
template<class Z>
k::Exit select_tilemap(Z& z) {
    auto& r=z.r;
    z.copy(0x6B45,r.c,r.a);
    z.add_register(0x6B46,r.a);
    z.copy(0x6B47,r.e,r.a);
    z.add_register(0x6B48,r.a);
    z.add_register(0x6B49,r.e);
    z.copy(0x6B4A,r.e,r.a);
    z.load_immediate(0x6B4B,r.d);
    z.load_hl(0x6B4D);
    z.add_to_hl(0x6B50,z.de());
    z.push(0x6B51,r.b,r.c);
    z.load_from_hl(0x6B52,r.a);
    z.store_a_direct(0x6B53);
    z.increment_hl(0x6B56);
    read_pointer(z,0x6B57);
    return k::continues(0x6B5B,k::call_cycles);
}
template<class Z>
void standing_frame(Z& z,std::uint16_t pc) {
    z.load_immediate(pc,z.r.a);
    if (!z.relative_if(pc+2,z.zero())) z.load_immediate(pc+4,z.r.a);
}
}
void register_animation(RoutineRegistry& registry) {
    for (const std::uint16_t pc:{0x6A58,0x6A69}) {
        k::add_segment(registry,{1,pc},k::segment(animation,{k::returned_at(0x6A6C),k::core_final_at(0x6A6E,-2)},
            {.scratch_stack=4}),[pc](auto& z) {
                if (pc==0x6A58) {
                    z.copy(0x6A58,z.r.l,z.r.a);
                    z.load_immediate(0x6A59,z.r.h);
                    z.add_to_hl(0x6A5B,z.hl());
                    z.load_de(0x6A5C);
                    z.add_to_hl(0x6A5F,z.de());
                    read_pointer(z,0x6A60);
                    z.load_immediate(0x6A64,z.r.a);
                    z.store_a_direct(0x6A66);
                }
                z.load_from_hl(0x6A69,z.r.a);
                z.compare_immediate(0x6A6A);
                if (z.zero()) return k::returns(0x6A6C,k::taken_conditional_return_cycles);
                z.return_not_taken(0x6A6C);
                z.push(0x6A6D,z.r.h,z.r.l);
                return k::continues(0x6A6E,k::call_cycles);
            });
    }
    k::add_segment(registry,{1,0x6A71},k::segment(animation,{k::core_final_at(0x6A73,2)},
        {.inherited_stack=2}),[](auto& z) {
            z.pop(0x6A71,z.r.h,z.r.l);
            z.increment_hl(0x6A72);
            return k::continues(0x6A73,k::jump_cycles);
        });
    auto draw=k::segment(animation,{k::core_final_at(0x6AE5)},{.scratch_stack=2});
    draw.single_instruction=true;
    registry.add({1,0x6AE5},std::move(draw),[](const Machine&){return k::call_cycles;},
        [](RoutineContext& c){c.finish_core(0x6AE5);});
    k::add_segment(registry,{1,0x6AE8},k::segment(animation,{k::core_final_at(0x6AEA)}),[](auto& z) {
        z.load_immediate(0x6AE8,z.r.a);
        return k::continues(0x6AEA,k::jump_cycles);
    });
    k::add_segment(registry,floor_music,k::segment(floor_music,{k::core_final_at(0x6DF4)},
        {.slot2_mapper=true}),[](auto& z) {
            z.load_hl(0x6DE2);
            z.store_to_hl(0x6DE5);
            z.load_a_direct(0x6DE7);
            z.add_register(0x6DEA,z.r.a);
            z.add_register(0x6DEB,z.r.a);
            z.copy(0x6DEC,z.r.l,z.r.a);
            z.load_immediate(0x6DED,z.r.h);
            z.load_de(0x6DEF);
            z.add_to_hl(0x6DF2,z.de());
            z.load_from_hl(0x6DF3,z.r.a);
            return k::continues(0x6DF4,k::jump_cycles);
        });
}
void register_frame(RoutineRegistry& registry) {
    add(registry,0x6AED,{k::core_final_at(0x6AF3),k::conditional_at(0x6B18,-4)},
        {.slot2_mapper=true,.scratch_stack=4},[](auto& z) {
            z.and_immediate(0x6AED);
            if (z.relative_if(0x6AEF,!z.zero())) return select_graphics(z);
            z.load_immediate(0x6AF1,z.r.b);
            return k::continues(0x6AF3,k::call_cycles);
        });
    for (auto pc:{0x6AF6,0x6AFC})
        add(registry,pc,{k::conditional_at(0x6B18,-4)},{.slot2_mapper=true,.scratch_stack=4},[pc](auto& z) {
            if (pc==0x6AF6) standing_frame(z,pc);
            return select_graphics(z);
        });
    const SegmentExit disable{.kind=RoutineExit::core_final,.address=0x6B1C,.interrupt_mask=InterruptMask::disable};
    add(registry,0x6B1A,{disable},{},[](auto& z) {
        z.load_immediate(0x6B1A,z.r.d);
        return k::continues(0x6B1C,4);
    });
    auto single=k::segment(frame,{disable});
    single.single_instruction=true;
    registry.add({1,0x6B1C},std::move(single),[](const Machine&){return 4;},
        [](RoutineContext& context){context.finish_core(0x6B1C);});
    const SegmentExit enable{.kind=RoutineExit::core_final,.address=0x6B23,.interrupt_mask=InterruptMask::enable};
    auto write_address=[](auto& z) {
        z.xor_a(0x6B1D);
        z.output(0x6B1E);
        z.copy(0x6B20,z.r.a,z.r.d);
        z.output(0x6B21);
        return k::continues(0x6B23,4);
    };
    registry.add({1,0x6B1D},k::segment(frame,{enable}),
        [=](const Machine& m) {
            const auto high=m.cpu().de.uint16_value>>8;
            if (high!=0x40 && high!=0x60) return k::uncertain_maximum;
            return k::cost(m,write_address,k::uncertain_maximum);
        },[=](RoutineContext& c) { k::run(c,write_address); });
    // The original CALL at 6B24 is the instruction protected by EI's delay.
    // The normal machine step executes it, with the boundary hook available.
    add(registry,0x6B27,{k::core_final_at(0x6B32,2)},
        {.slot2_mapper=true,.inherited_stack=2},[](auto& z) {
            z.pop(0x6B27,z.r.h,z.r.l);
            z.increment_hl(0x6B28);
            z.load_from_hl(0x6B29,z.r.a);
            z.store_a_direct(0x6B2A);
            z.increment_hl(0x6B2D);
            read_pointer(z,0x6B2E);
            return k::continues(0x6B32,k::call_cycles);
        });
    add(registry,0x6B35,{k::core_final_at(0x6B36,2)},{.inherited_stack=2},[](auto& z) {
        z.pop(0x6B35,z.r.b,z.r.c);
        return k::continues(0x6B36,k::call_cycles);
    });
    add(registry,0x6B39,{k::returned_at(0x6B39)},{},[](auto&) { return k::returns(0x6B39,k::return_cycles); });
    add(registry,0x6B3A,{k::core_final_at(0x6B3C)},{.scratch_stack=2},[](auto& z) {
        z.load_immediate(0x6B3A,z.r.b);
        return k::continues(0x6B3C,k::call_cycles);
    });
    for (auto pc:{0x6B3F,0x6B45})
        add(registry,pc,{k::core_final_at(0x6B5B,-2)},{.slot2_mapper=true,.scratch_stack=4},[pc](auto& z) {
            if (pc==0x6B3F) standing_frame(z,pc);
            return select_tilemap(z);
        });
    add(registry,0x6B5E,{k::core_final_at(0x6B5F,2)},{.inherited_stack=2},[](auto& z) {
        z.pop(0x6B5E,z.r.b,z.r.c);
        return k::continues(0x6B5F,k::jump_cycles);
    });
}
}
