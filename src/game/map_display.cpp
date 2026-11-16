// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
void register_map_display(RoutineRegistry& registry) {
    namespace k=map_kit;
    const auto load=map::load_map_unit,ice=map::ice_probe_unit;
    auto bridge=[&](RoutineEntry unit,std::uint16_t pc,unsigned cycles,InterruptMask mask=InterruptMask::none) {
        auto exit=k::core_final_at(pc);
        exit.interrupt_mask=mask;
        auto spec=k::segment(unit,{exit},{.scratch_stack=mask==InterruptMask::none ? 2u : 0u});
        spec.single_instruction=true;
        registry.add({unit.bank,pc},std::move(spec),[cycles](const Machine&){return cycles;},
                     [pc](RoutineContext& c){c.finish_core(pc);});
    };
    for (const std::uint16_t pc:{0x0B6D,0x0BB4,0x0C91}) bridge(load,pc,4,InterruptMask::disable);
    for (const std::uint16_t pc:{0x0B71,0x0BB8,0x0C93}) bridge(load,pc,4,InterruptMask::enable);
    for (const std::uint16_t pc:{0x0B6A,0x0B6E,0x0BB5}) bridge(load,pc,k::call_cycles);
    const SegmentExit mask{.kind=RoutineExit::core_final,.address=0x0C91,.interrupt_mask=InterruptMask::disable};
    k::add_segment(registry,{0,0x0C8E},k::segment(load,{mask}),[](auto& z) {
        z.load_de(0x0C8E);
        return k::continues(0x0C91,4);
    });
    bridge(load,0x0C92,k::restart_cycles);

    // Breaking the ice replaces four tile bytes on each of two rows.
    // The coordinate helper supplies the VRAM address in DE.
    bridge(ice,0x75CC,4,InterruptMask::disable);
    for (const std::uint16_t pc:{0x75CD,0x75D0}) {
        k::add_segment(registry,{1,pc},k::segment(ice,{k::core_final_at(0x75D1,-2)},{.scratch_stack=4}),[=](auto& z) {
            if (pc==0x75CD) z.load_bc(0x75CD);
            z.push(0x75D0,z.r.b,z.r.c);
            return k::continues(0x75D1,k::restart_cycles);
        });
    }
    for (const std::uint16_t pc:{0x75D2,0x75D4}) {
        auto copy=[pc](auto& z) {
            if (pc==0x75D2) z.load_immediate(pc,z.r.b);
            z.output_increment(0x75D4);
            z.nop(0x75D6);
            return k::jump_if_exit(0x75D7);
        };
        registry.add({1,pc},k::segment(ice,{k::conditional_at(0x75D7)}),[copy](const Machine& m) {
            if ((m.cpu().bc.uint16_value&255)!=0xBE) return k::uncertain_maximum;
            return k::cost(m,copy,k::uncertain_maximum);
        },[copy](RoutineContext& c){k::run(c,copy);});
    }
    k::add_segment(registry,{1,0x75DA},k::segment(ice,{k::conditional_at(0x75E1,2)},{.inherited_stack=2}),
        [](auto& z) {
            z.exchange_de_hl(0x75DA);
            z.load_bc(0x75DB);
            z.add_to_hl(0x75DE,z.bc());
            z.exchange_de_hl(0x75DF);
            z.pop(0x75E0,z.r.b,z.r.c);
            return k::decrement_jump_exit(z,0x75E1);
        });
    bridge(ice,0x75E3,4,InterruptMask::enable);
}
}
