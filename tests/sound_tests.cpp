// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include "game/sound.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <set>
#include <tuple>
using namespace psmodern;
namespace {
void sound_write_callbacks(probe::Harness& harness,const probe::Rom& rom) {
    // Keep the authentic sound bank. Authored IRQ/NMI stubs isolate window
    // admission from unrelated game state and return to the interrupted code.
    auto image=std::make_shared<Memory::Rom>(*rom.image);
    const std::array<std::uint8_t,7> irq{0xF5,0xDB,0xBF,0xF1,0xFB,0xED,0x4D};
    std::copy(irq.begin(),irq.end(),image->begin()+0x0038);
    image->at(0x0066)=0xED;image->at(0x0067)=0x45;
    std::set<std::uint16_t> rendered_sites;
    unsigned cases=0;
    for(unsigned entry:{0x857Eu,0x85ADu,0x85EDu,0x8618u,0x8234u})
    for(unsigned choice=0;choice<(entry==0x8234 ? 1u : 2u);++choice)
    for(unsigned fill:{0u,0x55u,0x80u,0xFFu})
    for(unsigned record:{0xC000u,0xC200u,0xDFE8u})
    for(unsigned event=0;event<3;++event) {
        Machine prepared(image);
        (void)prepared.read_port(0xBF,prepared.cycles());prepared.advance(1);
        prepared.memory().write(0xFFFC,0);prepared.memory().write(0xFFFE,1);prepared.memory().write(0xFFFF,12);
        prepared.cpu().pc.uint16_value=static_cast<std::uint16_t>(entry);
        prepared.cpu().sp.uint16_value=0xDF00;
        prepared.cpu().ix_iy[0].uint16_value=static_cast<std::uint16_t>(record);
        prepared.cpu().hl.uint16_value=static_cast<std::uint16_t>(entry==0x8234 ? (record==0xC000 ? 0xC08E : 0xC1AE) : record);
        prepared.cpu().de.uint16_value=0xC300;
        prepared.cpu().af.uint16_value=0xA5D7;
        prepared.cpu().r=static_cast<std::uint8_t>(fill+entry);
        prepared.cpu().iff1=prepared.cpu().iff2=0;
        for(unsigned i=0;i<64;++i) prepared.memory().write(static_cast<std::uint16_t>(record+i),static_cast<std::uint8_t>(fill));
        if(entry==0x8234) {
            const auto stride=static_cast<std::uint16_t>(rom.byte(12,0x8235)|(rom.byte(12,0x8236)<<8));
            for(unsigned i=0;i<3;++i) prepared.memory().write(static_cast<std::uint16_t>(prepared.cpu().hl.uint16_value+i*stride),static_cast<std::uint8_t>(fill));
        }
        prepared.memory().write(0xDF00,0x80);prepared.memory().write(0xDF01,1);
        const auto compare_at=entry==0x857E ? 0x8580 : entry==0x85ED ? 0x85EF : 0x861A;
        const auto data=entry==0x85AD ? (choice ? 0xFFu : 0u) :
            static_cast<unsigned>(static_cast<std::uint8_t>(rom.byte(12,static_cast<std::uint16_t>(compare_at))+choice));
        prepared.memory().write(0xC300,static_cast<std::uint8_t>(data));
        const auto routine=harness.registry().find({12,static_cast<std::uint16_t>(entry)});
        probe::check(routine!=nullptr,"sound callback fixture has an independently cataloged entry");
        const auto bound=routine->bound(prepared);
        probe::check(bound>1 && bound<1000,"sound callback bound: entry="+std::to_string(entry)+" record="+std::to_string(record)+" bound="+std::to_string(bound));
        for(auto distance:{std::uint64_t{0},std::uint64_t{1},bound-1,bound,bound+1,std::uint64_t{1000}}) {
            if(event==0 && distance==0) continue;
            Machine actual=prepared;
            if(event==0) {
                const auto phase=actual.cycles()%Machine::frame_cycles;
                actual.advance((Machine::frame_cycles-distance+Machine::frame_cycles-phase)%Machine::frame_cycles);
            }
            else if(event==1) {
                actual.write_port(0xBF,0x20,actual.cycles());actual.write_port(0xBF,0x81,actual.cycles());
                const auto irq_at=actual.vdp().next_irq();
                probe::check(irq_at && *irq_at>=actual.cycles()+distance,"sound fixture has a future IRQ");
                actual.advance(*irq_at-actual.cycles()-distance);actual.cpu().iff1=actual.cpu().iff2=1;
            } else actual.schedule_nmi(actual.cycles()+distance);
            Machine native=actual;
            // This fixture registers the one sound segment only; the rest of
            // the program (callers, handlers) runs on the oracle's core.
            oracle::Scaffold partial(oracle::Scaffold::authored);
            RoutineRegistry registry;
            if(routine->guard) registry.add_exact({12,static_cast<std::uint16_t>(entry)},routine->spec,*routine->guard,routine->bound,routine->body);
            else if(routine->exact) registry.add_exact({12,static_cast<std::uint16_t>(entry)},routine->spec,routine->bound,routine->body);
            else registry.add({12,static_cast<std::uint16_t>(entry)},routine->spec,routine->bound,routine->body);
            using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t,Memory::Kind>;
            std::vector<Write> expected,observed;
            std::set<std::uint16_t> native_sites;
            Machine::PortTrace expected_ports(native),actual_ports(actual);
            struct Stopped {};
            auto run=[&](Machine& machine,RoutineRegistry* replacements,std::vector<Write>& writes) {
                std::uint16_t pc=0;std::uint64_t start=0;
                Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                    writes.emplace_back(address,value,machine.cycles(),machine.memory().mapping(address).kind);
                    if(replacements==nullptr && pc>=0x8000 && machine.peek(pc)==0xDD) {
                        const auto displacement=static_cast<std::int8_t>(machine.peek(static_cast<std::uint16_t>(pc+2)));
                        const auto indexed=static_cast<std::uint16_t>(machine.cpu().ix_iy[0].uint16_value+displacement);
                        if(address==indexed) {
                            probe::check(machine.cycles()==start+4,"native sound indexed callback follows the DD prefix by exactly four cycles");
                            native_sites.insert(pc);
                        }
                    }
                    if(replacements==nullptr && pc==0x8239 && address==machine.cpu().hl.uint16_value) {
                        probe::check(machine.cycles()==start,"unindexed CB RES callback stays at instruction entry");
                        native_sites.insert(pc);
                    }
                };
                Machine::Hook hook=[&](std::uint16_t instruction,Memory::Mapping)->std::optional<std::uint16_t> {
                    pc=instruction;start=machine.cycles();
                    if(pc==0x0180) throw Stopped{};
                    return {};
                };
                try {for(unsigned frame=0;frame<4;++frame) machine.run_frame({},replacements,hook,observer);}
                catch(const Stopped&) {return;}
                throw std::runtime_error("sound callback fixture did not restore its caller");
            };
            run(native,nullptr,expected);run(actual,&registry,observed);
            probe::check(oracle::equal(actual,native,oracle::Scope::whole) && actual.memory().cartridge_ram_dirty()==native.memory().cartridge_ram_dirty(),"sound callback fix preserves CPU/WZ/R, RAM, VDP/PSG and IRQ/NMI state");
            probe::check(expected_ports.accesses()==actual_ports.accesses(),"sound callbacks preserve every PSG/VDP port value, order and cycle");
            probe::check(expected==observed,"sound writes preserve every address, value, order, mapping and callback cycle; entry="+std::to_string(entry));
            if(registry.find({12,static_cast<std::uint16_t>(entry)})->counters.cpp_calls)
                rendered_sites.insert(native_sites.begin(),native_sites.end());
            ++cases;
        }
    }
    for(unsigned pc:{0x8583u,0x8587u,0x85B7u,0x8605u,0x8622u,
                     0x858Cu,0x8590u,0x85BCu,0x85F2u,0x861Du,
                     0x85FEu,0x8601u,0x8611u,0x8614u,0x8239u})
        probe::check(rendered_sites.contains(static_cast<std::uint16_t>(pc)),"sound callback matrix executed every RES/SET and indexed INC/DEC site in C++");
    std::cout<<cases<<" sound memory-callback windows match the original, including all five local indexed RES sites.\n";
}
// Milestone 14 (Part C6): every sound segment under many seeded states. A state puts IX, HL, DE and BC
// at the voice records and ports the driver uses, and randomizes only the bytes of those records (at most
// 0x30 bytes at IX, HL and DE each: the driver RAM the segments read), never the whole work RAM, so that the
// nested sound tick cannot overflow the PSG queue. Each admitted run goes under strict lockstep
// (Harness::run_cpp, which also runs the opcode check of every rendered final); the exit address and the
// address the run continued at are recorded, so that an exit or a branch direction no state reaches is
// reported instead of silently untested. A segment with more than one instruction also gets the forced
// IRQ/NMI sweep (probe::sweep) with the IRQ enabled.
struct Coverage { std::set<std::tuple<std::uint16_t,std::uint16_t,std::uint16_t>> seen; };
void prepare_state(const probe::Rom& rom,RoutineEntry entry,unsigned n,Machine& machine) {
    std::mt19937_64 random(n*0x9e3779b97f4a7c15ull+entry.address);
    static constexpr std::uint16_t records[]={0xc040,0xc0a0,0xc120,0xc180,0xc1c0,0xc00e,0xc14e};
    static constexpr std::uint16_t shared[]={0xc12e,0xc08e,0xc18e,0xc1ae,0xc00c,0xc12e};
    auto& cpu=machine.cpu();
    const auto ix=records[random()%7], hl=shared[random()%6], de=static_cast<std::uint16_t>(0xc000+(random()%0x1f0));
    cpu.ix_iy[0].uint16_value=ix; cpu.hl.uint16_value=hl; cpu.de.uint16_value=de;
    if(random()%2) cpu.bc.uint8_values.at_0=0x7f;
    cpu.bc.uint8_values.at_1=static_cast<std::uint8_t>(random()%12);
    static constexpr std::uint8_t pool[]={0,1,2,3,0x0a,0x0b,0x0c,0x0f,0x10,0x7f,0x80,0x81,0x8a,0x8b,0xff};
    auto byte=[&]()->std::uint8_t { const auto v=random(); return v%3==0 ? pool[(v>>8)%15] : v%5==0 ? 0 : v%7==0 ? 0xff : static_cast<std::uint8_t>(v>>8); };
    // The driver's own variables (C001, C002, C005, C006, C009-C00B) and the level fields of the ten voice records the fade and
    // the timer walk (C00E.. and C14E.., stride 0x20): 7 + 30 bytes in all.
    for(unsigned i:{1u,2u,5u,6u,9u,10u,11u}) machine.memory().write(static_cast<std::uint16_t>(0xc000+i),byte());
    for(unsigned record=0;record<10;++record) for(unsigned field:{0u,8u,23u})
        machine.memory().write(static_cast<std::uint16_t>((record<6 ? 0xc00eu : 0xc14eu - 6*0x20)+record*0x20+field),byte());
    for(auto base:{ix,hl,de}) for(unsigned i=0;i<0x30;++i) machine.memory().write(static_cast<std::uint16_t>(base+i),byte());
    // The return address of an inherited-stack segment, when the unit is entered after its caller pushed it.
    machine.memory().write(static_cast<std::uint16_t>(cpu.sp.uint16_value+4),0x80);
    machine.memory().write(static_cast<std::uint16_t>(cpu.sp.uint16_value+5),0x01);
    // Fade phase ends (the timer reaching zero at the last step of a phase, either direction), and the
    // envelope sentinel byte the tone voice compares with.
    if(entry.address==0x80f0 && n%3==0) {
        machine.memory().write(0xc00b,1);
        machine.memory().write(0xc00a,static_cast<std::uint8_t>(random()%2 ? 0x0b : 0x8a));
        machine.memory().write(0xc002,static_cast<std::uint8_t>(random()%3==0 ? 0x7f : random()%2 ? 0x05 : 0x85));
        machine.memory().write(0xc006,static_cast<std::uint8_t>(random()%2));
    }
    if(entry.address==0x8791 || entry.address==0x87ad) {
        const auto sentinel=rom.byte(12,0x87b6);
        if(random()%2) {
            const auto flags=static_cast<std::uint16_t>(ix+static_cast<std::int8_t>(rom.byte(12,0x87af)));
            const auto level=static_cast<std::uint16_t>(ix+static_cast<std::int8_t>(rom.byte(12,0x87b3)));
            machine.memory().write(flags,static_cast<std::uint8_t>(machine.memory().read(flags)&~4u));
            machine.memory().write(level,sentinel);
        }
    }
}
bool admitted(probe::Harness& h,RoutineEntry entry,const Machine& machine) {
    const auto& routine=h.registry().find(entry)->bound;
    const auto* r=h.registry().find(entry);
    return (!r->guard || r->guard->holds(machine)) && routine(machine)<Machine::frame_cycles;
}
void sound_exit_probes_and_sweeps(probe::Harness& h,const probe::Rom& rom) {
    Coverage coverage;
    unsigned probes=0,swept=0,skipped=0;
    std::vector<std::pair<RoutineEntry,std::uint16_t>> all_exits;
    for(const auto& segment:game::sound::segments) {
        const auto* r=h.registry().find(segment.entry);
        probe::check(r!=nullptr,"sound segment is registered");
        for(const auto& exit:r->spec.exits) all_exits.emplace_back(segment.entry,exit.address);
        bool did_sweep=false;
        for(unsigned n=0;n<400;++n) {
            probe::Setup setup; setup.seed=n; setup.iff1=setup.iff2=true; setup.r=static_cast<std::uint8_t>(n*7);
            auto machine=h.prepare(segment.entry,setup);
            prepare_state(rom,segment.entry,n,machine);
            if(!admitted(h,segment.entry,machine)) continue;
            try {
                std::optional<SegmentExit> exit;
                h.exit_sink=&exit;
                h.run_cpp(machine,segment.entry);
                h.exit_sink=nullptr;
                ++probes;
                if(exit) coverage.seen.emplace(segment.entry.address,exit->address,static_cast<std::uint16_t>(machine.cpu().pc.uint16_value));
                if(!did_sweep && !r->spec.single_instruction) {
                    did_sweep=true; ++swept;
                    // Every cycle with PSMODERN_FULL_SWEEPS set; by default about 64
                    // positions per kind over the whole body (probe::auto_stride).
                    probe::sweep(h,segment.entry,setup,[&](Machine& m) { prepare_state(rom,segment.entry,n,m); },probe::auto_stride);
                }
            } catch(const std::exception& e) {
                h.exit_sink=nullptr;
                throw std::runtime_error("sound segment 12:"+std::to_string(segment.entry.address)+" state "+std::to_string(n)+": "+e.what());
            }
        }
        if(!r->spec.single_instruction && !did_sweep) { ++skipped; std::cout<<"NOT SWEPT (never admitted): "<<std::hex<<segment.entry.address<<std::dec<<"\n"; }
    }
    unsigned missing=0;
    for(const auto& [entry,at]:all_exits) {
        bool any=false;
        for(const auto& t:coverage.seen) if(std::get<0>(t)==entry.address && std::get<1>(t)==at) any=true;
        if(!any) { ++missing; std::cout<<"EXIT NOT REACHED: segment "<<std::hex<<entry.address<<" exit "<<at<<std::dec<<"\n"; }
    }
    probe::check(missing==0 && skipped==0,"a sound exit is reached by no probe, or a multi-instruction segment was never swept");
    std::cout<<"sound exit probes: "<<probes<<" runs, "<<swept<<" segments swept, "<<missing<<" exits not reached, "<<skipped<<" not swept\n";
}
}

