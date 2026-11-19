// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/battle_sprites.h"
#include "game/map_kit.h"

namespace psmodern::game::battle_sprites {
namespace {
namespace k=map_kit;
constexpr auto object=k::Index::iy;
template<class Logic>
void add(RoutineRegistry& registry,RoutineEntry unit,std::uint16_t pc,std::vector<SegmentExit> exits,
         k::Permissions permissions,Logic logic) {
    k::add_segment(registry,{1,pc},k::segment(unit,std::move(exits),permissions),logic);
}
void call(RoutineRegistry& registry,RoutineEntry unit,std::uint16_t pc) {
    auto spec=k::segment(unit,{k::core_final_at(pc)},{.scratch_stack=2});
    spec.single_instruction=true;
    k::add_segment(registry,{1,pc},std::move(spec),[](const Machine&){return k::call_cycles;},
                   [pc](auto& z){return z.final_call(pc);});
}
// The record holds a sound request, delay, first and last frames and a graphics pointer.
// The attack and projectile use different sources for their on-screen coordinates.
void record(RoutineRegistry& registry,RoutineEntry unit,std::uint16_t pc) {
    add(registry,unit,pc,{k::core_final_at(pc+19)},{},[pc](auto& z) {
        z.load_from_hl(pc,z.r.a);
        z.store_a_direct(pc+1);
        for (unsigned offset:{4u,9u,14u}) {
            z.increment_hl(pc+offset);
            z.load_from_hl(pc+offset+1,z.r.a);
            z.store_to_indexed(pc+offset+2,object,z.r.a);
        }
        z.increment_hl(pc+19);
        return k::fall_through(pc+19,pc+20);
    });
    pc+=20;
    add(registry,unit,pc,{k::core_final_at(pc+19)},{.scratch_stack=2},[pc](auto& z) {
        for (unsigned i:{0u,6u}) {
            z.load_a_direct(pc+i);
            z.store_to_indexed(pc+i+3,object,z.r.a);
        }
        z.load_from_hl(pc+12,z.r.a);
        z.increment_hl(pc+13);
        z.load_from_hl(pc+14,z.r.h);
        z.copy(pc+15,z.r.l,z.r.a);
        z.load_de(pc+16);
        return z.final_call(pc+19);
    });
    add(registry,unit,pc+22,{k::returned_at(pc+23)},{},[pc](auto& z) {
        z.xor_a(pc+22);
        return z.final_return(pc+23);
    });
}
// A signed timer controls frame changes. The last frame retires the object
// and bypasses the return to the outer update that normally reports FF.
void frames(RoutineRegistry& registry,RoutineEntry unit,std::uint16_t pc) {
    call(registry,unit,pc);
    add(registry,unit,pc+3,{k::returned_at(pc+5)},{},[pc](auto& z) {
        z.load_immediate(pc+3,z.r.a);
        return z.final_return(pc+5);
    });
    pc+=6;
    add(registry,unit,pc,{k::returned_at(pc+3),k::conditional_at(pc+17)},{},[pc](auto& z) {
        z.decrement_indexed(pc,object);
        if (auto exit=z.final_return_if(pc+3,k::Condition::p)) return *exit;
        z.load_from_indexed(pc+4,object,z.r.a);
        z.store_to_indexed(pc+7,object,z.r.a);
        z.load_from_indexed(pc+10,object,z.r.a);
        z.increment(pc+13,z.r.a);
        z.compare_indexed(pc+14,object);
        return z.final_relative_if(pc+17,k::Condition::nc);
    });
    add(registry,unit,pc+19,{k::returned_at(pc+22)},{},[pc](auto& z) {
        z.store_to_indexed(pc+19,object,z.r.a);
        return z.final_return(pc+22);
    });
}
}
void register_sprites(RoutineRegistry& registry) {
    add(registry,attack,0x5B87,{k::core_final_at(0x5B8B,-2)},{.scratch_stack=4},[](auto& z) {
        z.load_from_indexed(0x5B87,object,z.r.a);
        z.push(0x5B8A,z.r.a,z.r.f);
        return z.final_call(0x5B8B);
    });
    add(registry,attack,0x5B8E,{k::core_final_at(0x5BA1,2)},{.slot2_mapper=true,.inherited_stack=2},[](auto& z) {
        z.pop(0x5B8E,z.r.a,z.r.f);
        z.increment_indexed(0x5B8F,object);
        z.load_hl(0x5B92);
        z.store_to_hl(0x5B95);
        z.add_register(0x5B97,z.r.a);
        z.copy(0x5B98,z.r.e,z.r.a);
        z.add_register(0x5B99,z.r.a);
        z.add_register(0x5B9A,z.r.e);
        z.copy(0x5B9B,z.r.e,z.r.a);
        z.load_immediate(0x5B9C,z.r.d);
        z.load_hl(0x5B9E);
        z.add_to_hl(0x5BA1,z.de());
        return k::fall_through(0x5BA1,0x5BA2);
    });
    record(registry,attack,0x5BA2);
    frames(registry,attack,0x5BCE);
    add(registry,attack,0x5BEB,{k::returned_at(0x5BF0,2)},{.inherited_stack=2},[](auto& z) {
        z.xor_a(0x5BEB);
        z.store_to_indexed(0x5BEC,object,z.r.a);
        z.pop(0x5BEF,z.r.h,z.r.l);
        return z.final_return(0x5BF0);
    });

    call(registry,projectile,0x5E2E);
    add(registry,projectile,0x5E31,{k::conditional_at(0x5E41)},
        {.slot2_mapper=true},[](auto& z) {
            z.increment_indexed(0x5E31,object);
            z.load_hl(0x5E34);
            z.store_to_hl(0x5E37);
            z.load_a_direct(0x5E39);
            z.test_bit(0x5E3C,6,z.r.a);
            z.load_hl(0x5E3E);
            return z.final_relative_if(0x5E41,k::Condition::z);
        });
    auto alternate=k::segment(projectile,{k::core_final_at(0x5E43)});
    alternate.single_instruction=true;
    k::add_segment(registry,{1,0x5E43},std::move(alternate),[](const Machine&){return 10;},
                   [](auto& z){
                       z.load_hl(0x5E43);
                       return k::fall_through(0x5E43,0x5E46);
                   });
    record(registry,projectile,0x5E46);
    frames(registry,projectile,0x5E72);
    add(registry,projectile,0x5E8F,{k::conditional_at(0x5E99,2)},{.inherited_stack=2},[](auto& z) {
        z.xor_a(0x5E8F);
        z.store_to_indexed(0x5E90,object,z.r.a);
        z.pop(0x5E93,z.r.h,z.r.l);
        z.load_a_direct(0x5E94);
        z.and_immediate(0x5E97);
        return z.final_jump_if(0x5E99,k::Condition::z);
    });
    add(registry,projectile,0x5E9C,{k::returned_at(0x5EA1)},{},[](auto& z) {
        z.load_immediate(0x5E9C,z.r.a);
        z.store_a_direct(0x5E9E);
        return z.final_return(0x5EA1);
    });
}
}
