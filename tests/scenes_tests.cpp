// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/scenes.h"
#include "probe.h"
#include "game/menu_kit.h"
#include <iostream>
#include <algorithm>
#include <tuple>
#include <set>
#include <vector>
#include <random>
#include <string_view>
using namespace psmodern;
namespace {
struct Stopped {};
using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t,Memory::Kind>;
// The equality of these tests (milestone 14): the machines are compared with
// the documented oracle equality (the whole state, without the pinned core's
// per-step scratch, which rendered finals no longer produce), plus the two
// things that equality leaves out and these tests covered: the PSG writes
// the frame has made and the cartridge RAM's dirty flag.
bool same(const Console& engine,const Console& original) {
    const bool state=oracle::equal(engine,original,oracle::Scope::whole);
    const bool psg=engine.psg().writes()==original.psg().writes();
    const bool dirty=engine.memory().cartridge_ram_dirty()==original.memory().cartridge_ram_dirty();
    if(!state) {
        unsigned shown=0;
        for(const auto& difference:oracle::differences(engine,original,oracle::Scope::whole)) if(shown++<12) std::cerr<<"  "<<difference<<"\n";
    }
    if(!psg) std::cerr<<"  PSG writes differ\n";
    if(!dirty) std::cerr<<"  cartridge RAM dirty flag differs\n";
    return state && psg && dirty;
}
// A copy of a registered segment into another registry, keeping what makes it
// an exact segment (its entry guard included): the tests that drop one entry
// to stop a chain where the original continues build their registry with it.
void copy_routine(RoutineRegistry& to,const RoutineEntry& at,const RegisteredRoutine& routine) {
    if(routine.exact) {
        if(routine.guard) to.add_exact(at,routine.spec,*routine.guard,routine.bound,routine.body);
        else to.add_exact(at,routine.spec,routine.bound,routine.body);
    } else to.add(at,routine.spec,routine.bound,routine.body);
}
// Work RAM filled from a seed with a third of the bytes zero, so that the tests
// of a byte (`or a`, `cp n`) take both of their branches across the seeds.
// `last` bounds the filled area: the sweeps use C300-C600 only (random bytes in
// the sound queue make the interrupt handler overflow the PSG queue).
void random_ram(Machine& machine,unsigned seed,unsigned first=0xC000,unsigned last=0xDF00) {
    std::mt19937_64 random(seed*7919u+13u);
    for(unsigned address=first;address<last;++address) {
        const auto value=random();
        machine.memory().write(static_cast<std::uint16_t>(address),(value%3==0) ? 0 : static_cast<std::uint8_t>(value>>8));
    }
}
bool is_scene_file(std::string_view origin) {
    return origin.find("scenes")!=std::string_view::npos || origin.find("scene_objects")!=std::string_view::npos;
}
// The entries whose body is more than one instruction: what the interrupt
// sweep covers (a one-instruction segment has no boundary inside).
bool has_boundary_inside(const RegisteredRoutine& routine) {
    if(routine.spec.single_instruction) return false;
    for(const auto& exit:routine.spec.exits)
        if(exit.kind==RoutineExit::copy_repeat || exit.kind==RoutineExit::output_repeat) return false;
    return true;
}
void persistence_observers(probe::Harness& h) {
    for(bool resume:{false,true})
    for(bool include_prefix:{false,true})
    for(unsigned slot=1;slot<=(resume ? 5u : 1u);++slot)
    for(unsigned control:{0u,8u,12u,0x88u})
    for(unsigned remaining:{1u,100u,1000u,30000u,59000u}) {
        if(!resume && include_prefix) continue;
        const RoutineEntry entry{0,static_cast<std::uint16_t>(resume ? (include_prefix ? 0x069D : 0x06A2) : 0x05E8)};
        const std::uint16_t stop=resume ? 0x06B9 : 0x05F4;
        const std::uint16_t watched=resume ? 0x06B4 : 0x05F4;
        probe::Setup setup; setup.cartridge_ram=true;
        auto actual=h.prepare(entry,setup);
        actual.memory().write(0xFFFC,static_cast<std::uint8_t>(control));
        actual.memory().write(0xC2C5,static_cast<std::uint8_t>(slot));
        actual.memory().write(0xFFFC,8);
        for(unsigned i=0;i<1024;++i)
            actual.memory().write(static_cast<std::uint16_t>(0x8000+slot*1024+i),
                static_cast<std::uint8_t>(i*17+slot));
        actual.memory().write(0xFFFC,static_cast<std::uint8_t>(control));
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        const auto instruction_start=actual.cycles();
        Machine native=actual;
        std::vector<Write> expected_writes,actual_writes;
        std::vector<std::pair<std::uint64_t,Memory::Kind>> expected_hooks,actual_hooks;
        auto run=[&](Machine& m,RoutineRegistry* registry,std::vector<Write>& writes,auto& hooks) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                writes.emplace_back(address,value,m.cycles(),m.memory().mapping(0x8000).kind);
            };
            Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if(map.kind==Memory::Kind::rom && map.bank==0 && pc==watched)
                    hooks.emplace_back(m.cycles(),m.memory().mapping(0x8000).kind);
                if(map.kind==Memory::Kind::rom && map.bank==0 && pc==stop) throw Stopped{};
                return {};
            };
            try {
                for(unsigned frames=0;frames<4;++frames) m.run_frame({},registry,hook,observer);
            } catch(const Stopped&) { return; }
            // The resume chain has no segment at the stop: the engine stops there
            // before running anything, as the hook does on the original.
            catch(const UnhandledState&) {
                if(!registry || m.cpu().pc.uint16_value!=stop || m.memory().mapping(stop)!=Memory::Mapping{Memory::Kind::rom,0}) throw;
                return;
            }
            throw std::runtime_error("persistence observer chain did not reach its completion boundary: resume="+std::to_string(resume)+" remaining="+std::to_string(remaining)+" native="+std::to_string(registry==nullptr)+" pc="+std::to_string(m.cpu().pc.uint16_value));
        };
        RoutineRegistry chain;
        for(const auto& [at,routine]:h.registry().entries()) {
            if(resume && at==RoutineEntry{0,stop}) continue;
            copy_routine(chain,at,routine);
        }
        run(native,nullptr,expected_writes,expected_hooks);
        run(actual,&chain,actual_writes,actual_hooks);
        probe::check(same(native,actual),"initialization/Continue observer chain state");
        probe::check(expected_writes==actual_writes,"initialization/Continue write order, cycle, payload and mapping");
        probe::check(expected_hooks==actual_hooks && actual_hooks.size()==1,"initialization/Continue completion observed once at original cycle/mapping");
        if(resume && include_prefix) {
            const auto store=std::find_if(actual_writes.begin(),actual_writes.end(),[](const auto& w) {return std::get<0>(w)==0xFFFC;});
            probe::check(store!=actual_writes.end() && std::get<1>(*store)==8 && std::get<2>(*store)==instruction_start+7 &&
                std::get<3>(*store)==Memory::Kind::cartridge_ram,"Continue enables SRAM at the original store callback cycle S+7");
            probe::check(actual.peek(0xC300)==slot,"Continue prefix copies the selected SRAM slot even when ROM was initially visible");
        }
        if(resume) probe::check(actual.memory().mapping(0x8000).kind==Memory::Kind::rom,"Continue restores slot 2 ROM before completion");
    }
}
void cartridge_initialization_observers(probe::Harness& h) {
    struct Reached {};
    for(unsigned control:{8u,12u})
    for(unsigned remaining:{1u,100u,1000u,30000u,59000u})
    for(bool power_loss:{false,true}) {
        probe::Setup setup; setup.cartridge_ram=true;
        auto actual=h.prepare({0,0x0847},setup);
        actual.memory().write(0xFFFC,static_cast<std::uint8_t>(control));
        for(unsigned i=0;i<8192;++i) actual.memory().write(static_cast<std::uint16_t>(0x8000+i),static_cast<std::uint8_t>(i+37));
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        // Milestone 14: the run with routines is compared with the oracle at
        // every instruction boundary and frame (the lockstep), and, as before
        // the port, with an independent run of the original instructions
        // (no registry): every write with its observer cycle and the mapping
        // of slot 2 at that moment, and the whole final state. The power loss
        // is injected in both at the same cycle: where the engine's run_frame
        // returned (after the segment crossing the frame end); the original
        // is run to that cycle and the comparison of the writes starts there.
        Machine native=actual;
        std::vector<Write> expected,observed;
        Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
            observed.emplace_back(address,value,actual.cycles(),actual.memory().mapping(0x8000).kind);
        };
        Machine::WriteObserver native_observer=[&](std::uint16_t address,std::uint8_t value) {
            expected.emplace_back(address,value,native.cycles(),native.memory().mapping(0x8000).kind);
        };
        Machine::Hook hook=[](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
            if(map.kind==Memory::Kind::rom && map.bank==0 && pc==0x086E) throw Stopped{};
            return {};
        };
        // The hook sees only the steps no routine takes: the erase's return
        // (00:086E, a registered segment) is left out of this chain so that
        // the stop hook fires there, as at the original continuation.
        RoutineRegistry chain;
        for(const auto& [at,routine]:h.registry().entries())
            if(at!=RoutineEntry{0,0x086E}) copy_routine(chain,at,routine);
        oracle::Lockstep lockstep(actual,"cartridge initialization",oracle::Mode::instructions);
        std::size_t first_expected=0,first_observed=0;
        bool stopped=false;
        try {
            if(power_loss) {
                lockstep.run_frame(actual,{},&chain,{},hook,observer);
                // Re-enter initialization with the partially written SRAM,
                // as after losing power before the header is restored.
                lockstep.synchronize(actual);
                const auto target=actual.cycles();
                Machine::Hook until=[&](std::uint16_t,Memory::Mapping)->std::optional<std::uint16_t> {
                    if(native.cycles()>=target) throw Reached{};
                    return {};
                };
                // The original's frames, observed, to the engine's boundary: past its frame end too, where
                // the engine's run_frame went on to the end of the segment crossing it (review R3 L6).
                while(native.cycles()<target) { try { native.run_frame({},nullptr,until,native_observer); } catch(const Reached&) {} }
                probe::check(native.cycles()==target,"the original reaches the engine's power-loss boundary exactly");
                actual.cpu().pc.uint16_value=0x0847; lockstep.oracle().cpu().pc.uint16_value=0x0847;
                native.cpu().pc.uint16_value=0x0847;
                first_expected=expected.size(); first_observed=observed.size();
            }
            for(unsigned frame=0;frame<6;++frame) lockstep.run_frame(actual,{},&chain,{},hook,observer);
        } catch(const Stopped&) { stopped=true; }
        // No segment begins at 00:086E in the chain: the engine stops there
        // before running anything, as the hook does on the original.
        catch(const UnhandledState&) {
            if(actual.cpu().pc.uint16_value!=0x086E || actual.memory().mapping(0x086E)!=Memory::Mapping{Memory::Kind::rom,0}) throw;
            stopped=true;
        }
        probe::check(stopped,"cartridge initialization did not finish");
        lockstep.synchronize(actual);
        bool native_stopped=false;
        try {
            for(unsigned frame=0;frame<6;++frame) native.run_frame({},nullptr,hook,native_observer);
        } catch(const Stopped&) { native_stopped=true; }
        probe::check(native_stopped,"the original's cartridge initialization did not finish");
        // The writes before the power loss, then those after it (review R3 L6: both parts).
        probe::check(std::vector<Write>(expected.begin(),expected.begin()+static_cast<std::ptrdiff_t>(first_expected))==
                     std::vector<Write>(observed.begin(),observed.begin()+static_cast<std::ptrdiff_t>(first_observed)),
            "cartridge initialization writes before the power loss");
        probe::check(std::vector<Write>(expected.begin()+static_cast<std::ptrdiff_t>(first_expected),expected.end())==
                     std::vector<Write>(observed.begin()+static_cast<std::ptrdiff_t>(first_observed),observed.end()),
            "cartridge initialization/re-entry ordered byte writes, observer cycles and slot-2 mapping");
        probe::check(same(actual,native),"cartridge initialization/re-entry whole state");
        const auto first=std::find_if(observed.begin(),observed.end(),[](const Write& w) { return std::get<0>(w)==0x8000; });
        probe::check(first!=observed.end() && std::get<1>(*first)==0 && std::get<3>(*first)==Memory::Kind::cartridge_ram,"first initialization write seeds mapped SRAM with zero");
        probe::check(actual.memory().mapping(0x8000).kind==Memory::Kind::rom,"initialization restores ROM mapping");
        for(unsigned i=8188;i<8192;++i)
            probe::check(actual.memory().cartridge_ram()[i]==static_cast<std::uint8_t>(i+37),"initialization preserves the final four SRAM bytes");
    }
    auto disabled=h.prepare({0,0x0847},{});
    h.run_fallback(disabled,{0,0x0847},RoutineFallback::guard);   // the entry guard: cartridge RAM is not mapped
}

