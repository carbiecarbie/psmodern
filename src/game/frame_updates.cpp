// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/frame_updates.h"
#include "game/map_kit.h"

namespace psmodern::game::frame_updates {
namespace {
namespace k=map_kit;
constexpr unsigned transfer_slice=104;
bool safe_scroll(const Machine& m) {
    for (auto [address,high]:{std::pair{0xC300,0x88},std::pair{0xC304,0x89}}) {
        auto reg=m.vdp().control_pair_register(m.peek(address),high);
        if (reg && (*reg==0 || *reg==1 || *reg==10)) return false;
    }
    return true;
}
template<class Z> k::Exit transfer_tiles(Z& z,std::uint16_t pc) {
    for (;;) {
        z.output_increment(pc);
        if (z.loop_exit(transfer_slice)) return k::jump_if_exit(pc+2);
        if (!z.jump_if(pc+2,!z.zero())) break;
    }
    z.decrement(pc+5,z.r.a);
    return k::jump_if_exit(pc+6);
}
}
void register_updates(RoutineRegistry& registry) {
    auto bridge=[&](Transfer t,SegmentExit exit,unsigned cost,bool call) {
        auto spec=k::segment({0,t.unit},{exit},{.scratch_stack=call ? 2u : 0u});
        spec.single_instruction=true;
        spec.observe_entry=t.pc==0x0B14;
        registry.add({0,t.pc},std::move(spec),[cost](const Machine&){return cost;},
                     [t](RoutineContext& c){c.finish_core(t.pc);});
    };
    for (auto t:calls) bridge(t,k::core_final_at(t.pc),k::call_cycles,true);
    for (auto t:jumps) bridge(t,k::core_final_at(t.pc),k::jump_cycles,false);
    for (auto t:{entries[0],entries[1]})
        k::add_segment(registry,{0,t.pc},k::segment({0,t.unit},{k::returned_at(t.pc)}),
                       [t](auto&){return k::returns(t.pc,k::return_cycles);});
    auto enable=k::core_final_at(0x00AE);
    enable.interrupt_mask=InterruptMask::enable;
    bridge({0x00AE,0x0085},enable,4,false);
    for (std::uint16_t start:{0x01EF,0x025F}) {
        const bool palette=start==0x025F;
        const RoutineEntry unit{0,start};
        auto add=[&](std::uint16_t pc,std::vector<SegmentExit> exits,k::Permissions perms,auto logic) {
            k::add_segment(registry,{0,pc},k::segment(unit,std::move(exits),perms),logic);
        };
        const auto scroll=[=](auto& z) {
            k::write_scroll_registers(z,start);
            return k::continues(start+18,palette ? k::call_cycles : 10);
        };
        registry.add(unit,k::segment(unit,{k::core_final_at(start+18)},{.scratch_stack=palette ? 2u : 0u}),
            [scroll](const Machine& m){return safe_scroll(m) ? k::cost(m,scroll,k::uncertain_maximum) : k::uncertain_maximum;},
            [scroll](RoutineContext& c){k::run(c,scroll);});
        if (palette) {
            add(0x0274,{k::core_final_at(0x027A)},{.scratch_stack=2},[](auto& z) {
                z.load_hl(0x0274);
                z.load_de(0x0277);
                return k::continues(0x027A,k::restart_cycles);
            });
            add(0x027B,{k::core_final_at(0x027D)},{.scratch_stack=2},[](auto& z) {
                z.load_immediate(0x027B,z.r.c);
                return k::continues(0x027D,k::call_cycles);
            });
        }
        const std::uint16_t loop=palette ? 0x0290 : 0x0226;
        const auto begin=[=](auto& z) {
            const auto clear=palette ? 0x0283 : 0x0204;
            if (palette) z.load_hl(0x0280);
            z.xor_a(clear);
            z.output(clear+1);
            z.load_immediate(clear+3,z.r.a);
            z.output(clear+5);
            z.load_immediate(clear+7,z.r.c);
            if (!palette) return k::continues(0x020D,k::call_cycles);
            z.load_immediate(0x028C,z.r.a);
            return k::continues(0x028E,7);
        };
        const auto entry=static_cast<std::uint16_t>(palette ? 0x0280 : 0x0204);
        add(entry,{k::core_final_at(palette ? 0x028E : 0x020D)},{.scratch_stack=palette ? 0u : 2u},begin);
        if (!palette) add(0x0222,{k::core_final_at(0x0224)},{},[](auto& z) {
            z.load_immediate(0x0222,z.r.a);
            return k::continues(0x0224,7);
        });
        auto emit=[=](auto& z){return transfer_tiles(z,loop);};
        registry.add({0,loop},k::segment(unit,{k::conditional_loop_at(loop+2,transfer_slice),k::conditional_at(loop+6)}),
            [emit](const Machine& m) {
                return (m.cpu().bc.uint16_value&0xff)==0xbe ? k::cost(m,emit,k::uncertain_maximum) : k::uncertain_maximum;
            },[emit](RoutineContext& c){k::run(c,emit);});
        add(loop+5,{k::conditional_at(loop+6)},{},[loop](auto& z) {
            z.decrement(loop+5,z.r.a);
            return k::jump_if_exit(loop+6);
        });
    }
}
}
