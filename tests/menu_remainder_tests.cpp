// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/menu_remainder.h"
#include "probe.h"
#include "game/scenes.h"
#include <tuple>
#include <vector>
#include <iostream>
using namespace psmodern;
namespace {
struct Stopped {};
void protected_loads(probe::Harness& h,const probe::Rom& rom) {
    std::vector<RoutineEntry> entries(game::menu_remainder::ei_delay_loads.begin(),game::menu_remainder::ei_delay_loads.end());
    entries.insert(entries.end(),game::scenes::ei_delay_loads.begin(),game::scenes::ei_delay_loads.end());
    for(const auto entry:entries) {
        probe::check((rom.byte(entry.bank,entry.address)==0x3E || rom.byte(entry.bank,entry.address)==0x21 || rom.byte(entry.bank,entry.address)==0x11 || rom.byte(entry.bank,entry.address)==0x3A) && rom.byte(entry.bank,static_cast<std::uint16_t>(entry.address-1))==0xFB,
            "EI scaffolding is exactly the following register load");
        probe::check(h.registry().find(entry)==nullptr,"EI load stays on the original core");
        for(unsigned remaining:{1u,7u,8u}) {
            probe::Setup setup; setup.iff1=true; setup.iff2=true;
            auto actual=h.prepare(entry,setup);
            actual.cpu().data.uint8_array[0]=0xFB;
            const auto phase=actual.cycles()%Machine::frame_cycles;
            actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
            Machine native=actual;
            Machine::PortTrace ports(actual);
            unsigned hooks=0;
            Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                probe::check(pc==entry.address && map==Memory::Mapping{Memory::Kind::rom,entry.bank},"EI load callback mapping and PC");
                ++hooks; return {};
            };
            const auto start=actual.cycles();
            native.step();
            actual.step(h.registry(),hook);
            probe::check(actual.equivalent(native) && actual.cycles()-start==(rom.byte(entry.bank,entry.address)==0x3E ? 7u : rom.byte(entry.bank,entry.address)==0x3A ? 13u : 10u) && hooks==1 && ports.accesses().empty(),
                "EI protected load preserves state, original load cycles, native callback and no I/O");
        }
    }
    for(const auto entry:game::menu_remainder::ei_delay_epilogues) {
        const auto opcode=rom.byte(entry.bank,entry.address);
        probe::check((opcode==0xC9 || opcode==0xE1 || opcode==0xF1) &&
            rom.byte(entry.bank,static_cast<std::uint16_t>(entry.address-1))==0xFB,
            "native EI epilogue is the original return or saved-register load");
        probe::check(h.registry().find(entry)==nullptr,"EI epilogue stays on the original core");
        auto actual=h.prepare(entry,{});
        actual.cpu().iff1=actual.cpu().iff2=1;
        actual.cpu().data.uint8_array[0]=0xFB;
        Machine native=actual;
        const auto start=actual.cycles();
        native.step(); actual.step(h.registry());
        probe::check(actual.equivalent(native) && actual.cycles()-start==10,"EI return/POP preserves the exact native state and delay step");
    }

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
            if(routine.spec.exits.empty()) chain.add(at,routine.bound,routine.body);
            else chain.add(at,routine.spec,routine.bound,routine.body);
        }
        // The full-chain trace owns the port log. A nested per-call trace
        // would divert those same events into its private comparison buffer.
        chain.verifier=nullptr;
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t,Memory::Kind>;
        std::vector<Write> expected,observed;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        auto run=[](Machine& m,RoutineRegistry* registry,std::vector<Write>& writes) {
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
                    m.take_psg_writes();
                    m.take_samples();
                }
            }
            catch(const Stopped&) { return; }
            throw std::runtime_error("save-name drawing did not restore cartridge mapping");
        };
        run(native,nullptr,expected);
        run(actual,&chain,observed);
        probe::check(actual.equivalent(native),"save-name complete draw, interrupts and mapping");
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
            if(routine.spec.exits.empty()) chain.add(at,routine.bound,routine.body);
            else chain.add(at,routine.spec,routine.bound,routine.body);
        }
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        auto run=[](Machine& m,RoutineRegistry* r,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) { writes.emplace_back(address,value,m.cycles()); };
            Machine::Hook hook=[](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                if(pc==0x0180) throw Stopped{};
                return {};
            };
            try { for(unsigned frame=0;frame<2;++frame) m.run_frame({},r,hook,observer); }
            catch(const Stopped&) { return; }
            throw std::runtime_error("draw-at-once window did not return");
        };
        run(native,nullptr,expected);
        run(actual,&chain,observed);
        probe::check(actual.equivalent(native),"window bank option preserves full original state");
        probe::check(expected==observed && expected_ports.accesses()==actual_ports.accesses(),"window bank store/restore callbacks and all video ports match");
        probe::check(actual.memory().mapping(0x8000)==native.memory().mapping(0x8000),"window restores bank and cartridge mapping");
    }
}

}
int main(int argc,char** argv) {
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
                harness.run_cpp(machine,entry);
            }
        }
        protected_loads(harness,*rom);
        slot_name_mapping(harness);
        draw_window_bank_observers(harness);
        std::cout<<"Menu interrupt/SRAM boundaries verified on the identified ROM\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
