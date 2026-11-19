// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
void register_map_display(RoutineRegistry& registry) {
    namespace k=map_kit;
    const auto load=map::load_map_unit,ice=map::ice_probe_unit;
    // A segment that is one instruction: the interrupt switch-off or switch-on
    // of a video stretch, or the call or restart inside it.
    auto bridge=[&](RoutineEntry unit,std::uint16_t pc,unsigned cycles,InterruptMask mask,auto logic) {
        auto exit=k::core_final_at(pc);
        exit.interrupt_mask=mask;
        auto spec=k::segment(unit,{exit},{.scratch_stack=mask==InterruptMask::none ? 2u : 0u});
        spec.single_instruction=true;
        k::add_segment(registry,{unit.bank,pc},std::move(spec),[cycles](const Machine&) -> std::uint64_t { return cycles; },logic);
    };
    auto disable=[](auto& z) { return z.final_disable_interrupts(0x0C91); };
    for (const std::uint16_t pc:{0x0B6D,0x0BB4,0x0C91})
        bridge(load,pc,4,InterruptMask::disable,[pc](auto& z) { return z.final_disable_interrupts(pc); });
    for (const std::uint16_t pc:{0x0B71,0x0BB8,0x0C93})
        bridge(load,pc,4,InterruptMask::enable,[pc](auto& z) { return z.final_enable_interrupts(pc); });
    for (const std::uint16_t pc:{0x0B6A,0x0B6E,0x0BB5})
        bridge(load,pc,k::call_cycles,InterruptMask::none,[pc](auto& z) { return z.final_call(pc); });
    const SegmentExit mask{.kind=RoutineExit::core_final,.address=0x0C91,.interrupt_mask=InterruptMask::disable};
    k::add_segment(registry,{0,0x0C8E},k::segment(load,{mask}),[=](auto& z) {
        z.load_de(0x0C8E);
        return disable(z);
    });
    bridge(load,0x0C92,k::restart_cycles,InterruptMask::none,[](auto& z) { return z.final_restart(0x0C92,0x08); });

    // Breaking the ice replaces four tile bytes on each of two rows.
    // The coordinate helper supplies the VRAM address in DE.
    bridge(ice,0x75CC,4,InterruptMask::disable,[](auto& z) { return z.final_disable_interrupts(0x75CC); });
    for (const std::uint16_t pc:{0x75CD,0x75D0}) {
        k::add_segment(registry,{1,pc},k::segment(ice,{k::core_final_at(0x75D1,-2)},{.scratch_stack=4}),[=](auto& z) {
            if (pc==0x75CD) z.load_bc(0x75CD);
            z.push(0x75D0,z.r.b,z.r.c);
            return z.final_restart(0x75D1,0x08);
        });
    }
    // The tile bytes go to the video data port. C is a register, so the entry
    // guard also holds across a delivery inside the body.
    const k::Guard data_port{"C selects the video data port",[](const Machine& m) {
        return (m.cpu().bc.uint16_value&255)==0xBE;
    }};
    for (const std::uint16_t pc:{0x75D2,0x75D4}) {
        k::add_segment(registry,{1,pc},k::segment(ice,{k::conditional_at(0x75D7)}),data_port,[pc](auto& z) {
            if (pc==0x75D2) z.load_immediate(pc,z.r.b);
            z.output_increment(0x75D4);
            z.nop(0x75D6);
            return z.final_jump_if(0x75D7,k::Condition::nz);
        });
    }
    k::add_segment(registry,{1,0x75DA},k::segment(ice,{k::conditional_at(0x75E1,2)},{.inherited_stack=2}),
        [](auto& z) {
            z.exchange_de_hl(0x75DA);
            z.load_bc(0x75DB);
            z.add_to_hl(0x75DE,z.bc());
            z.exchange_de_hl(0x75DF);
            z.pop(0x75E0,z.r.b,z.r.c);
            return z.final_decrement_jump(0x75E1);
        });
    bridge(ice,0x75E3,4,InterruptMask::enable,[](auto& z) { return z.final_enable_interrupts(0x75E3); });

}
}