void scene_copy_native_tails(probe::Harness& h) {
    unsigned cases=0;
    for (const auto address:{0x61B9,0x61C2})
    for (const unsigned remaining:{0u,1u,2u,3u,8u,16u,17u})
    for (unsigned left=1;left<=70;++left)
    for (unsigned event=0;event<4;++event) {
        const RoutineEntry entry{1,static_cast<std::uint16_t>(address)};
        auto machine=h.prepare(entry,{});
        machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(remaining);
        machine.cpu().hl.uint16_value=0xC400;
        machine.cpu().de.uint16_value=0xC401;
        for (unsigned i=0;i<32;++i) machine.memory().write(0xC400+i,static_cast<std::uint8_t>(i*19+7));
        const auto phase=machine.cycles()%Machine::frame_cycles;
        machine.advance((Machine::frame_cycles-left+Machine::frame_cycles-phase)%Machine::frame_cycles);
        const unsigned total=remaining ? remaining : 65536;
        const auto duration=[&](unsigned n) {return 21*n-(n==total ? 5u : 0u);};
        unsigned frame_count=0;
        for (unsigned n=1;n<=16 && n<=total;++n)
            if (duration(n)<=left || (n>=2 && 21*(n-1)<left)) frame_count=n;
        const unsigned nominal=duration(frame_count ? frame_count : 1);
        const unsigned deadline=event==1 ? nominal-1 : event==2 ? nominal : nominal+1;
        if (event) machine.schedule_nmi(machine.cycles()+deadline);
        unsigned expected=0;
        for (unsigned n=1;n<=16 && n<=total;++n)
            if ((duration(n)<=left || (n>=2 && 21*(n-1)<left)) && (!event || duration(n)<=deadline)) expected=n;
        Machine original=machine;
        for (unsigned n=0;n<(expected ? expected : 1);++n) original.step();
        Machine unchecked=machine;
        RoutineRegistry plain;
        game::register_block_transfer(plain,entry,true);
        if (expected) {
            const auto start=machine.cycles();
            const auto result=h.run_cpp(machine,entry);
            probe::check(result.cycles==duration(expected),"scene copy chooses independent complete-transfer count");
            probe::check(machine.cpu().bc.uint16_value==static_cast<std::uint16_t>(remaining-expected),"scene copy retains exact BC including zero wrap");
            if (result.cycles>left) probe::check(expected>=2 && 21*(expected-1)<left && start+21*(expected-1)<start+left,"scene copy crosses frame only in a real native tail after C++ work");
        } else {
            oracle::NativeMachine native(machine);
            native.step();
            const auto exact_before=h.exact_instead();
            h.run_fallback(machine,entry,event && deadline<duration(1) ? RoutineFallback::nmi : RoutineFallback::frame);
            // A refusal leaves the machine unchanged (run_fallback checks it); the exact copy
            // that runs instead takes one transfer, the original instruction (review R3 L6).
            if(h.exact_instead()!=exact_before)
                probe::check(same(machine,native),"scene copy slice preserves the original instruction and late NMI");
        }
        unchecked.step(plain);
        probe::check(same(unchecked,machine),"scene copy admission and full state are identical with verifier disabled");
        const auto* unverified=plain.find(entry);
        // Milestone 14: the copy is exact, so a slice that does not fit before
        // the frame end or an NMI is admitted too (the event is taken at its
        // boundaries); the reference above is the original instruction.
        probe::check(unverified && unverified->counters.cpp_calls==1u && !unverified->counters.fallback_calls,"scene copy does not force admission with verifier disabled");
        machine.step();
        original.step();
        probe::check(same(machine,original),"scene copy preserves late NMI acceptance on the following instruction");
        ++cases;
    }
    std::cout<<"Scene native copy tails, final/repeat/zero BC and NMI boundaries: "<<cases<<"\n";
}

