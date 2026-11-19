// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/menu_remainder.h"
#include "probe.h"
#include "oracle/native_machine.h"
#include <tuple>
#include <vector>
#include <iostream>
using namespace psmodern;
namespace {
struct Stopped {};
// The instruction right after each EI of the printers (and the ones the printers' epilogues run after it):
// an interrupt that falls due at the EI is taken only after that instruction, as on the original. Run in C++
// under strict lockstep with the frame interrupt due a few cycles after the entry.
const std::vector<RoutineEntry> after_enable{
    {0,0x3030},{0,0x30A1},{0,0x332B},{0,0x3411},{0,0x35B6},{0,0x3633},{0,0x3A7A},
    {0,0x319D},{0,0x33C7},{0,0x3AA1},{0,0x3AC3}};
void after_enable_instructions(probe::Harness& h) {
    for(const auto entry:after_enable) {
        for(unsigned remaining:{1u,3u,7u,8u,17u,24u}) {
            probe::Setup setup; setup.iff1=true; setup.iff2=true;
            auto machine=h.prepare(entry,setup);
            machine.cpu().data.uint8_array[0]=0xFB;
            const auto phase=machine.cycles()%Machine::frame_cycles;
            machine.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
            h.run_cpp(machine,entry);
        }
    }
}
// Interrupt sweeps (milestone 14): an IRQ and an NMI at every cycle of every segment of more than one instruction.
void sweeps(probe::Harness& h) {
    auto follow_enable=[](Machine& m) { m.cpu().data.uint8_array[0]=0xFB; };
    for(const std::uint16_t at:{0x3030,0x30A1,0x332B,0x3411,0x35B6,0x3633,0x3A7A})
        probe::sweep(h,{0,at},{},follow_enable);       // load of the wait index, then the call of the frame wait
    for(const std::uint16_t at:{0x319A,0x340F,0x35B3,0x3AC0})
        probe::sweep(h,{0,at},{});                      // the saved registers come back, then EI
    probe::sweep(h,{0,0x3A68},{});                      // the row's BC is saved, then DI
    for(const std::uint16_t at:{0x39B1,0x39C2}) {
        probe::Setup setup; setup.cartridge_ram=true;
        probe::sweep(h,{0,at},setup);                   // the control value, then the cartridge RAM switch
    }
    // The stretches of the other menu files.
    probe::sweep(h,{0,0x3161},{},{},2);                 // the delay writes before the enemy-name loop
    for(const std::uint16_t at:{0x3A6B,0x3A6E,0x3A73,0x3A8F,0x3AAD}) {
        probe::Setup setup;
        probe::sweep(h,{0,at},setup,[at](Machine& m) {
            m.cpu().bc.uint16_value=at==0x3A6E ? 0x04BE : 0x0404; m.cpu().hl.uint16_value=0xC100;
        });
    }
    for(const std::uint16_t at:{0x2F1B,0x2FA4,0x2FAF,0x2FBD}) probe::sweep(h,{0,at},{});
    // 00:2FA1 is one instruction (the load the battle animation joins at): no boundary inside it to sweep; the
    // probes of every catalog entry (main) run it under lockstep from 64 shaken states.
}
// A registry with the entries of the game's, keeping what the rendered bodies need (exactness, entry guard).
void copy_entry(RoutineRegistry& chain,RoutineEntry at,const RegisteredRoutine& routine) {
    if(routine.exact && routine.guard) chain.add_exact(at,routine.spec,*routine.guard,routine.bound,routine.body);
    else if(routine.exact) chain.add_exact(at,routine.spec,routine.bound,routine.body);
    else chain.add(at,routine.spec,routine.bound,routine.body);
}
void slot_name_mapping(probe::Harness& h) {
    for(unsigned initial:{0u,12u}) {
        probe::Setup setup; setup.cartridge_ram=true;
        auto actual=h.prepare({0,0x39B1},setup);
        for(unsigned i=0;i<216;++i) actual.memory().write(static_cast<std::uint16_t>(0x8100+i),static_cast<std::uint8_t>(i*11));
        actual.memory().write(0xFFFC,static_cast<std::uint8_t>(initial));
        actual.write_port(0xBF,0x20,actual.cycles());
        actual.write_port(0xBF,0x81,actual.cycles());
        Machine native=actual;
        RoutineRegistry chain;
        for(const auto& [at,routine]:h.registry().entries()) if(at!=RoutineEntry{0,0x39C7}) {
            copy_entry(chain,at,routine);
        }
        // The full-chain trace owns the port log. A nested per-call trace
        // would divert those same events into its private comparison buffer.
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t,Memory::Kind>;
        std::vector<Write> expected,observed;
        std::vector<PsgWrite> expected_psg,observed_psg;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        auto run=[](Machine& m,RoutineRegistry* registry,std::vector<Write>& writes,std::vector<PsgWrite>& psg) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                writes.emplace_back(address,value,m.cycles(),m.memory().mapping(0x8000).kind);
            };
            Machine::Hook hook=[](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if(map==Memory::Mapping{Memory::Kind::rom,0} && pc==0x39C7) throw Stopped{};
                return {};
            };
            try {
                for(unsigned frame=0;frame<32;++frame) {
                    m.run_frame({},registry,hook,observer);
                    for(const auto& w:m.take_psg_writes()) psg.push_back(w);
                    m.take_samples();
                }
            }
            catch(const Stopped&) { for(const auto& w:m.take_psg_writes()) psg.push_back(w); return; }
            // The chain has no segment at 00:39C7: the engine stops there before
            // running anything, as the hook does on the original.
            catch(const UnhandledState&) {
                if(!registry || m.cpu().pc.uint16_value!=0x39C7 || m.memory().mapping(0x39C7)!=Memory::Mapping{Memory::Kind::rom,0}) throw;
                for(const auto& w:m.take_psg_writes()) psg.push_back(w); return;
            }
            throw std::runtime_error("save-name drawing did not restore cartridge mapping");
        };
        run(native,nullptr,expected,expected_psg);
        run(actual,&chain,observed,observed_psg);
        // Whole-state equality (milestone 14: not Console::equivalent, which includes the core's per-step
        // bookkeeping), with the PSG writes and the cartridge RAM's dirty flag compared explicitly.
        probe::check(oracle::equal(actual,native,oracle::Scope::whole),"save-name complete draw, interrupts and mapping");
        probe::check(expected_psg==observed_psg,"save-name PSG writes");
        probe::check(actual.memory().cartridge_ram_dirty()==native.memory().cartridge_ram_dirty(),"save-name cartridge RAM dirty flag");
        if(expected!=observed) {
            const auto where=std::mismatch(expected.begin(),expected.end(),observed.begin(),observed.end());
            if(where.first!=expected.end() && where.second!=observed.end())
                throw std::runtime_error("save-name callback address="+std::to_string(std::get<0>(*where.first))+" native cycle="+std::to_string(std::get<2>(*where.first))+" cpp cycle="+std::to_string(std::get<2>(*where.second)));
            throw std::runtime_error("save-name memory callback count");
        }
        probe::check(expected_ports.accesses()==actual_ports.accesses(),"save-name ordered video/PSG port cycles");
        probe::check(actual.memory().mapping(0x8000).kind==Memory::Kind::rom,"save-name query restores ROM mapping");
    }
}
void draw_window_bank_observers(probe::Harness& h) {
    for(unsigned bank:{0u,3u,17u,31u})
    for(bool cartridge:{false,true}) {
        probe::Setup setup; setup.banks[2]=static_cast<std::uint8_t>(bank); setup.cartridge_ram=cartridge;
        auto actual=h.prepare({0,0x3A83},setup);
        actual.cpu().hl.uint16_value=0xC100;
        actual.cpu().de.uint16_value=0x7800;
        actual.cpu().bc.uint16_value=0x0204;
        for(unsigned i=0;i<8;++i) actual.memory().write(static_cast<std::uint16_t>(0xC100+i),static_cast<std::uint8_t>(i*31));
        Machine native=actual;
        RoutineRegistry chain;
        for(const auto& [at,routine]:h.registry().entries()) {
            copy_entry(chain,at,routine);
        }
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        std::vector<PsgWrite> expected_psg,observed_psg;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        auto run=[](Machine& m,RoutineRegistry* r,std::vector<Write>& writes,std::vector<PsgWrite>& psg) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) { writes.emplace_back(address,value,m.cycles()); };
            Machine::Hook hook=[](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                if(pc==0x0180) throw Stopped{};
                return {};
            };
            try { for(unsigned frame=0;frame<2;++frame) { m.run_frame({},r,hook,observer); for(const auto& w:m.take_psg_writes()) psg.push_back(w); } }
            catch(const Stopped&) { for(const auto& w:m.take_psg_writes()) psg.push_back(w); return; }
            // No segment begins at the probe's return address 00:0180: the
            // engine stops there before running anything, as the hook does.
            catch(const UnhandledState&) {
                if(!r || m.cpu().pc.uint16_value!=0x0180 || m.memory().mapping(0x0180)!=Memory::Mapping{Memory::Kind::rom,0}) throw;
                for(const auto& w:m.take_psg_writes()) psg.push_back(w); return;
            }
            throw std::runtime_error("draw-at-once window did not return");
        };
        run(native,nullptr,expected,expected_psg);
        run(actual,&chain,observed,observed_psg);
        probe::check(oracle::equal(actual,native,oracle::Scope::whole),"window bank option preserves full original state");
        probe::check(expected_psg==observed_psg && actual.memory().cartridge_ram_dirty()==native.memory().cartridge_ram_dirty(),
                     "window bank option: PSG writes and cartridge RAM dirty flag");
        probe::check(expected==observed && expected_ports.accesses()==actual_ports.accesses(),"window bank store/restore callbacks and all video ports match");
        probe::check(actual.memory().mapping(0x8000)==native.memory().mapping(0x8000),"window restores bank and cartridge mapping");
    }
}

}
int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        probe::check(argc==3,"Expected ROM and region");
        const auto rom=probe::load(argv[1],argv[2]);
        if(!rom) return 77;
        probe::Harness harness(*rom);
        RoutineRegistry part;
        game::register_menu_remainder(part);
        probe::check(part.entries().size()==game::menu_remainder::segments.size(),"menu remainder catalog size");
        for(const auto& [entry,unit]:game::menu_remainder::segments) {
            probe::check(part.find(entry) && part.find(entry)->spec.unit==unit,"menu remainder catalog entry and unit");
            for(unsigned seed=0;seed<64;++seed) {
                probe::Setup setup;
                setup.seed=seed;
                setup.r=static_cast<std::uint8_t>(seed*7);
                auto machine=harness.prepare(entry,setup);
                // The row's OUTI loop writes through port C: the video data port (a random port could be the memory control).
                if(entry==RoutineEntry{0,0x3A6E}) { machine.cpu().bc.uint8_values.at_0=0xBE; machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(1+seed%4); }
                harness.run_cpp(machine,entry);
            }
        }
        after_enable_instructions(harness);
        sweeps(harness);
        slot_name_mapping(harness);
        draw_window_bank_observers(harness);
        std::cout<<"Menu interrupt/SRAM boundaries verified on the identified ROM\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
