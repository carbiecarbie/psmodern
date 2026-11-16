// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_kit.h"
#include "game/video_transfer.h"

namespace psmodern::game::video_transfer {
namespace {
namespace k=map_kit;
template<class Z>
void partial_page(Z& z,std::uint16_t at) {
    z.copy(at,z.r.a,z.r.c);
    z.or_a(at+1);
    if (!z.relative_if(at+2,z.zero())) z.increment(at+4,z.r.b);
}
template<class Z>
k::Exit fill_word(Z& z) {
    z.copy(0x0369,z.r.a,z.r.l);
    z.output(0x036A);
    z.push(0x036C,z.r.a,z.r.f);
    z.pop(0x036D,z.r.a,z.r.f);
    z.copy(0x036E,z.r.a,z.r.h);
    z.output(0x036F);
    z.decrement(0x0371,z.r.c);
    return k::relative_if_exit(0x0372,!z.zero());
}
template<class Z>
k::Exit tile_with_attribute(Z& z) {
    z.output_increment(0x037C);
    z.load_a_direct(0x037E);
    z.nop(0x0381);
    z.bus().fetch(0x0382);
    z.bus().fetch(0x0383);
    z.r.wz=static_cast<std::uint16_t>(z.bc()+1);
    z.bus().out(z.bc(),z.r.a,11);
    z.bus().advance(1);
    return k::relative_if_exit(0x0384,!z.zero());
}
template<class Logic>
void add(RoutineRegistry& registry,unsigned pc,unsigned unit,std::vector<SegmentExit> exits,
         k::Permissions permissions,Logic logic) {
    k::add_segment(registry,{0,static_cast<std::uint16_t>(pc)},
        k::segment({0,static_cast<std::uint16_t>(unit)},std::move(exits),permissions),logic);
}
template<class Logic>
void continue_data(RoutineRegistry& registry,std::uint16_t pc,std::uint16_t unit,
                   std::uint16_t branch,Logic logic) {
    registry.add({0,pc},k::segment({0,unit},{k::conditional_at(branch)}),
        [logic](const Machine& m) {
            if ((m.cpu().bc.uint16_value&255)!=0xBE) return k::uncertain_maximum;
            return k::cost(m,logic,k::uncertain_maximum);
        },[logic](RoutineContext& c) { k::run(c,logic); });
}

}
void register_packets(RoutineRegistry& registry) {
    // Each stream supplies one byte of every four in VRAM. The second
    // decoder additionally masks interrupts from address setup through OUT.
    for (bool masked:{false,true}) {
        const std::uint16_t unit=masked ? 0x03FA : 0x03CD;
        const std::uint16_t header=unit+13, address=header+11;
        const std::uint16_t data=masked ? 0x0414 : 0x03E6;
        const std::uint16_t stride=masked ? 0x041C : 0x03ED;
        for (auto pc:{unit,static_cast<std::uint16_t>(unit+2)}) {
            add(registry,pc,unit,{k::core_final_at(unit+4,-4)},{.scratch_stack=6},[=](auto& z) {
                if (pc==unit) z.load_immediate(unit,z.r.b);
                z.push(unit+2,z.r.b,z.r.c);
                z.push(unit+3,z.r.d,z.r.e);
                return k::continues(unit+4,k::call_cycles);
            });
        }
        add(registry,unit+7,unit,{k::conditional_at(unit+10,4)},{.inherited_stack=4},[=](auto& z) {
            z.pop(unit+7,z.r.d,z.r.e);
            z.increment_pair(unit+8,z.r.d,z.r.e);
            z.pop(unit+9,z.r.b,z.r.c);
            return k::decrement_jump_exit(z,unit+10);
        });
        add(registry,unit+12,unit,{k::returned_at(unit+12)},{},[=](auto&) {
            return k::returns(unit+12,k::return_cycles);
        });
        auto begin_address=k::core_final_at(address);
        if (masked) begin_address.interrupt_mask=InterruptMask::disable;
        const auto packet=[=](auto& z) {
            z.load_from_hl(header,z.r.a);
            z.increment_hl(header+1);
            z.or_a(header+2);
            if (z.zero()) return k::returns(header+3,k::taken_conditional_return_cycles);
            z.return_not_taken(header+3);
            z.copy(header+4,z.r.c,z.r.a);
            z.and_immediate(header+5);
            z.copy(header+7,z.r.b,z.r.a);
            z.copy(header+8,z.r.a,z.r.c);
            z.and_immediate(header+9);
            return k::continues(address,masked ? 4 : k::restart_cycles);
        };
        const k::Permissions stack{.scratch_stack=masked ? 0u : 2u};
        add(registry,header,unit,{k::returned_at(header+3),begin_address},stack,packet);
        auto single=[&](std::uint16_t pc,SegmentExit exit,unsigned cycles,k::Permissions perm) {
            auto spec=k::segment({0,unit},{exit},perm);
            spec.single_instruction=true;
            registry.add({0,pc},std::move(spec),[cycles](const Machine&){return cycles;},
                         [pc](RoutineContext& c){c.finish_core(pc);});
        };
        single(address,begin_address,masked ? 4 : k::restart_cycles,stack);
        if (masked) single(address+1,k::core_final_at(address+1),k::restart_cycles,{.scratch_stack=2});
        auto written=masked ? k::core_final_at(data+3) : k::conditional_at(data+3);
        if (masked) written.interrupt_mask=InterruptMask::enable;
        add(registry,data,unit,{written},{},[=](auto& z) {
            z.load_from_hl(data,z.r.a);
            z.output(data+1);
            return k::continues(data+3,masked ? 4 : k::jump_cycles);
        });
        // The conditional jump after EI executes in its original protected
        // instruction slot. A literal consumes its byte immediately.
        for (const std::uint16_t pc:{static_cast<std::uint16_t>(stride-1),stride}) {
            add(registry,pc,unit,{k::conditional_at(stride+4)},{},[=](auto& z) {
                if (pc!=stride) z.increment_hl(pc);
                for (unsigned i=0;i<4;++i) z.increment_pair(stride+i,z.r.d,z.r.e);
                return k::decrement_jump_exit(z,stride+4);
            });
        }
        const std::uint16_t end=stride+6;
        // The original jump goes straight to the header after a literal run.
        // Repeated runs fall through here to advance past their shared byte.
        add(registry,end+3,unit,{k::core_final_at(end+4)},{},[=](auto& z) {
            z.increment_hl(end+3);
            return k::continues(end+4,k::jump_cycles);
        });
    }
}
void register_transfers(RoutineRegistry& registry) {
    add(registry,0x03A4,0x03A4,{k::core_final_at(0x03A7)},{},[](auto& z) {
        z.load_hl(0x03A4);
        return k::continues(0x03A7,10);
    });
    const SegmentExit registers{RoutineExit::output_repeat,0x03AA};
    registry.add({0,0x03AA},k::segment({0,0x03A4},{registers}),&Machine::video_register_output_cycles,
        [](RoutineContext& c) {
            if (c.copy_count()==2) {
                auto& cpu=c.cpu();
                c.fetch_opcode(0x03AA);
                c.fetch_opcode(0x03AB);
                const auto value=c.read(cpu.hl.uint16_value++);
                cpu.bc.uint16_value=static_cast<std::uint16_t>(cpu.bc.uint16_value-256);
                c.write_port(cpu.bc.uint16_value,value,15);
                c.advance(6);
            }
            // The last OTIR sets every block-I/O flag, WZ and prefix latch.
            c.finish_output(0x03AA);
        });
    add(registry,0x03AC,0x03A4,{k::returned_at(0x03B8)},{},[](auto& z) {
        z.load_a_direct(0x03AC);
        z.store_a_direct(0x03AF);
        z.load_a_direct(0x03B2);
        z.store_a_direct(0x03B5);
        return k::returns(0x03B8,k::return_cycles);
    });
    auto address=[](auto& z) {
        z.copy(0x0008,z.r.a,z.r.e);
        z.output(0x0009);
        z.copy(0x000B,z.r.a,z.r.d);
        z.output(0x000C);
        return k::returns(0x000E,k::return_cycles);
    };
    registry.add({0,8},k::segment({0,8},{k::returned_at(0x000E)}),[](const Machine& m)->std::uint64_t {
        const auto de=m.cpu().de.uint16_value;
        const auto reg=m.vdp().control_pair_register(k::low_byte(de),k::high_byte(de));
        if (reg && (*reg==0 || *reg==1 || *reg==10)) return k::uncertain_maximum;
        return 40;
    },[=](RoutineContext& c) { k::run(c,address); });
    for (const std::uint16_t pc:{0x0346,0x0363}) {
        auto spec=k::segment({0,pc},{k::core_final_at(pc)},{.scratch_stack=2});
        spec.single_instruction=true;
        registry.add({0,pc},std::move(spec),[](const Machine&) {return k::restart_cycles;},
            [pc](RoutineContext& c){c.finish_core(pc);});
    }
    for (auto pc:{0x0347,0x034C})
        add(registry,pc,0x0346,{k::conditional_at(0x0352)},{},[pc](auto& z) {
            if (pc==0x0347) partial_page(z,pc);
            z.copy(0x034C,z.r.a,z.r.b);
            z.copy(0x034D,z.r.b,z.r.c);
            z.load_immediate(0x034E,z.r.c);
            z.output_increment(0x0350);
            return k::jump_if_exit(0x0352);
        });
    continue_data(registry,0x0350,0x0346,0x0352,[](auto& z) {
        z.output_increment(0x0350);
        return k::jump_if_exit(0x0352);
    });
    add(registry,0x0355,0x0346,{k::conditional_at(0x0356)},{},[](auto& z) {
        z.decrement(0x0355,z.r.a);
        return k::jump_if_exit(0x0356);
    });
    add(registry,0x035A,0x0363,{k::core_final_at(0x0363)},{.scratch_stack=2},[](auto& z) {
        z.load_de(0x035A);
        z.load_bc(0x035D);
        z.load_hl(0x0360);
        return k::continues(0x0363,k::restart_cycles);
    });
    for (auto pc:{0x0364,0x0369})
        add(registry,pc,0x0363,{k::conditional_at(0x0372)},{.scratch_stack=2},[pc](auto& z) {
            if (pc==0x0364) partial_page(z,pc);
            return fill_word(z);
        });
    // DJNZ for the next page can also be the final iteration of the fill.
    add(registry,0x0374,0x0363,{k::returned_at(0x0376),k::conditional_at(0x0372)},
        {.scratch_stack=2},[](auto& z) {
            if (z.decrement_jump(0x0374)) return fill_word(z);
            return k::returns(0x0376,k::return_cycles);
        });
    for (bool attributes:{true,false}) {
        const std::uint16_t unit=attributes ? 0x0377 : 0x0390;
        const auto loop=unit+5, next_row=attributes ? 0x0386 : 0x039A;
        const auto branch=attributes ? 0x0384 : 0x0397;
        add(registry,unit,unit,{k::core_final_at(unit+1,-2)},{.scratch_stack=4},[unit](auto& z) {
            z.push(unit,z.r.b,z.r.c);
            return k::continues(unit+1,k::restart_cycles);
        });
        auto transfer=[=](auto& z) {
            if (attributes) return tile_with_attribute(z);
            z.output_increment(loop);
            return k::jump_if_exit(branch);
        };
        add(registry,unit+2,unit,{k::conditional_at(branch)},{},[=](auto& z) {
            z.copy(unit+2,z.r.b,z.r.c);
            z.load_immediate(unit+3,z.r.c);
            return transfer(z);
        });
        continue_data(registry,loop,unit,branch,transfer);
        add(registry,next_row,unit,{k::conditional_at(next_row+7,2)},{.inherited_stack=2},[=](auto& z) {
            z.exchange_de_hl(next_row);
            z.load_bc(next_row+1);
            z.add_to_hl(next_row+4,z.bc());
            z.exchange_de_hl(next_row+5);
            z.pop(next_row+6,z.r.b,z.r.c);
            return k::decrement_jump_exit(z,next_row+7);
        });
    }
    for (auto [pc,unit]:{std::pair{0x0359,0x0346},{0x0376,0x0363},{0x038F,0x0377},{0x03A3,0x0390}})
        add(registry,pc,unit,{k::returned_at(pc)},{},[pc](auto&) {return k::returns(pc,k::return_cycles);});
}
}