void scene_copy_irq_windows(probe::Harness& h) {
    unsigned cases=0;
    for (const auto address:{0x61B9,0x61C2})
    for (const unsigned remaining:{0u,1u,2u,3u,8u,16u,17u})
    for (unsigned distance=1;distance<=70;++distance) {
        const RoutineEntry entry{1,static_cast<std::uint16_t>(address)};
        probe::Setup setup;
        setup.iff1=true;
        setup.iff2=true;
        auto machine=h.prepare(entry,setup);
        machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(remaining);
        machine.cpu().hl.uint16_value=0xC400;
        machine.cpu().de.uint16_value=0xC401;
        const auto irq=machine.next_irq();
        probe::check(irq && *irq>machine.cycles()+distance,"scene copy has an upcoming real VDP IRQ");
        machine.advance(*irq-machine.cycles()-distance);
        const unsigned total=remaining ? remaining : 65536;
        unsigned expected=0;
        for (unsigned n=1;n<=16 && n<=total;++n)
            if (21*n-(n==total ? 5u : 0u)<=distance) expected=n;
        Machine native=machine;
        for (unsigned n=0;n<(expected ? expected : 1);++n) native.step();
        if (expected) {
            const auto result=h.run_cpp(machine,entry);
            probe::check(result.cycles==21*expected-(expected==total ? 5u : 0u),"scene copy IRQ window retains complete bound");
        } else h.run_fallback(machine,entry,RoutineFallback::irq);
        probe::check(same(machine,native),"scene copy matches at IRQ boundary including late sampling");
        machine.step();
        native.step();
        probe::check(same(machine,native),"scene copy preserves IRQ acceptance on the following instruction");
        ++cases;
    }
    for (const auto address:{0x61B9,0x61C2})
    for (const std::uint16_t destination:{0xFFFB,0xFFFC,0xFFFF}) {
        const RoutineEntry entry{1,static_cast<std::uint16_t>(address)};
        auto machine=h.prepare(entry,{});
        machine.cpu().bc.uint16_value=3;
        machine.cpu().hl.uint16_value=0xC400;
        machine.cpu().de.uint16_value=destination;
        h.run_fallback(machine,entry,RoutineFallback::mapper_write);
    }
    std::cout<<"Scene copy real VDP IRQ windows and continuation states: "<<cases<<"\n";
}

void visual_scene_copy_windows(probe::Harness& h) {
    RoutineRegistry chain;
    for (const auto& [entry,routine]:h.registry().entries()) {
        if (entry==RoutineEntry{1,0x61D9}) continue;
        copy_routine(chain,entry,routine);
    }
    unsigned cases=0;
    for (unsigned event=0;event<3;++event)
    for (unsigned remaining=1;remaining<=128;++remaining) {
        probe::Setup setup;
        setup.iff1=event==2;
        setup.iff2=event==2;
        auto actual=h.prepare({1,0x617D},setup);
        actual.cpu().af.uint8_values.at_1=1;
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        if (event==1) actual.schedule_nmi(actual.cycles()+remaining/2);
        if (event==2) probe::check(actual.vdp().irq(),"scene window IRQ variant has a pending VDP interrupt");
        Machine native=actual;
        Machine verified=actual;
        std::vector<Write> expected,observed,verified_writes;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        FrameInput input;
        input.pause=(remaining&1)!=0;
        input.pads[0].one=(remaining&2)!=0;
        auto run=[&](Machine& machine,RoutineRegistry* registry,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                writes.emplace_back(address,value,machine.cycles(),machine.memory().mapping(address).kind);
            };
            Machine::Hook hook=[](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if (map.kind==Memory::Kind::rom && map.bank==1 && pc==0x61D9) throw Stopped{};
                return {};
            };
            try {
                for (unsigned frame=0;frame<8;++frame) machine.run_frame(input,registry,hook,observer);
            } catch (const Stopped&) { return; }
            // The chain has no segment at 01:61D9: the engine stops there before
            // running anything, as the hook does on the original.
            catch (const UnhandledState&) {
                const auto map=machine.memory().mapping(0x61D9);
                if (!registry || machine.cpu().pc.uint16_value!=0x61D9 || map.kind!=Memory::Kind::rom || map.bank!=1) throw;
                return;
            }
            throw std::runtime_error("scene copies did not reach their continuation");
        };
        run(native,nullptr,expected);
        run(verified,&chain,verified_writes);
        // A nested per-call trace owns the ports during verification. A
        // separate identical run lets the outer trace capture the whole chain.
        run(actual,&chain,observed);
        // The documented equality (milestone 14): the core's per-step scratch
        // is not machine state, and the rendered chain runs fewer core steps.
        probe::check(same(verified,native) && verified_writes==expected,
            "verified scene chains preserve full state and every callback");
        probe::check(same(actual,native),"scene adaptive copies preserve whole state across frame windows");
        probe::check(expected==observed,"scene adaptive copies preserve every seed/copy write and callback cycle");
        if (expected_ports.accesses()!=actual_ports.accesses()) {
            std::string detail="scene chain ports event="+std::to_string(event)+" left="+std::to_string(remaining)+" expected="+std::to_string(expected_ports.accesses().size())+" actual="+std::to_string(actual_ports.accesses().size());
            for (unsigned i=0;i<std::min(expected_ports.accesses().size(),actual_ports.accesses().size());++i) {
                const auto& a=expected_ports.accesses()[i];
                const auto& b=actual_ports.accesses()[i];
                if (a==b) continue;
                detail+=" first="+std::to_string(i)+" expected cycle/port/value="+std::to_string(a.cycle)+"/"+std::to_string(a.port)+"/"+std::to_string(a.value)+" actual="+std::to_string(b.cycle)+"/"+std::to_string(b.port)+"/"+std::to_string(b.value);
                break;
            }
            throw std::runtime_error(detail);
        }
        ++cases;
    }
    std::cout<<"Visual scene frame windows and ordered seed/copy writes: "<<cases<<"\n";
}

