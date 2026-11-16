// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_view.h"
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game::dungeon_view {
namespace {
namespace k=map_kit;
void call(RoutineRegistry& registry,RoutineEntry unit,std::uint16_t pc,bool observed=false) {
    auto spec=k::segment(unit,{k::core_final_at(pc)},{.scratch_stack=2});
    spec.single_instruction=true;
    spec.observe_entry=observed;
    registry.add({0,pc},std::move(spec),[](const Machine&){return k::call_cycles;},
                 [pc](RoutineContext& context){context.finish_core(pc);});
}
}

void register_load(RoutineRegistry& registry) {
    // The host samples the player's position before the walking handler.
    call(registry,dungeon_check_unit,0x0F48,true);
    call(registry,dungeon_check_unit,0x0F72);
    k::add_segment(registry,{0,0x0F7C},k::segment(dungeon_check_unit,{k::returned_at(0x0F7C)}),
                   [](auto&){return k::returns(0x0F7C,k::return_cycles);});

    for (const std::uint16_t pc:{0x0F7D,0x0F80,0x0FAA,0x0FD6,0x0FD9}) call(registry,load,pc);
    auto add=[&](std::uint16_t pc,std::vector<SegmentExit> exits,k::Permissions permissions,auto logic) {
        k::add_segment(registry,{0,pc},k::segment(load,std::move(exits),permissions),logic);
    };
    // Advance to the walking mode and upload the two sets of dungeon graphics.
    for (const std::uint16_t pc:{0x0F83,0x0F95}) {
        const auto graphics=static_cast<std::uint16_t>(pc==0x0F83 ? 0x0F8C : 0x0F95);
        add(pc,{k::core_final_at(graphics+6)},{.slot2_mapper=pc==0x0F83,.scratch_stack=2},
            [pc,graphics](auto& z) {
                if (pc==0x0F83) {
                    z.load_hl(0x0F83);
                    z.increment_at_hl(0x0F86);
                    z.load_hl(0x0F87);
                    z.store_to_hl(0x0F8A);
                }
                z.load_hl(graphics);
                z.load_de(graphics+3);
                return k::continues(graphics+6,k::call_cycles);
            });
    }
    add(0x0F9E,{k::core_final_at(0x0FA0)},{.scratch_stack=2},[](auto& z) {
        z.load_immediate(0x0F9E,z.r.a);
        return k::continues(0x0FA0,k::call_cycles);
    });
    add(0x0FA3,{k::core_final_at(0x0FAA)},{.scratch_stack=2},[](auto& z) {
        if (!z.relative_if(0x0FA3,!z.zero())) {
            z.load_immediate(0x0FA5,z.r.a);
            z.store_a_direct(0x0FA7);
        }
        return k::continues(0x0FAA,k::call_cycles);
    });
    auto mask=k::core_final_at(0x0FC0);
    mask.interrupt_mask=InterruptMask::disable;
    add(0x0FAD,{mask},{},[](auto& z) {
        z.xor_a(0x0FAD);
        for (auto pc:{0x0FAE,0x0FB1,0x0FB4,0x0FB7,0x0FBA}) z.store_a_direct(pc);
        z.load_de(0x0FBD);
        return k::continues(0x0FC0,4);
    });
    auto single=[&](std::uint16_t pc,SegmentExit exit,unsigned cycles,k::Permissions permissions={}) {
        auto spec=k::segment(load,{exit},permissions);
        spec.single_instruction=true;
        registry.add({0,pc},std::move(spec),[cycles](const Machine&){return cycles;},
                     [pc](RoutineContext& c){c.finish_core(pc);});
    };
    single(0x0FC1,k::core_final_at(0x0FC1),k::restart_cycles,{.scratch_stack=2});
    auto enable=k::core_final_at(0x0FC2);
    enable.interrupt_mask=InterruptMask::enable;
    single(0x0FC2,enable,4);
    // EI protects the following CALL. The core steps it and then enters the sprite clear.
    add(0x0FC6,{k::core_final_at(0x0FC8)},{.scratch_stack=2},[](auto& z) {
        z.load_immediate(0x0FC6,z.r.b);
        return k::continues(0x0FC8,k::call_cycles);
    });
    add(0x0FCB,{k::returned_at(0x0FCF),k::core_final_at(0x0FD3)},{.scratch_stack=2},[](auto& z) {
        z.load_a_direct(0x0FCB);
        z.or_a(0x0FCE);
        if (!z.zero()) return k::returns(0x0FCF,k::taken_conditional_return_cycles);
        z.return_not_taken(0x0FCF);
        z.load_hl(0x0FD0);
        return k::continues(0x0FD3,k::call_cycles);
    });
    auto leave=[](auto& z) {
        z.load_hl(0x0FED);
        z.store_to_hl(0x0FF0);
        return k::returns(0x0FF2,k::return_cycles);
    };
    add(0x0FDC,{k::returned_at(0x0FF2),k::core_final_at(0x0FE7)},{.scratch_stack=2},[leave](auto& z) {
        z.load_a_direct(0x0FDC);
        z.or_a(0x0FDF);
        if (z.relative_if(0x0FE0,z.zero())) return leave(z);
        z.load_immediate(0x0FE2,z.r.a);
        z.store_a_direct(0x0FE4);
        return k::continues(0x0FE7,k::call_cycles);
    });
    single(0x0FEA,k::core_final_at(0x0FEA),k::jump_cycles);
    add(0x0FED,{k::returned_at(0x0FF2)},{},leave);

    // One seed followed by an overlapping 255-byte clear, then the sprite terminator.
    auto seed=k::core_final_at(0x0FFC);
    seed.wram_seed=0xC800;
    seed.seed_target=SeedTarget::sprite_shadow_zero;
    registry.add(refresh,k::segment(refresh,{seed}),[](const Machine& m) {
        return m.memory().mapping(0xC800).kind==Memory::Kind::ram ? 40u : k::uncertain_maximum;
    },[](RoutineContext& c) {
        k::run(c,[](auto& z) {
            z.load_hl(0x0FF3);
            z.load_de(0x0FF6);
            z.load_bc(0x0FF9);
            return k::continues(0x0FFC,10);
        });
    });
    register_block_transfer(registry,{0,0x0FFE});
    k::add_segment(registry,{0,0x1000},k::segment(refresh,{k::core_final_at(0x1005)},{.scratch_stack=2}),
        [](auto& z) {
            z.load_immediate(0x1000,z.r.a);
            z.store_a_direct(0x1002);
            return k::continues(0x1005,k::call_cycles);
        });
    k::add_segment(registry,{0,0x1008},k::segment(refresh,{k::core_final_at(0x100C)}),[](auto& z) {
        z.xor_a(0x1008);
        z.store_a_direct(0x1009);
        return k::continues(0x100C,k::jump_cycles);
    });
}
}