int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
 try {
    if(argc!=3) return 1;
    auto rom=probe::load(argv[1],argv[2]); if(!rom) return 77;
    probe::Harness h(*rom);
    sound_write_callbacks(h,*rom);
    for(unsigned seed=0;seed<64;++seed) {
        probe::Setup setup;
        setup.seed=seed;
        setup.r=static_cast<std::uint8_t>(seed*7);
        for(RoutineEntry entry: {RoutineEntry{12,0x8000},RoutineEntry{12,0x800C},RoutineEntry{12,0x8011},RoutineEntry{12,0x801F},RoutineEntry{12,0x802D},RoutineEntry{12,0x8127},RoutineEntry{12,0x8420},RoutineEntry{12,0x8426},RoutineEntry{12,0x842F}}) {
            auto machine=h.prepare(entry,setup);
            h.run_cpp(machine,entry);
        }
        setup.return_address=0x7777;
        auto machine=h.prepare({12,0x802A},setup);
        machine.memory().write(setup.sp+4,0x80);
        machine.memory().write(setup.sp+5,0x01);
        h.run_cpp(machine,{12,0x802A});
    }
    for (unsigned n:{0u,1u,2u,7u,8u,16u,32u,255u}) {
        probe::Setup setup;
        setup.seed=n;
        auto machine=h.prepare({12,0x8028},setup);
        machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(n);
        machine.cpu().bc.uint8_values.at_0=0x7f;
        machine.cpu().hl.uint16_value=0x803b;
        machine.memory().write(setup.sp+4,0x80);
        machine.memory().write(setup.sp+5,0x01);
        h.run_cpp(machine,{12,0x8028});
    }
    {
        auto machine=h.prepare({12,0x8028},{});
        machine.cpu().bc.uint8_values.at_0=0xbf;
        h.run_fallback(machine,{12,0x8028},RoutineFallback::guard);
    }
    // Compare each actual PSG access with the native transfer, including
    // offsets from the same starting cycle and bytes read from the ROM.
    {
        probe::Setup setup; auto cpp=h.prepare({12,0x8028},setup);
        cpp.cpu().bc.uint8_values.at_1=8;
        cpp.cpu().bc.uint8_values.at_0=0x7f;
        cpp.cpu().hl.uint16_value=0x803b;
        cpp.memory().write(setup.sp+4,0x80);
        cpp.memory().write(setup.sp+5,0x01);
        auto native=cpp; const auto start=cpp.cycles();
        Machine::PortTrace native_trace(native);
        while(native.cpu().pc.uint16_value!=setup.return_address) native.step();
        Machine::PortTrace cpp_trace(cpp);
        // The lockstep's own port trace would take the accesses from this one.
        h.verified=false;
        h.run_cpp(cpp,{12,0x8028});
        h.verified=true;
        const auto& a=native_trace.accesses(); const auto& b=cpp_trace.accesses();
        probe::check(a==b && b.size()==8,"ordered PSG access trace differs");
        for(unsigned i=0;i<8;++i) {
            probe::check(b[i].write && b[i].port==0x7f && b[i].cycle==start+15+21*i && b[i].value==rom->byte(12,0x803b+i),"PSG sequence or offset differs");
        }
    }
    // A pending NMI and a frame crossing must both leave the transfer native.
    for(unsigned kind=0;kind<2;++kind) {
        probe::Setup setup;
        auto machine=h.prepare({12,0x8028},setup);
        machine.cpu().bc.uint8_values.at_1=8;
        machine.cpu().bc.uint8_values.at_0=0x7f;
        machine.cpu().hl.uint16_value=0x803b;
        if(kind==0) machine.schedule_nmi(machine.cycles()+20);
        else machine.advance(Machine::frame_cycles-machine.cycles()%Machine::frame_cycles-20);
        h.run_fallback(machine,{12,0x8028},kind==0 ? RoutineFallback::nmi : RoutineFallback::frame);
    }
    for (unsigned selector=0;selector<256;++selector) {
        for (auto entry:{RoutineEntry{12,0x8426},RoutineEntry{12,0x842F}}) {
            probe::Setup setup;
            setup.seed=selector;
            auto machine=h.prepare(entry,setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(selector);
            machine.cpu().hl.uint16_value=0x823f;
            h.run_cpp(machine,entry);
        }
    }
    for (unsigned timer=0;timer<256;++timer) for (unsigned mode:{0u,1u,0x7fu,0x80u,0xffu}) {
        probe::Setup setup;
        setup.seed=timer;
        auto machine=h.prepare({12,0x80C9},setup);
        machine.memory().write(0xc001,static_cast<std::uint8_t>(timer));
        machine.memory().write(0xc002,static_cast<std::uint8_t>(mode));
        h.run_cpp(machine,{12,0x80C9});
    }
    for (unsigned phase=0;phase<256;++phase) for (unsigned counter:{0u,1u,2u}) for (unsigned mode:{0u,0x7fu,0x80u,0xffu}) {
        probe::Setup setup;
        setup.seed=phase;
        auto machine=h.prepare({12,0x80F0},setup);
        machine.memory().write(0xc00a,static_cast<std::uint8_t>(phase));
        machine.memory().write(0xc00b,static_cast<std::uint8_t>(counter));
        machine.memory().write(0xc002,static_cast<std::uint8_t>(mode));
        machine.memory().write(0xc006,static_cast<std::uint8_t>(phase&1));
        for (unsigned i=0;i<12;++i) {
            machine.memory().write(static_cast<std::uint16_t>(0xc016+32*i),static_cast<std::uint8_t>(phase+i));
            machine.memory().write(static_cast<std::uint16_t>(0xc025+32*i),static_cast<std::uint8_t>(255-phase));
        }
        h.run_cpp(machine,{12,0x80F0});
    }
    for(unsigned enabled:{0u,1u}) {
        auto machine=h.prepare({12,0x8127},{});
        machine.memory().write(0xc006,static_cast<std::uint8_t>(enabled));
        h.run_cpp(machine,{12,0x8127});
    }
    for (unsigned value=0;value<256;++value) {
        for(auto entry:{RoutineEntry{12,0x81CD},RoutineEntry{12,0x81D5},RoutineEntry{12,0x81F0},RoutineEntry{12,0x81F8},RoutineEntry{12,0x8205},RoutineEntry{12,0x820D}}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare(entry,setup);
            machine.memory().write(0xc002,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc12e,static_cast<std::uint8_t>(value));
            if(entry.address==0x81F8) machine.cpu().hl.uint16_value=0xc12e;
            h.run_cpp(machine,entry);
        }
    }
    { auto machine=h.prepare({12,0x81F8},{});
    machine.cpu().hl.uint16_value=0xffff;
      h.run_fallback(machine,{12,0x81F8},RoutineFallback::guard); }
    for(unsigned seed=0;seed<64;++seed) {
        probe::Setup setup;
        setup.seed=seed;
        auto machine=h.prepare({12,0x8215},setup);
        h.run_cpp(machine,{12,0x8215});
        for(unsigned address:{0xc08eu,0xc1aeu}) {
            auto helper=h.prepare({12,0x8234},setup);
            helper.cpu().hl.uint16_value=static_cast<std::uint16_t>(address);
            h.run_cpp(helper,{12,0x8234});
        }
    }
    { auto machine=h.prepare({12,0x8234},{});
    machine.cpu().hl.uint16_value=0xffff;
      h.run_fallback(machine,{12,0x8234},RoutineFallback::guard); }
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x82f2u,0x82f7u,0x82fbu,0x8300u,0x8305u,0x830au,0x8319u,0x8321u,0x832fu,0x8334u}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x833fu,0x8356u,0x8370u,0x837au}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.memory().write(0xc00a,static_cast<std::uint8_t>(value));
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    for(unsigned value=0;value<256;++value) for(unsigned at:{0x8519u,0x851cu}) {
        probe::Setup setup;
        setup.seed=value;
        auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
        h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
    }
    for(unsigned value=0;value<256;++value) for(unsigned at:{0x8554u,0x855eu,0x8566u,0x8595u,0x85a0u,0x85a5u,0x85adu,0x85c1u,0x85cau}) {
        probe::Setup setup;
        setup.seed=value;
        auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
        machine.cpu().de.uint16_value=0xc300;
        machine.cpu().ix_iy[0].uint16_value=0xc200;
        machine.memory().write(0xc300,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc00a,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc202,static_cast<std::uint8_t>(255-value));
        machine.memory().write(0xc208,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc217,static_cast<std::uint8_t>(255-value));
        h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
    }
    { auto machine=h.prepare({12,0x85AD},{});
    machine.cpu().ix_iy[0].uint16_value=0xffff;
      h.run_fallback(machine,{12,0x85AD},RoutineFallback::guard); }
    for(unsigned value=0;value<256;++value) for(unsigned at:{0x857eu,0x85cfu,0x85e7u,0x85edu,0x8618u}) for(unsigned state:{0u,1u,2u,0xffu}) {
        probe::Setup setup;
        setup.seed=value;
        auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().de.uint16_value=0xc300;
        machine.cpu().ix_iy[0].uint16_value=0xc200;
        machine.memory().write(0xc300,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc301,0x88);
        machine.memory().write(0xc21c,static_cast<std::uint8_t>(state));
        machine.memory().write(0xc208,static_cast<std::uint8_t>(state));
        h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
    }
    for(unsigned value=0;value<256;++value) for(unsigned at:{0x8509u,0x8810u,0x8812u,0x8867u}) for(unsigned variant:{0u,1u,15u,255u}) {
        probe::Setup setup;
        setup.seed=value;
        auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
        machine.cpu().hl.uint16_value=static_cast<std::uint16_t>((value<<8)|variant);
        machine.cpu().ix_iy[0].uint16_value=0xc200;
        machine.memory().write(0xc205,static_cast<std::uint8_t>(variant));
        machine.memory().write(0xc208,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc201,static_cast<std::uint8_t>(variant));
        h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
    }
    for(unsigned value=0;value<256;++value) for(unsigned at:{0x8687u,0x86a2u,0x86b5u,0x86cbu}) for(unsigned state:{0u,1u,2u,0xffu}) {
        probe::Setup setup;
        setup.seed=value;
        auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().de.uint16_value=0xc300;
        machine.cpu().ix_iy[0].uint16_value=0xc100;
        machine.memory().write(0xc300,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc301,static_cast<std::uint8_t>(state));
        machine.memory().write(0xc109,static_cast<std::uint8_t>(value));
        machine.memory().write(static_cast<std::uint16_t>(0xc100+((value+0x18)&0xff)),static_cast<std::uint8_t>(state));
        h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
    }
    { auto machine=h.prepare({12,0x8687},{});
    machine.cpu().ix_iy[0].uint16_value=0xffff;
      h.run_fallback(machine,{12,0x8687},RoutineFallback::guard); }
    // Vary every command byte and representative voice fields, including
    // all early returns and both outcomes of each score dispatch.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x86d0u,0x86dcu,0x86f4u,0x86f9u,0x8700u,0x8706u,0x870eu,0x8715u,0x871fu,0x8723u,0x8729u}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().de.uint16_value=0xc300;
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            machine.memory().write(0xc300,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc200,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc207,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc20a,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc20b,static_cast<std::uint8_t>(255-value));
            machine.memory().write(0xc213,static_cast<std::uint8_t>(value));
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // The tonal voice covers each score value and all relevant flag bytes.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x8772u,0x877eu,0x8786u,0x8791u,0x87adu,0x87bau,0x87c2u,0x87c9u,0x87d2u,0x87dcu,0x87e0u,0x87edu,0x87f1u,0x87fau,0x8804u}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            for(unsigned offset:{0u,1u,6u,7u,10u,11u,14u,15u,18u,19u,20u})
                machine.memory().write(static_cast<std::uint16_t>(0xc200+offset),static_cast<std::uint8_t>(value));
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // Envelope markers, sample bytes, index wrapping, and the two paths that
    // inherit a saved word from the caller.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x8827u,0x8836u,0x883au,0x883eu,0x8842u,0x8846u,0x8852u,0x8855u,0x885bu}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().bc.uint16_value=0xc300;
            machine.cpu().hl.uint16_value=0xc300;
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            machine.memory().write(0xc300,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc301,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc20c,0);
            machine.memory().write(0xc208,static_cast<std::uint8_t>(value));
            if(at==0x8846 || at==0x8855) {
                machine.memory().write(setup.sp+2,0x80);
                machine.memory().write(setup.sp+3,0x01);
            }
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // Each frame selects a group and visits every active voice. Probe both
    // outcomes of its branches, calls and inherited-counter exits.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x8053u,0x805au,0x8060u,0x8066u,0x806du,0x8076u,0x807fu,0x8085u,0x808bu,0x8092u,0x809bu}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            machine.memory().write(0xc002,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc200,static_cast<std::uint8_t>(value));
            machine.memory().write(setup.sp,static_cast<std::uint8_t>(value));
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // Voice setup copies score bytes, preserves the caller's count, then
    // schedules the original PSG writer. Exercise every initial count.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x83f4u,0x83f8u,0x841du}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().hl.uint16_value=0x9000;
            machine.cpu().de.uint16_value=0xc100;
            machine.memory().write(setup.sp,static_cast<std::uint8_t>(value));
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    {
        auto machine=h.prepare({12,0x83F8},{});
        machine.cpu().de.uint16_value=0xfff8;
        h.run_fallback(machine,{12,0x83F8},RoutineFallback::guard);
    }
    // Score-note branches, signed pitch shifts and every byte-sized duration
    // multiplier are checked against the original, including wraparound.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x846du,0x8473u,0x8474u,0x8475u,0x847au,0x8481u,0x848bu,0x8494u,0x849bu,0x84aau,0x84b1u,0x84b6u,0x84beu,0x84bfu,0x84c5u,0x84c6u,0x84c9u,0x84cfu,0x84d0u,0x84e8u}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().de.uint16_value=0xc300;
            machine.cpu().hl.uint16_value=0xc300;
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            machine.memory().write(0xc300,static_cast<std::uint8_t>(value));
            for(unsigned offset:{0u,2u,3u,4u,5u,14u,15u,21u})
                machine.memory().write(static_cast<std::uint16_t>(0xc200+offset),static_cast<std::uint8_t>(value));
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // Pitch-envelope markers and the saved-word exit use the same proof
    // as the loudness envelope, with all possible score bytes.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x843cu,0x844cu,0x8450u,0x8454u,0x845bu,0x845fu,0x8462u}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().bc.uint16_value=0xc300;
            machine.cpu().de.uint16_value=0xc400;
            machine.cpu().hl.uint16_value=0xc300;
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            machine.memory().write(0xc300,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc301,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc20d,0);
            if(at==0x8454) {
                machine.memory().write(setup.sp+2,0x80);
                machine.memory().write(setup.sp+3,0x01);
            }
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // The fade-in prepares six records and restores IX; the silence CALL
    // preceding this entry remains native and is covered separately.
    for(unsigned value=0;value<256;++value) {
        probe::Setup setup;
        setup.seed=value;
        auto machine=h.prepare({12,0x8199},setup);
        machine.memory().write(0xc005,static_cast<std::uint8_t>(value));
        for(unsigned voice=0;voice<6;++voice)
            machine.memory().write(static_cast<std::uint16_t>(0xc016+32*voice),static_cast<std::uint8_t>(value+voice));
        h.run_cpp(machine,{12,0x8199});
    }
    // Effect request variants select the ROM table and shared channels.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x836fu,0x837du,0x8382u,0x8389u,0x8391u,0x839au,0x839eu,0x83a2u,0x83acu,0x83beu,0x83c7u,0x83d9u,0x83e6u}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().bc.uint16_value=0xc300;
            machine.cpu().hl.uint16_value=0xc300;
            for(unsigned offset=0;offset<16;++offset)
                machine.memory().write(static_cast<std::uint16_t>(0xc300+offset),static_cast<std::uint8_t>(value));
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // The noise-pattern selector depends on bits of every possible score
    // value and can emit a PSG quiet command.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x872du,0x8731u,0x8735u,0x8739u,0x873du,0x8741u,0x874bu,0x8751u,0x8760u,0x8768u,0x876cu,0x876eu}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // A finished shared effect restores muted voices and returns past two
    // saved words. Test both branch choices and ordered chip commands.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x8627u,0x8645u,0x8652u,0x8657u,0x866du,0x867au,0x867fu}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            machine.memory().write(0xc201,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc08e,static_cast<std::uint8_t>(value));
            machine.memory().write(0xc0ae,static_cast<std::uint8_t>(value));
            if(at==0x867a || at==0x867f) {
                machine.memory().write(setup.sp+4,0x80);
                machine.memory().write(setup.sp+5,0x01);
            }
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // Adjacent core EXX / EX AF operations preserve the alternate register
    // sets and the saved voice pattern across these pieces.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x86cdu,0x8757u,0x875au,0x8764u,0x878eu}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().hl.uint16_value=0xc00c;
            machine.cpu().ix_iy[0].uint16_value=0xc200;
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    {
        auto machine=h.prepare({12,0x878E},{});
        machine.cpu().hl.uint16_value=0xffff;
        h.run_fallback(machine,{12,0x878E},RoutineFallback::guard);
    }
    // Short continuations preserve the saved caller state and original
    // conditional-call boundaries between sound subsystems.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x83a6u,0x83d7u,0x83e4u,0x83f2u,0x8486u,0x852cu}) {
            probe::Setup setup;
            setup.seed=value;
            auto machine=h.prepare({12,static_cast<std::uint16_t>(at)},setup);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            machine.cpu().hl.uint16_value=at==0x83e4 ? 0xc18e : 0xc1ae;
            h.run_cpp(machine,{12,static_cast<std::uint16_t>(at)});
        }
    }
    // Host-observed sound entries run the callback once, before admission,
    // whether an NMI is pending or not. Milestone 14: the replacement is exact,
    // so the NMI is taken inside it and the hook also sees the handler's own
    // native boundaries at depth 1; only top-level calls are the entry's.
    for(unsigned at:{0x02edu,0x02f5u,0x8043u,0x82efu}) {
        const RoutineEntry entry{at<0x4000 ? 0u : 12u,static_cast<std::uint16_t>(at)};
        for(unsigned fallback=0;fallback<2;++fallback) {
            probe::Setup setup;
            auto machine=h.prepare(entry,setup);
            if(fallback) machine.schedule_nmi(machine.cycles()+1);
            unsigned calls=0;
            const auto before=h.routine(entry).counters;
            const auto old_cycles=machine.cycles();
            machine.step([&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
                if(machine.depth()) return {};
                probe::check(pc==entry.address && mapping==Memory::Mapping{Memory::Kind::rom,entry.bank},"observed sound entry differs");
                probe::check(machine.cycles()==old_cycles,"sound hook occurred after execution");
                ++calls;
                return {};
            },&h.registry());
            const auto& after=h.routine(entry).counters;
            probe::check(calls==1,"sound hook did not occur exactly once");
            probe::check(after.cpp_calls==before.cpp_calls+1,
                         "sound entry did not follow the expected admission path");
        }
    }
    // Vary every mapper input and request byte against the original path.
    for(unsigned value=0;value<256;++value) {
        for(unsigned at:{0x02e5u,0x02edu,0x02f5u,0x8043u,0x82efu}) {
            const RoutineEntry entry{at<0x4000 ? 0u : 12u,static_cast<std::uint16_t>(at)};
            probe::Setup setup;
            setup.seed=value;
            setup.banks[2]=static_cast<std::uint8_t>(value%32);
            auto machine=h.prepare(entry,setup);
            machine.memory().write(0xc004,static_cast<std::uint8_t>(value));
            h.run_cpp(machine,entry);
        }
    }
    for (unsigned flags=0;flags<256;++flags) for (unsigned mode:{0u,0x80u}) for (unsigned next:{0u,0xe0u}) {
        auto machine=h.prepare({12,0x80A4},{});
        machine.memory().write(0xc12e,static_cast<std::uint8_t>(flags));
        machine.memory().write(0xc12f,static_cast<std::uint8_t>(next));
        machine.memory().write(0xc10e,static_cast<std::uint8_t>(mode));
        h.run_cpp(machine,{12,0x80A4});
    }
    for (auto entry:{RoutineEntry{12,0x8000},RoutineEntry{12,0x801F}}) {
        probe::Setup setup;
        setup.seed=987; auto rewritten=h.prepare(entry,setup), original=rewritten;
        unsigned steps=0; while(rewritten.cpu().pc.uint16_value!=setup.return_address && steps++<2000) rewritten.step(h.registry());
        probe::check(steps<2000,"rewritten sound chain did not return");
        steps=0; while(original.cpu().pc.uint16_value!=setup.return_address && steps++<2000) original.step();
        probe::check(steps<2000,"original sound chain did not return");
        for(const auto& d:oracle::differences(rewritten,original,oracle::Scope::whole)) std::cerr<<d<<" ";
        probe::check(oracle::equal(rewritten,original,oracle::Scope::whole) &&
                     rewritten.memory().cartridge_ram_dirty()==original.memory().cartridge_ram_dirty(),"sound chain diverged from original");
    }
    // The duration setup is interruptible at its original branches. Check
    // the complete multiplication and clear, including byte wrap and B=0.
    for (unsigned multiplier=0;multiplier<256;++multiplier) {
        auto actual=h.prepare({12,0x84BE},{});
        actual.cpu().ix_iy[0].uint16_value=0xc120;
        actual.cpu().de.uint16_value=0x9000;
        actual.cpu().af.uint8_values.at_1=0xa7;
        actual.memory().write(0xc122,static_cast<std::uint8_t>(multiplier));
        Machine native=actual;
        for(unsigned step=0;native.cpu().pc.uint16_value!=0x0180 && step<2000;++step) native.step();
        for(unsigned step=0;actual.cpu().pc.uint16_value!=0x0180 && step<2000;++step) actual.step(h.registry());
        probe::check(actual.cpu().pc.uint16_value==0x0180 && oracle::equal(actual,native,oracle::Scope::whole) && actual.memory().cartridge_ram_dirty()==native.memory().cartridge_ram_dirty(),"full note duration chain differs");
        const auto expected=static_cast<std::uint8_t>(0xa7*multiplier);
        probe::check(actual.peek(0xc12a)==expected && actual.peek(0xc135)==expected,"duration multiplier or wrap differs");
    }
    // Compare write observer ordering and cycles while the split score
    // crosses different frame boundaries, not only its final memory.
    for(unsigned remaining:{1u,10u,40u,100u,200u,1000u}) {
        auto actual=h.prepare({12,0x84BE},{});
        actual.cpu().ix_iy[0].uint16_value=0xc120;
        actual.cpu().de.uint16_value=0x9000;
        actual.cpu().af.uint8_values.at_1=0xa7;
        actual.memory().write(0xc122,4);
        actual.advance(Machine::frame_cycles-actual.cycles()%Machine::frame_cycles-remaining);
        Machine native=actual;
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        struct Stop {};
        auto run=[](Machine& machine,RoutineRegistry* registry,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](auto address,auto value) {writes.emplace_back(address,value,machine.cycles());};
            Machine::Hook hook=[](auto pc,auto)->std::optional<std::uint16_t> {if(pc==0x0180) throw Stop{};return {};};
            try {for(unsigned frame=0;frame<4;++frame) machine.run_frame({},registry,hook,observer);}
            catch(const Stop&) {return;}
            // The game's registry has no segment at the probe's return address:
            // the engine stops there before running anything, as the hook does.
            catch(const UnhandledState&) {if(machine.cpu().pc.uint16_value==0x0180) return; throw;}
            throw std::runtime_error("duration chain did not return");
        };
        run(native,nullptr,expected);
        run(actual,&h.registry(),observed);
        probe::check(oracle::equal(actual,native,oracle::Scope::whole) && actual.memory().cartridge_ram_dirty()==native.memory().cartridge_ram_dirty() && expected==observed,"duration split changes write callback order or cycles");
        probe::check(expected.size()==9,"duration chain must write both duration and seven voice fields");
    }
    // A frame or NMI at an entry still uses the unmodified native opcode.
    for(unsigned at:{0x846du,0x8473u,0x8474u,0x8475u,0x84aau,0x84b6u,0x84beu,0x84bfu,0x84c5u,0x84c6u,0x84c9u,0x84d0u}) {
        const RoutineEntry entry{12,static_cast<std::uint16_t>(at)};
        for(unsigned event=0;event<3;++event) {
            auto machine=h.prepare(entry,{});
            machine.cpu().ix_iy[0].uint16_value=0xc120;
            machine.cpu().de.uint16_value=0x9000;
            machine.memory().write(0xc122,4);
            if(event==0) machine.schedule_nmi(machine.cycles()+1);
            else if(event==1) machine.advance(Machine::frame_cycles-machine.cycles()%Machine::frame_cycles-1);
            else {
                machine.write_port(0xbf,0x20,machine.cycles());
                machine.write_port(0xbf,0x81,machine.cycles());
                const auto irq=machine.vdp().next_irq();
                probe::check(irq && *irq>machine.cycles(),"expected a future video interrupt");
                machine.advance(*irq-machine.cycles()-1);
                machine.cpu().iff1=machine.cpu().iff2=1;
            }
            h.run_fallback(machine,entry,event==0 ? RoutineFallback::nmi : event==1 ? RoutineFallback::frame : RoutineFallback::irq);
        }
    }
    sound_exit_probes_and_sweeps(h,*rom);
    std::cout<<"Sound initialization and silence segments verified against ROM.\n";
    return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