void scene_copy_windows(probe::Harness& harness,const probe::Rom& rom) {
    // Minimal interrupt handlers patched into the image (PUSH AF; IN A,(BF);
    // POP AF; EI; RET and NOP; RETN, a segment's final at its own entry being
    // a single instruction, which an NMI return is not): the original runs them on the oracle, the
    // chain runs the test's own rendered segments for them (milestone 14).
    // RET, not RETI: in this IM 1 handler EI has set IFF1=IFF2 already, so
    // RETI's only other effect (IFF1 from IFF2) is the same machine state.
    auto image=std::make_shared<Memory::Rom>(*rom.image);
    const std::array<std::uint8_t,6> irq{0xF5,0xDB,0xBF,0xF1,0xFB,0xC9};
    std::copy(irq.begin(),irq.end(),image->begin()+0x0038);
    image->at(0x0066)=0x00;image->at(0x0067)=0xED;image->at(0x0068)=0x45;
    RoutineRegistry handlers;
    {
        namespace k=game::menu_kit;
        k::add_segment(handlers,{0,0x0038},k::segment({0,0x0038},{k::core_final_at(0x003C)},{.scratch_stack=2}),[](auto& z) {
            z.push(0x0038,z.r.a,z.r.f);
            z.bus().fetch(0x0039);                                    // in a,(n)
            const auto port=k::pair(z.r.a,z.bus().read(0x003A));
            z.r.a=z.bus().in(port,10); z.r.wz=static_cast<std::uint16_t>(port+1); z.bus().advance(1);
            z.pop(0x003B,z.r.a,z.r.f);
            return z.final_enable_interrupts(0x003C);
        });
        k::add_segment(handlers,{0,0x003D},k::segment({0,0x0038},{k::returned_at(0x003D)}),[](auto& z) { return z.final_return(0x003D); });
        SegmentExit retn{RoutineExit::core_final,0x0067};
        retn.return_from_nmi=true;
        k::add_segment(handlers,{0,0x0066},SegmentSpec{{0,0x0066},{retn}},[](auto& z) { z.nop(0x0066); return z.final_return_from_nmi(0x0067); });
    }
    struct Case {RoutineEntry entry;std::uint16_t final;unsigned kind;};
    const std::array cases{Case{{0,0x075A},0x0793,0},Case{{0,0x08CE},0x08E4,0},
        Case{{0,0x08FF},0x090C,0},Case{{0,0x3D4E},0x0180,1},
        Case{{1,0x4048},0x4063,0},Case{{1,0x42D8},0x42FE,1},
        Case{{1,0x45F6},0x4611,0},Case{{0,0x3D36},0x0180,2},Case{{0,0x3D36},0x3D44,3},
        Case{{0,0x3D47},0x0180,5},Case{{0,0x3D47},0x0180,4}};
    std::set<RoutineEntry> seen_seeds;
    for(const auto& test:cases)
    for(unsigned event=0;event<3;++event)
    for(unsigned distance:{1u,64u,1000u}) {
        Machine actual(image);
        (void)actual.read_port(0xBF,actual.cycles());actual.advance(1);
        actual.memory().write(0xFFFC,0);actual.memory().write(0xFFFE,1);actual.memory().write(0xFFFF,2);
        for(unsigned a=0xC000;a<0xE000;++a) actual.memory().write(static_cast<std::uint16_t>(a),static_cast<std::uint8_t>(a*17+test.entry.address));
        actual.cpu().pc.uint16_value=test.entry.address;actual.cpu().sp.uint16_value=0xDF00;
        actual.cpu().hl.uint16_value=0xC300;actual.cpu().de.uint16_value=0xC400;actual.cpu().bc.uint16_value=1;
        actual.cpu().af.uint16_value=0xA5D7;actual.cpu().iff1=actual.cpu().iff2=0;
        actual.memory().write(0xDF00,0x80);actual.memory().write(0xDF01,1);
        if(test.entry.address==0x4048) actual.cpu().hl.uint16_value=static_cast<std::uint16_t>(rom.byte(1,0x4046)|(rom.byte(1,0x4047)<<8));
        if(test.entry.address==0x08FF) actual.cpu().hl.uint16_value=static_cast<std::uint16_t>(rom.byte(0,0x08FD)|(rom.byte(0,0x08FE)<<8));
        if(test.kind==2) actual.cpu().af.uint8_values.at_1=0xFF;
        if(test.kind==3) actual.cpu().af.uint8_values.at_1=0;
        if(test.entry.address==0x3D47) {
            const auto field=static_cast<std::uint16_t>(rom.byte(0,0x3D48)|(rom.byte(0,0x3D49)<<8));
            actual.memory().write(field,static_cast<std::uint8_t>(test.kind==5 ? 0xFF : 0));
        }
        const auto stop_address=test.kind==1 || test.kind==2 || test.kind==4 || test.kind==5 ? test.final :
            static_cast<std::uint16_t>(rom.byte(test.entry.bank,static_cast<std::uint16_t>(test.final+1))|
                (rom.byte(test.entry.bank,static_cast<std::uint16_t>(test.final+2))<<8));
        const RoutineEntry stop{stop_address<0x4000 ? 0u : 1u,stop_address};
        if(event==0) {
            const auto phase=actual.cycles()%Machine::frame_cycles;
            actual.advance((Machine::frame_cycles-distance+Machine::frame_cycles-phase)%Machine::frame_cycles);
        } else if(event==1) {
            actual.write_port(0xBF,0x20,actual.cycles());actual.write_port(0xBF,0x81,actual.cycles());
            const auto next=actual.vdp().next_irq();
            probe::check(next && *next>=actual.cycles()+distance,"scene copy chain has a future IRQ");
            actual.advance(*next-actual.cycles()-distance);actual.cpu().iff1=actual.cpu().iff2=1;
        } else actual.schedule_nmi(actual.cycles()+distance);
        Machine native=actual;
        // The whole game registry but the stop (milestone 14: the code after the
        // scene files, such as the slot copy of 01:61D9, is rendered elsewhere
        // and no core runs it any more).
        RoutineRegistry chain;
        for(const auto& [entry,routine]:harness.registry().entries())
            if(entry!=stop && entry!=RoutineEntry{0,0x0038} && entry!=RoutineEntry{0,0x0066}) copy_routine(chain,entry,routine);
        for(const auto& [entry,routine]:handlers.entries()) copy_routine(chain,entry,routine);
        using Event=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t,Memory::Mapping,Memory::Mapping>;
        std::vector<Event> expected,observed;
        std::vector<PsgWrite> expected_psg,observed_psg;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        auto run=[&](Machine& machine,RoutineRegistry* registry,std::vector<Event>& writes,std::vector<PsgWrite>& psg) {
            bool image_arm=false;
            std::uint16_t pc=0;unsigned bank=0;std::uint64_t instruction_start=0,instruction_fetches=0;
            Machine::WriteObserver observer=[&](std::uint16_t a,std::uint8_t value) {
                writes.emplace_back(a,value,machine.cycles(),machine.memory().mapping(a),machine.memory().mapping(0x8000));
            };
            Machine::Hook hook=[&](std::uint16_t address,Memory::Mapping mapping)->std::optional<std::uint16_t> {
                pc=address;bank=mapping.bank;instruction_start=machine.cycles();instruction_fetches=machine.opcode_fetches();
                if(!registry && test.entry.address==0x3D47 && mapping==Memory::Mapping{Memory::Kind::rom,0} && address==(test.kind==5 ? 0x3D4E : 0x3D64)) image_arm=true;
                if(mapping.kind==Memory::Kind::rom && RoutineEntry{mapping.bank,address}==stop) {
                    if(!registry && test.entry.address==0x3D47) probe::check(image_arm,"real interaction-type field selects the expected 3D47 arm");
                    throw Stopped{};
                }
                return {};
            };
            try {for(unsigned frame=0;frame<16;++frame) {machine.run_frame({},registry,hook,observer);machine.take_samples();const auto drained=machine.take_psg_writes();psg.insert(psg.end(),drained.begin(),drained.end());}}
            catch(const Stopped&) {return;}
            // The chain has no segment at the stop: the engine stops there before
            // running anything, as the hook does on the original.
            catch(const UnhandledState&) {
                const auto at=machine.cpu().pc.uint16_value;const auto mapping=machine.memory().mapping(at);
                if(!registry || mapping.kind!=Memory::Kind::rom || RoutineEntry{mapping.bank,at}!=stop) throw;
                return;
            }
            throw std::runtime_error("scene copy chain did not reach its original continuation: "+std::to_string(test.entry.address));
        };
        run(native,nullptr,expected,expected_psg);run(actual,&chain,observed,observed_psg);
        // The fill seeds are one-instruction segments now: the comparison of
        // every write with its cycle and mapping below is their payload and
        // observer-cycle proof; here it is noted which of them this chain ran.
        for(const auto seed:game::scenes::startup_native_seeds)
            if(const auto* routine=chain.find(seed); routine && routine->counters.cpp_calls) seen_seeds.insert(seed);
        probe::check(same(actual,native),"split scene copy preserves CPU/WZ/R, buffers, mapping, peripherals and events");
        probe::check(expected==observed,"split scene copy preserves every write address, byte, order, cycle and mapping");
        probe::check(expected_psg==observed_psg,"split scene copy preserves every PSG write and its cycle");
        probe::check(expected_ports.accesses()==actual_ports.accesses(),"split scene copy preserves every port operation and cycle");
    }
    for(const auto seed:game::scenes::startup_native_seeds) {
        const auto* routine=harness.registry().find(seed);
        probe::check(routine && routine->spec.single_instruction,"declared scene fill seed is a one-instruction segment");
        probe::check(seen_seeds.contains(seed),"every declared scene fill seed has a payload/cycle observer proof");
    }
}
void hapsby_lookup_routes(probe::Harness& h,const probe::Rom& rom) {
    const auto item=rom.byte(1,0x5547);
    const auto inventory=static_cast<std::uint16_t>(rom.byte(0,0x282F)|(rom.byte(0,0x2830)<<8));
    const auto slots=rom.byte(0,0x2832);
    const auto current_item=static_cast<std::uint16_t>(rom.byte(1,0x5549)|(rom.byte(1,0x554A)<<8));
    const auto dialogue=static_cast<std::uint16_t>(rom.byte(1,0x5541)|(rom.byte(1,0x5542)<<8));
    const auto cleanup=static_cast<std::uint16_t>(rom.byte(1,0x5551)|(rom.byte(1,0x5552)<<8));
    for(unsigned scenario=0;scenario<3;++scenario) {
        auto actual=h.prepare({1,0x5546},{});
        for(unsigned slot=0;slot<slots;++slot) actual.memory().write(static_cast<std::uint16_t>(inventory+slot),0);
        if(scenario) actual.memory().write(static_cast<std::uint16_t>(inventory+(scenario==1 ? 0 : slots-1)),item);
        Machine native=actual;
        RoutineRegistry chain;
        for(const auto& [entry,routine]:h.registry().entries()) if(entry.address!=dialogue && entry.address!=cleanup) {
            copy_routine(chain,entry,routine);
        }
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        std::uint16_t reached=0;
        Memory::Mapping reached_mapping{};
        auto run=[&](Machine& m,RoutineRegistry* registry,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t a,std::uint8_t v) {writes.emplace_back(a,v,m.cycles());};
            Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if(pc==dialogue || pc==cleanup) {reached=pc;reached_mapping=map;throw Stopped{};}
                return {};
            };
            try {m.run_frame({},registry,hook,observer);} catch(const Stopped&) {return;}
            // The chain has no segment at either continuation: the engine stops
            // there before running anything, as the hook does on the original.
            catch(const UnhandledState&) {
                const auto pc=m.cpu().pc.uint16_value;
                // The stop is at the continuation the original reached, in the same bank (review R3 L6).
                if(!registry || (pc!=dialogue && pc!=cleanup) || m.memory().mapping(pc)!=reached_mapping) throw;
                reached=pc; return;
            }
            throw std::runtime_error("Hapsby lookup did not reach its selected continuation");
        };
        run(native,nullptr,expected);const auto expected_target=reached;
        run(actual,&chain,observed);
        probe::check(same(actual,native) && expected==observed,"Hapsby lookup routes preserve full state and all write callbacks");
        probe::check(expected_ports.accesses()==actual_ports.accesses(),"Hapsby lookup routes preserve every port operation");
        probe::check(reached==expected_target && reached==(scenario ? dialogue : cleanup),"Hapsby chooses already-present dialogue or new-item continuation");
        probe::check(actual.peek(current_item)==item,"Hapsby leaves the cartridge item request for the grant continuation");
        for(unsigned slot=0;slot<slots;++slot)
            probe::check(actual.peek(static_cast<std::uint16_t>(inventory+slot))==(scenario && slot==(scenario==1 ? 0u : slots-1) ? item : 0),"lookup does not alter inventory before the selected continuation");
    }
}
void regional_name_literal(probe::Harness& h,const probe::Rom& rom,std::uint8_t expected) {
    probe::check(rom.byte(1,0x4181)==expected,"identified release has its original name-layout literal");
    auto machine=h.prepare({1,0x4166},{});
    h.run_cpp(machine,{1,0x4166});
    for(const auto [first,count]:{std::pair{0xD2CCu,11u},std::pair{0xD34Cu,11u},std::pair{0xD3CCu,10u}})
        for(unsigned cell=0;cell<count;++cell)
            probe::check(machine.peek(static_cast<std::uint16_t>(first+4*cell))==expected,
                "4180 writes the regional literal into every incrementing-run destination cell");
}

}
int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
 try {
  probe::check(argc==3,"Expected ROM and region");
  const auto rom=probe::load(argv[1],argv[2]); if(!rom) return 77;
  probe::Harness h(*rom); RoutineRegistry catalog; game::register_scenes(catalog);
  scene_copy_native_tails(h);
  scene_copy_irq_windows(h);
  visual_scene_copy_windows(h);
  scene_copy_windows(h,*rom);
  hapsby_lookup_routes(h,*rom);
  regional_name_literal(h,*rom,std::string_view(argv[2])=="us" ? 0xC0 : 0x20);
  unsigned calls=0;
  // The entry state of one segment's probes: the registers and memory the segment
  // reads, set so that its interesting path runs (shared by the probes and the
  // interrupt sweeps below).
  auto special=[&](Machine& machine,RoutineEntry entry,unsigned seed) {
  if(entry.address==0x05F2 || entry.address==0x06B2 || entry.address==0x3F47 || entry.address==0x3F53 || entry.address==0x0851 || (entry.bank==1 && (entry.address==0x618F || entry.address==0x619C || entry.address==0x61B9 || entry.address==0x61C2 || entry.address==0x61D7))) {
   machine.cpu().hl.uint16_value=0xC300;
   machine.cpu().de.uint16_value=0xC301;
   machine.cpu().bc.uint16_value=seed+1;
  }
  if(entry.bank==1 && entry.address==0x413A) {
   machine.cpu().hl.uint16_value=0xD146;
   machine.cpu().de.uint16_value=0xC721;
  }
  if(entry.bank==1 && (entry.address==0x41C8 || entry.address==0x41D5 || entry.address==0x41B2 || entry.address==0x41AB)) {
   machine.cpu().hl.uint16_value=0xD146;
   machine.cpu().de.uint16_value=0xF303;
   machine.cpu().bc.uint16_value=0x1805;
  }
  if(entry.address==0x3FF2 || entry.address==0x4006) {
   machine.cpu().ix_iy[0].uint16_value=0xC784;
   machine.cpu().ix_iy[1].uint16_value=0xC784;
   machine.cpu().bc.uint16_value=0xC808;
   machine.cpu().de.uint16_value=2;
   machine.memory().write(0xC786,0x0A);
   machine.memory().write(0xC787,0xD3);
  }
  if(entry.address==0x641B) {
   machine.cpu().hl.uint16_value=0x6506;
   machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(((seed%4)+1)<<8);
   machine.memory().write(0xFFFF,16);
  }
  if(entry.address==0x0847) machine.memory().write(0xFFFC,8);
  if(entry.address==0x07E2) machine.cpu().bc.uint16_value=0x1000;
  if(entry.bank==1 && entry.address==0x4107) machine.cpu().hl.uint16_value=0xC781;
  if(entry.address==0x615A || entry.address==0x615E) {
   machine.cpu().hl.uint16_value=0xC100;
   machine.cpu().de.uint16_value=0xD100;
   machine.cpu().bc.uint16_value=0x08FF;
   for(unsigned i=0;i<32;++i) machine.memory().write(static_cast<std::uint16_t>(0xC100+i),static_cast<std::uint8_t>((i+seed)%3 ? i+1 : 0));
  }
  if(entry.address==0x6427 || entry.address==0x6438 || entry.address==0x643D) {
   const auto sp=machine.cpu().sp.uint16_value;
   machine.memory().write(sp,entry.address==0x6427 ? 0 : 8);
   machine.memory().write(static_cast<std::uint16_t>(sp+1),entry.address==0x6427 ? 4 : 0x65);
   machine.memory().write(static_cast<std::uint16_t>(sp+2),0);
   machine.memory().write(static_cast<std::uint16_t>(sp+3),4);
   machine.cpu().hl.uint16_value=entry.address==0x6427 ? 0x6507 : 0x8000;
   machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(((seed%128)+1)*256+0xBE);
   machine.memory().write(0xFFFF,16);
  }
  for(const auto& load:game::scenes::ei_delay_scaffolding) if(load.suffix==entry) {
   // The load's effect, from the instruction in the player's ROM.
   const auto opcode=rom->byte(load.instruction.bank,load.instruction.address);
   const auto word=static_cast<std::uint16_t>(rom->byte(load.instruction.bank,static_cast<std::uint16_t>(load.instruction.address+1))|
       (rom->byte(load.instruction.bank,static_cast<std::uint16_t>(load.instruction.address+2))<<8));
   if(opcode==0x21) machine.cpu().hl.uint16_value=word;
   if(opcode==0x11) machine.cpu().de.uint16_value=word;
   if(opcode==0x3A) machine.cpu().af.uint8_values.at_1=machine.peek(word);
   if(opcode==0x3E) machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(word);
  }
  };
  probe::check(catalog.entries().size()==game::scenes::segments.size(),"scene catalog size");
  for(const auto& [entry,unit]:game::scenes::segments) {
   probe::check(catalog.find(entry) && catalog.find(entry)->spec.unit==unit,"declared scene segment registration");
   for(unsigned seed=0;seed<32;++seed) {
    probe::Setup setup; setup.seed=seed; setup.r=static_cast<std::uint8_t>(seed*9);
    setup.cartridge_ram=entry==RoutineEntry{0,0x0847};   // the erase's entry guard needs cartridge RAM
    auto machine=h.prepare(entry,setup);
    special(machine,entry,seed);
    h.run_cpp(machine,entry); ++calls;
   }
  }
  for(unsigned slot=0;slot<=5;++slot) {
   probe::Setup setup; setup.cartridge_ram=true;
   auto machine=h.prepare({0,0x0639},setup);
   for(unsigned i=1;i<=5;++i) machine.memory().write(static_cast<std::uint16_t>(0x8200+i),i==slot ? 1 : 0);
   h.run_cpp(machine,{0,0x0639}); ++calls;
   probe::check(machine.cpu().pc.uint16_value==(slot ? 0x0652 : 0x064A),"slot scan chooses correct path");
  }
  for(unsigned seed=0;seed<512;++seed) {
   for(const auto entry:{RoutineEntry{0,0x0998},RoutineEntry{0,0x09DB},RoutineEntry{0,0x09E9}}) {
    probe::Setup setup; setup.seed=seed; auto machine=h.prepare(entry,setup);
    std::mt19937_64 random(seed);
    for(auto address:{0xC304,0xC305,0xC306,0xC307,0xC2F2,0xC2F3,0xC2E9,0xC2F0})
     machine.memory().write(static_cast<std::uint16_t>(address),static_cast<std::uint8_t>(random()));
    machine.memory().write(0xC2E9,seed%4); h.run_cpp(machine,entry); ++calls;
   }
  }
  for(unsigned position=0;position<256;++position) {
   probe::Setup setup; setup.seed=position;
   auto machine=h.prepare({1,0x4117},setup);
   machine.memory().write(0xC781,static_cast<std::uint8_t>(position));
   h.run_cpp(machine,{1,0x4117}); ++calls;
   auto repeat=h.prepare({1,0x402C},setup);
   repeat.memory().write(0xC789,static_cast<std::uint8_t>(position));
   h.run_cpp(repeat,{1,0x402C}); ++calls;
  }
  for(const auto& [entry,unit]:game::scenes::segments) {
   probe::Setup setup;
   setup.cartridge_ram=entry==RoutineEntry{0,0x0847};   // the erase's entry guard
   auto machine=h.prepare(entry,setup);
   if(entry.address==0x05F2 || entry.address==0x06B2 || entry.address==0x3F47 || entry.address==0x3F53 || entry.address==0x0851 || (entry.bank==1 && (entry.address==0x618F || entry.address==0x619C || entry.address==0x61B9 || entry.address==0x61C2 || entry.address==0x61D7))) {
    machine.cpu().hl.uint16_value=0xC300;
    machine.cpu().de.uint16_value=0xC301;
    machine.cpu().bc.uint16_value=1;
   }
   const auto phase=machine.cycles()%Machine::frame_cycles;
   machine.advance(Machine::frame_cycles-phase-1);
   // Milestone 14: an exact segment runs in C++ across the frame end instead
   // (compared with the oracle by the harness); a refused one stops with the
   // machine unchanged (checked by run_fallback). Nothing runs in its place.
   h.run_fallback(machine,entry,RoutineFallback::frame);
  }
  for(unsigned buttons=0;buttons<256;++buttons) {
   for(unsigned held=0;held<16;++held) {
    auto machine=h.prepare({0,0x3F73},{});
    machine.memory().write(0xC205,static_cast<std::uint8_t>(buttons));
    machine.memory().write(0xC204,static_cast<std::uint8_t>(held));
    h.run_cpp(machine,{0,0x3F73}); ++calls;
   }
  }
  for(unsigned slot=1;slot<=5;++slot) {
   probe::Setup setup; setup.cartridge_ram=true;
   auto machine=h.prepare({0,0x06FA},setup);
   machine.memory().write(0xC2C5,static_cast<std::uint8_t>(slot));
   machine.memory().write(static_cast<std::uint16_t>(0x8200+slot),1);
   h.run_cpp(machine,{0,0x06FA}); ++calls;
   machine.memory().write(0xFFFC,8);
   probe::check(machine.peek(static_cast<std::uint16_t>(0x8200+slot))==0,"erase clears selected occupied flag");
  }
  for(unsigned counter=0;counter<256;++counter) {
   for(const auto entry:{RoutineEntry{1,0x64A3},RoutineEntry{1,0x64CF}}) {
    auto machine=h.prepare(entry,{});
    machine.memory().write(0xC202,13);
    machine.memory().write(0xC213,static_cast<std::uint8_t>(counter));
    machine.memory().write(0xC214,static_cast<std::uint8_t>(counter));
    h.run_cpp(machine,entry); ++calls;
   }
  }
  for(unsigned counter=0;counter<256;++counter) {
   for(const auto entry:{RoutineEntry{1,0x63D2},RoutineEntry{1,0x63F9},RoutineEntry{1,0x6442}}) {
    auto machine=h.prepare(entry,{});
    machine.memory().write(0xC2BC,static_cast<std::uint8_t>(counter));
    machine.memory().write(0xC2BD,static_cast<std::uint8_t>(counter));
    h.run_cpp(machine,entry); ++calls;
   }
  }
  persistence_observers(h);
  cartridge_initialization_observers(h);
  for(unsigned mismatch=0;mismatch<=32;++mismatch) {
   probe::Setup setup; setup.cartridge_ram=true;
   auto machine=h.prepare({0,0x07E2},setup);
   machine.cpu().bc.uint16_value=0x1000;
   for(unsigned i=0;i<32;++i) machine.memory().write(static_cast<std::uint16_t>(0x8001+i),rom->byte(0,static_cast<std::uint16_t>(0x0808+i)));
   if(mismatch<32) machine.memory().write(static_cast<std::uint16_t>(0x8001+mismatch),static_cast<std::uint8_t>(machine.peek(static_cast<std::uint16_t>(0x8001+mismatch))^0x80));
   h.run_cpp(machine,{0,0x07E2}); ++calls;
  }
  {
   probe::Setup setup; setup.banks={0,2,3};
   auto machine=h.prepare({0,0x3FF2},setup);
   machine.cpu().ix_iy[1].uint16_value=0xC784;
   machine.memory().write(0xC784,0x20);
   h.run_fallback(machine,{0,0x3FF2},RoutineFallback::guard);   // the entry guard: bank 1 is not at 4000h
   probe::check(machine.peek(0xC784)==0x20,"cross-bank cursor admission refuses a remapped second fixed bank before mutation");
  }
  for(unsigned pose=0;pose<9;++pose) {
   auto machine=h.prepare({1,0x46D5},{});
   const auto sp=machine.cpu().sp.uint16_value;
   machine.memory().write(sp,static_cast<std::uint8_t>(pose*23));
   machine.memory().write(static_cast<std::uint16_t>(sp+1),static_cast<std::uint8_t>(pose));
   h.run_cpp(machine,{1,0x46D5}); ++calls;
  }
  for(unsigned members=0;members<4;++members) {
   auto machine=h.prepare({1,0x4464},{});
   const auto first=static_cast<std::uint16_t>(rom->byte(1,0x446C)|(rom->byte(1,0x446D)<<8));
   const auto second=static_cast<std::uint16_t>(rom->byte(1,0x4475)|(rom->byte(1,0x4476)<<8));
   machine.memory().write(first,static_cast<std::uint8_t>(members&1));
   machine.memory().write(second,static_cast<std::uint8_t>((members>>1)&1));
   h.run_cpp(machine,{1,0x4464}); ++calls;
  }
  {
   const auto kind=static_cast<std::uint16_t>(rom->byte(0,0x3CAE)|(rom->byte(0,0x3CAF)<<8));
   for(unsigned environment:{0u,1u,7u,8u,10u,2u,255u})
   for(unsigned requested:{0u,1u,5u,9u,31u,32u,255u}) {
    auto machine=h.prepare({0,0x3C5A},{});
    machine.memory().write(0xC308,static_cast<std::uint8_t>(environment));
    machine.memory().write(kind,static_cast<std::uint8_t>(requested));
    h.run_cpp(machine,{0,0x3C5A}); ++calls;
    unsigned expected=requested;
    if(environment==0 && requested==5) expected=4;
    if((environment==1 || environment==7) && requested==1) expected=5;
    if(environment==8) expected=6;
    if(environment==10 && requested==9) expected=8;
    if(expected==0) expected=1;
    probe::check(machine.peek(kind)==expected,"environment selects original interaction kind");
   }
   for(unsigned vehicle:{0u,1u,0x80u,0x81u}) {
    auto machine=h.prepare({0,0x3BC5},{});
    machine.memory().write(0xC202,13);
    machine.memory().write(0xC2E9,static_cast<std::uint8_t>(vehicle));
    h.run_cpp(machine,{0,0x3BC5}); ++calls;
    probe::check(machine.peek(0xC202)==((vehicle&0x80) ? 4 : vehicle ? 14 : 8),"scene cleanup chooses the original return mode");
   }
  }
  for(unsigned pattern=0;pattern<3;++pattern)
  for(unsigned width:{1u,3u,31u})
  for(unsigned height:{1u,3u}) {
   auto actual=h.prepare({1,0x615A},{});
   actual.cpu().hl.uint16_value=0xC100;
   actual.cpu().de.uint16_value=0xD100;
   actual.cpu().bc.uint16_value=static_cast<std::uint16_t>((width<<8)|height);
   for(unsigned i=0;i<1024;++i) actual.memory().write(static_cast<std::uint16_t>(0xC100+i),static_cast<std::uint8_t>(pattern==0 || (pattern==2 && (i/2)%2==0) ? 0 : (i%127)+1));
   for(unsigned i=0;i<256;++i) actual.memory().write(static_cast<std::uint16_t>(0xD100+i),0x55);
   Machine native=actual;
   auto run=[](Machine& m,RoutineRegistry* registry) {
    for(unsigned step=0;step<20000;++step) {
     m.step({},registry);
     if(m.cpu().pc.uint16_value==0x0180 && m.cpu().sp.uint16_value==0xDF02) return;
    }
    throw std::runtime_error("visual overlay did not return");
   };
   run(native,nullptr);
   run(actual,&h.registry());
   probe::check(same(actual,native),"transparent multi-row overlay complete chain");
   for(unsigned row=0;row<height;++row)
   for(unsigned column=0;column<width;++column) {
    const auto source=static_cast<std::uint16_t>(0xC100+2*(row*width+column));
    const auto target=static_cast<std::uint16_t>(0xD100+row*64+column*2);
    probe::check(actual.peek(target)==(actual.peek(source)==0 ? 0x55 : actual.peek(source)),"overlay preserves transparent cells and copies opaque cells");
   }
  }
  for(unsigned key:{0x41u,0x5Cu,0x5Du,0x5Eu})
  for(unsigned position:{0x24u,0x25u}) {
   auto machine=h.prepare({0,0x3ED9},{});
   machine.memory().write(0xC788,static_cast<std::uint8_t>(key));
   machine.memory().write(0xC781,static_cast<std::uint8_t>(position));
   h.run_cpp(machine,{0,0x3ED9}); ++calls;
  }
  for(unsigned health:{0u,1u,0x80u,0xFFu}) {
   auto machine=h.prepare({1,0x5657},{});
   machine.memory().write(0xC400,static_cast<std::uint8_t>(health));
   h.run_cpp(machine,{1,0x5657}); ++calls;
  }
  for(bool answer:{false,true}) {
   auto machine=h.prepare({1,0x568A},{});
   machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(answer ? 0x40 : 0);
   h.run_cpp(machine,{1,0x568A}); ++calls;
  }
  const auto money_address=static_cast<std::uint16_t>(rom->byte(1,0x5128)|(rom->byte(1,0x5129)<<8));
  const auto cake_cost=static_cast<std::uint16_t>(rom->byte(1,0x5125)|(rom->byte(1,0x5126)<<8));
  for(unsigned balance:{unsigned(cake_cost-1),unsigned(cake_cost),unsigned(cake_cost+1),65535u}) {
   auto machine=h.prepare({1,0x5124},{});
   machine.memory().write(money_address,static_cast<std::uint8_t>(balance));
   machine.memory().write(static_cast<std::uint16_t>(money_address+1),static_cast<std::uint8_t>(balance>>8));
   h.run_cpp(machine,{1,0x5124}); ++calls;
  }
  const auto capacity_address=static_cast<std::uint16_t>(rom->byte(1,0x5136)|(rom->byte(1,0x5137)<<8));
  const unsigned capacity=rom->byte(1,0x5139);
  for(unsigned used:{0u,capacity-1,capacity,capacity+1}) {
   auto machine=h.prepare({1,0x5135},{});
   machine.memory().write(capacity_address,static_cast<std::uint8_t>(used));
   h.run_cpp(machine,{1,0x5135}); ++calls;
  }
  const auto mode_address=static_cast<std::uint16_t>(rom->byte(1,0x54F3)|(rom->byte(1,0x54F4)<<8));
  for(unsigned mode:{0u,2u,3u,255u}) {
   auto machine=h.prepare({1,0x54F2},{});
   machine.memory().write(mode_address,static_cast<std::uint8_t>(mode));
   h.run_cpp(machine,{1,0x54F2}); ++calls;
  }
  const auto progress_address=static_cast<std::uint16_t>(rom->byte(1,0x51C3)|(rom->byte(1,0x51C4)<<8));
  for(unsigned state:{0u,1u,2u,255u}) {
   auto machine=h.prepare({1,0x51C2},{});
   machine.memory().write(progress_address,static_cast<std::uint8_t>(state));
   h.run_cpp(machine,{1,0x51C2}); ++calls;
  }
  const auto hp_address=static_cast<std::uint16_t>(rom->byte(1,0x54AE)|(rom->byte(1,0x54AF)<<8));
  for(unsigned hp:{0u,1u,128u,255u}) {
   auto machine=h.prepare({1,0x549F},{});
   machine.memory().write(hp_address,static_cast<std::uint8_t>(hp));
   h.run_cpp(machine,{1,0x549F}); ++calls;
  }
  const auto status_address=static_cast<std::uint16_t>(rom->byte(1,0x56A3)|(rom->byte(1,0x56A4)<<8));
  for(unsigned mask=0;mask<16;++mask) {
   auto machine=h.prepare({1,0x56A2},{});
   machine.memory().write(status_address,static_cast<std::uint8_t>(mask));
   h.run_cpp(machine,{1,0x56A2}); ++calls;
  }
  for(unsigned remaining:{1u,2u,4u}) for(bool alive:{false,true}) {
   auto machine=h.prepare({1,0x56C3},{});
   machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(remaining);
   machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(alive ? 0 : 0x40);
   h.run_cpp(machine,{1,0x56C3}); ++calls;
  }
  const auto command_address=static_cast<std::uint16_t>(rom->byte(1,0x574F)|(rom->byte(1,0x5750)<<8));
  const auto command_table=static_cast<std::uint16_t>(rom->byte(1,0x574A)|(rom->byte(1,0x574B)<<8));
  for(unsigned current=0;current<3;++current) for(unsigned chosen=0;chosen<3;++chosen)
  for(bool confirm:{false,true}) if(current!=chosen) {
   auto machine=h.prepare({1,0x573F},{});
   const auto sp=machine.cpu().sp.uint16_value;
   machine.memory().write(sp,static_cast<std::uint8_t>(current));
   machine.memory().write(static_cast<std::uint16_t>(sp+1),static_cast<std::uint8_t>(chosen));
   machine.memory().write(static_cast<std::uint16_t>(sp+2),0x80);
   machine.memory().write(static_cast<std::uint16_t>(sp+3),1);
   machine.memory().write(command_address,0x5A);
   machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(confirm ? 0x40 : 0);
   h.run_cpp(machine,{1,0x573F}); ++calls;
   probe::check(machine.peek(command_address)==(confirm ? rom->byte(1,static_cast<std::uint16_t>(command_table+current*3+chosen)) : 0x5A),
       "destination confirmation reads the cartridge request table; refusal preserves the pending request");
   if(confirm) probe::check(machine.cpu().pc.uint16_value==0x0180,"confirmed destination restores the caller stack");
  }
  // Every segment again with work RAM randomized (a third of the bytes zero),
  // so that the branches that read memory run both ways.
  for(const auto& [entry,unit]:game::scenes::segments) {
   for(unsigned seed=0;seed<32;++seed) {
    probe::Setup setup; setup.seed=seed*3+1; setup.r=static_cast<std::uint8_t>(seed*9);
    setup.cartridge_ram=entry==RoutineEntry{0,0x0847};   // the erase's entry guard
    auto machine=h.prepare(entry,setup);
    random_ram(machine,seed);
    special(machine,entry,seed);
    h.run_cpp(machine,entry); ++calls;
   }
  }
  // The instruction after an EI, with the frame interrupt due at every
  // distance around it: the EI and its load run as two segments, each step
  // compared with the oracle (the interrupt delay keeps the load in front of
  // the interrupt).
  for(const auto& load:game::scenes::ei_delay_scaffolding) {
   const RoutineEntry ei{load.instruction.bank,static_cast<std::uint16_t>(load.instruction.address-1)};
   probe::check(h.registry().find(ei) && h.registry().find(load.instruction) && h.registry().find(load.instruction)->spec.single_instruction,
       "the EI and the load after it are one-instruction segments");
   for(unsigned remaining=1;remaining<=14;++remaining) {
    probe::Setup setup; setup.iff1=true; setup.iff2=true; setup.seed=remaining;
    auto machine=h.prepare(ei,setup);
    special(machine,load.suffix,remaining);
    const auto phase=machine.cycles()%Machine::frame_cycles;
    machine.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
    oracle::Lockstep lockstep(machine,"EI and its load",oracle::Mode::instructions);
    lockstep.step(machine,&h.registry());
    lockstep.step(machine,&h.registry());
    ++calls;
   }
  }
  // The interrupt sweep (recipe step 5): an IRQ and an NMI at every cycle of
  // every segment that has an instruction boundary inside its body, each run
  // under strict lockstep (every cycle with PSMODERN_FULL_SWEEPS set; by
  // default about 64 positions per kind spread over each body: probe::auto_stride). The state is the probes' (seeded registers, random
  // work RAM, the segment's own set-up above).
  unsigned swept=0;
  for(const auto& [entry,unit]:game::scenes::segments) {
   const auto* routine=h.registry().find(entry);
   if(!routine || !has_boundary_inside(*routine)) continue;
   probe::Setup setup; setup.seed=entry.address%29+1;
   setup.cartridge_ram=entry==RoutineEntry{0,0x0847};   // the erase's entry guard
   // A conditional return at the entry (RET Z, RET NZ, RET C) ends the run
   // at once on one side of its flag: the flags are varied until the run
   // has a body to interrupt (the first variant that delivers inside it).
   bool delivered=false;
   for(unsigned variant=0;variant<5 && !delivered;++variant) {
    try {
     probe::sweep(h,entry,setup,[&](Machine& machine) {
      random_ram(machine,entry.address%31,0xC300,0xC600);
      special(machine,entry,entry.address%31);
      auto& flags=machine.cpu().af.uint8_values.at_0;
      if(variant==1) flags|=0x40;
      if(variant==2) flags&=static_cast<std::uint8_t>(~0x40);
      if(variant==3) flags|=0x01;
      if(variant==4) flags&=static_cast<std::uint8_t>(~0x01);
     },probe::auto_stride);
     delivered=true;
    } catch(const std::runtime_error& error) {
     if(std::string_view(error.what()).find("nothing was delivered")==std::string_view::npos || variant==4) throw;
    }
   }
   ++swept;
  }
  // The cartridge-RAM header check (00:07DF, 00:07E2) with the header mapped and
  // equal to its ROM copy, different at its first byte, or at its last: the
  // hand-rendered CPI loop (32 comparisons, about 1,500 cycles) with the
  // interrupts landing at every fifth cycle.
  for(const RoutineEntry entry:{RoutineEntry{0,0x07DF},RoutineEntry{0,0x07E2}})
  for(unsigned mismatch:{99u,0u,31u}) {
   probe::Setup setup; setup.cartridge_ram=true; setup.seed=mismatch+5;
   probe::sweep(h,entry,setup,[&](Machine& machine) {
    machine.cpu().bc.uint16_value=0x1000;
    for(unsigned i=0;i<32;++i) machine.memory().write(static_cast<std::uint16_t>(0x8001+i),rom->byte(0,static_cast<std::uint16_t>(0x0808+i)));
    if(mismatch<32) machine.memory().write(static_cast<std::uint16_t>(0x8001+mismatch),static_cast<std::uint8_t>(machine.peek(static_cast<std::uint16_t>(0x8001+mismatch))^0x80));
   },5);
  }
  // The erase (00:0847) with cartridge RAM mapped, swept: the zero seed and
  // the setup before it.
  {
   probe::Setup setup; setup.cartridge_ram=true;
   probe::sweep(h,{0,0x0847},setup);
  }
  // The exits that depend on one particular value, each with a probe that
  // sets it (the others are found by the search below).
  {
   struct Target {RoutineEntry entry;std::function<void(Machine&)> prepare;};
   const std::array<Target,8> targets{
    Target{{0,0x3FF2},[](Machine& m) {m.memory().write(0xC784,0xC8);}},                    // the cursor is at its stop coordinate
    Target{{1,0x5438},[](Machine& m) {m.memory().write(0xC43B,0x18);}},                    // Noah already wears the prize armour
    Target{{1,0x56B5},[](Machine& m) {m.memory().write(m.cpu().sp.uint16_value,3);}},      // the fourth member's turn is done
    Target{{1,0x5CAA},[](Machine& m) {m.memory().write(0xC82E,0);m.memory().write(0xC821,0x3C);}},  // the last frame of the chest
    Target{{1,0x5D0D},[](Machine& m) {m.memory().write(static_cast<std::uint16_t>(m.cpu().sp.uint16_value+1),0x50);}}, // the last frame
    Target{{1,0x5D7E},[](Machine& m) {m.memory().write(0xC824,0x91);}},                    // the fixed column
    Target{{1,0x5D8F},[](Machine& m) {m.memory().write(0xC822,0x77);}},                    // the fixed row
    Target{{1,0x6427},[](Machine& m) {m.memory().write(static_cast<std::uint16_t>(m.cpu().sp.uint16_value+3),1);}}}; // the last strip
   for(const auto& target:targets) {
    probe::Setup setup; setup.seed=3;
    auto machine=h.prepare(target.entry,setup);
    machine.cpu().ix_iy[0].uint16_value=0xC820;machine.cpu().ix_iy[1].uint16_value=0xC820;
    special(machine,target.entry,3);
    target.prepare(machine);
    h.run_cpp(machine,target.entry); ++calls;
   }
  }
  // The exit search: a segment with an exit no probe above took is run again
  // with the flags (all 256 values) and a few seeds of random work RAM until
  // each of its exits has been taken, every run compared with the oracle at
  // every instruction boundary like any probe.
  auto untaken_exits=[&](const RegisteredRoutine& routine) {
   for(std::size_t i=0;i<routine.spec.exits.size() && i<routine.rendered.size();++i)
    if(!routine.rendered[i] && routine.spec.exits[i].kind!=RoutineExit::copy_repeat) return true;
   return false;
  };
  for(const auto& [entry,routine]:h.registry().entries()) {
   if(!is_scene_file(routine.origin) || !untaken_exits(routine)) continue;
   for(unsigned seed=0;seed<6 && untaken_exits(*h.registry().find(entry));++seed)
   for(unsigned flags=0;flags<256 && untaken_exits(*h.registry().find(entry));++flags) {
    probe::Setup setup; setup.seed=seed*5+flags; setup.r=static_cast<std::uint8_t>(flags*9);
    setup.cartridge_ram=entry==RoutineEntry{0,0x0847};
    auto machine=h.prepare(entry,setup);
    if(seed) random_ram(machine,seed,0xC300,0xC600);
    special(machine,entry,seed);
    machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(flags);
    h.run_cpp(machine,entry); ++calls;
   }
  }
  // Then, for what is left, work RAM filled with small values (the compared
  // bytes are mostly small numbers or one of a few usual limits), many seeds.
  for(const auto& [entry,routine]:h.registry().entries()) {
   if(!is_scene_file(routine.origin) || !untaken_exits(routine)) continue;
   for(unsigned seed=1;seed<=600 && untaken_exits(*h.registry().find(entry));++seed) {
    probe::Setup setup; setup.seed=seed; setup.r=static_cast<std::uint8_t>(seed*9);
    setup.cartridge_ram=entry==RoutineEntry{0,0x0847};
    auto machine=h.prepare(entry,setup);
    std::mt19937_64 random(seed*104729u+entry.address);
    static constexpr std::uint8_t usual[]={0,1,2,3,4,5,6,7,8,9,10,11,12,0x25,0x38,0x5C,0x80,0xE0,0xFF};
    for(unsigned address=0xC000;address<0xC900;++address)
     machine.memory().write(static_cast<std::uint16_t>(address),usual[random()%std::size(usual)]);
    special(machine,entry,seed);
    // The pointers stay in work RAM (the game's pointers do; one into the
    // mapper registers would change the code under the segment).
    auto clamp=[](std::uint16_t value) { return static_cast<std::uint16_t>(value<0xC000 || value>=0xDF00 ? 0xC000+(value&0x7FF) : value); };
    machine.cpu().hl.uint16_value=clamp(machine.cpu().hl.uint16_value);
    machine.cpu().de.uint16_value=clamp(machine.cpu().de.uint16_value);
    machine.cpu().ix_iy[0].uint16_value=clamp(machine.cpu().ix_iy[0].uint16_value);
    machine.cpu().ix_iy[1].uint16_value=clamp(machine.cpu().ix_iy[1].uint16_value);
    machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(random());
    machine.cpu().af.uint8_values.at_1=usual[random()%std::size(usual)];
    try { h.run_cpp(machine,entry); } catch(const std::exception& e) { throw std::runtime_error("exit search "+std::to_string(entry.bank)+":"+std::to_string(entry.address)+" seed "+std::to_string(seed)+": "+e.what()); }
    ++calls;
   }
  }
  // Every exit of every scene segment was taken by some probe (the
  // transitions no recording reaches are verified only by them).
  unsigned untaken=0;
  for(const auto& [entry,routine]:h.registry().entries()) {
   if(!is_scene_file(routine.origin)) continue;
   for(std::size_t i=0;i<routine.spec.exits.size() && i<routine.rendered.size();++i)
    // 01:4100 stores 38h and decrements it before its RET M: that exit
    // cannot be taken from this entry (the sheet lists it as the instruction's
    // other branch; the loop at 01:4107 takes it). 01:6427 runs the first
    // 128 cycles of a 128-byte strip: the strip's end (01:643F) is the
    // resumed entry's (01:6438), which takes it.
    if(!routine.rendered[i] && routine.spec.exits[i].kind!=RoutineExit::copy_repeat && !(entry==RoutineEntry{1,0x4100} && routine.spec.exits[i].address==0x4108) &&
        !(entry==RoutineEntry{1,0x6427} && routine.spec.exits[i].address==0x643F)) {
     std::cout<<"exit not taken: "<<entry.bank<<":"<<std::hex<<entry.address<<" exit "<<routine.spec.exits[i].address<<std::dec<<"\n";
     ++untaken;
    }
  }
  probe::check(untaken==0,"every exit of every scene segment is taken by a probe");
  std::cout<<swept<<" scene segments swept\n";
  std::cout<<calls<<" verified scene segments\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
