// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include "c4_sweep.h"
#include <algorithm>
#include <iostream>
using namespace psmodern;
int main(int argc,char** argv) {
    // Registry-less reference runs only: every run with the game's registry
    // stops at unrendered code (review R1 H-1).
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        if(argc!=3) return 77;
        auto rom=probe::load(argv[1],argv[2]); if(!rom) return 77;
        probe::Harness harness(*rom);
        {
            probe::Harness sweeping(*rom);
            std::vector<RoutineEntry> all(game::battle_setup_entries.begin(),game::battle_setup_entries.end());
            all.push_back(game::money_add_unit);
            c4::sweep_file(sweeping,"money and battle setup",all);
        }
        for(auto entry:game::battle_setup_entries) {
            for(unsigned seed=0;seed<20;++seed) {
                probe::Setup setup{}; setup.seed=seed;
                auto machine=harness.prepare(entry,setup);
                machine.cpu().hl.uint16_value=0xc500;
                machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(seed%5);
                machine.memory().write(0xc2e6,static_cast<std::uint8_t>(seed%2));
                machine.memory().write(0xc500,static_cast<std::uint8_t>(seed%2 ? 0x83 : 0x03));
                machine.memory().write(0xc501,static_cast<std::uint8_t>(seed%2 ? 0x83 : 0x03));
                machine.memory().write(setup.sp,0x00); machine.memory().write(setup.sp+1,0xc5);
                harness.run_cpp(machine,entry);
            }
        }
        // The empty marker and high bit are part of the original enemy-id
        // handling. Sweep the complete byte domain rather than relying on a
        // few enemy records reached by the input recordings.
        for(unsigned id=0;id<256;++id) {
            probe::Setup setup{}; setup.seed=id;
            auto machine=harness.prepare(game::battle_setup_unit,setup);
            machine.memory().write(0xc2e6,static_cast<std::uint8_t>(id));
            harness.run_cpp(machine,game::battle_setup_unit);
        }
        for(auto entry:game::battle_setup_entries) for(unsigned refresh:{0u,1u,0x7eu,0x7fu,0x80u,0xfeu,0xffu}) {
            probe::Setup setup{}; setup.seed=refresh; setup.r=static_cast<std::uint8_t>(refresh);
            auto machine=harness.prepare(entry,setup);
            machine.cpu().hl.uint16_value=0xc500;
            machine.cpu().bc.uint8_values.at_1=2;
            machine.memory().write(0xc2e6,1); machine.memory().write(0xc501,0x83);
            machine.memory().write(setup.sp,0); machine.memory().write(setup.sp+1,0xc5);
            harness.run_cpp(machine,entry);
        }
        for(unsigned count=0;count<16;++count) {
            auto machine=harness.prepare({1,0x605e},{});
            machine.cpu().hl.uint16_value=0xc500;
            machine.memory().write(0xc501,static_cast<std::uint8_t>(0x80|count));
            harness.run_cpp(machine,{1,0x605e});
        }
        for(unsigned bound=0;bound<=8;++bound) for(unsigned draw=0;draw<16;++draw) {
            auto machine=harness.prepare({1,0x6078},{});
            machine.cpu().hl.uint16_value=0xc500;
            machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(bound);
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(draw);
            harness.run_cpp(machine,{1,0x6078});
        }
        for(unsigned count:{0u,1u,2u,8u,15u}) {
            auto machine=harness.prepare({1,0x608d},{});
            machine.cpu().hl.uint16_value=0x8170; machine.cpu().de.uint16_value=0x8123;
            machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(count);
            machine.memory().write(0xdf00,0x00); machine.memory().write(0xdf01,0xc5);
            harness.run_cpp(machine,{1,0x608d});
        }
        for(unsigned sp:{0xc440u,0xc441u,0xc448u,0xc44fu}) {
            probe::Setup setup{}; setup.sp=static_cast<std::uint16_t>(sp);
            auto machine=harness.prepare({1,0x608d},setup);
            machine.cpu().hl.uint16_value=0x8170; machine.cpu().de.uint16_value=0x8123;
            machine.cpu().bc.uint8_values.at_1=2;
            harness.run_cpp(machine,{1,0x608d});
        }
        // Execute the whole setup through all native bridges and helper calls.
        // The return address is outside the unit and can be recognized without
        // assuming the number of frames taken by the graphics and wait code.
        auto complete=[&](Machine& machine,RoutineRegistry* registry) {
                for(unsigned steps=0;steps<3000000;++steps) {
                    machine.step({},registry);
                    if((steps&1023)==0) { machine.take_samples(); machine.take_psg_writes(); }
                    if(machine.cpu().pc.uint16_value==0x0180 && machine.cpu().sp.uint16_value==0xdf02) {
                        machine.take_samples(); machine.take_psg_writes(); return;
                    }
                }
                throw std::runtime_error("battle setup did not return");
        };
        for(unsigned enemy:{0u,1u,2u,0x80u,0x81u}) {
            auto original=harness.prepare(game::battle_setup_unit,{});
            original.memory().write(0xc2e6,static_cast<std::uint8_t>(enemy));
            auto rewritten=original;
            complete(original,nullptr); complete(rewritten,&harness.registry());
            // The documented equality (milestone 14): the pinned core's
            // per-step scratch is not machine state, and the rewritten run
            // executes fewer core steps.
            std::string differences;
            for(const auto& difference:oracle::differences(rewritten,original,oracle::Scope::whole)) differences+=" "+difference;
            probe::check(differences.empty(),"whole setup differs from original:"+differences);
        }
        for(unsigned rate:{200u,400u}) for(unsigned enemy:{1u,2u,0x81u}) {
            auto original=harness.prepare(game::battle_setup_unit,{});
            original.memory().write(0xc2e6,static_cast<std::uint8_t>(enemy));
            auto enhanced=original;
            // The whole game with the option (review R1 H-1: no part of the run on test scaffolding).
            RoutineRegistry registry; probe::check(game::register_routines(registry,rom->sha1,game::Options{100,rate}),"registry");
            complete(original,nullptr); complete(enhanced,&registry);
            const unsigned base=original.memory().read(0xc2dd)|(original.memory().read(0xc2de)<<8);
            const auto scaled=std::min(65535u,base*rate/100);
            original.memory().write(0xc2dd,static_cast<std::uint8_t>(scaled));
            original.memory().write(0xc2de,static_cast<std::uint8_t>(scaled>>8));
            std::string differences;
            for(const auto& difference:oracle::differences(enhanced,original,oracle::Scope::whole)) differences+=" "+difference;
            probe::check(differences.empty(),"enhanced whole setup differs beyond money:"+differences);
            probe::check(registry.census().scaffold_steps==0,"the enhanced setup ran test scaffolding");
            probe::check(registry.find(game::battle_money_store)->counters.fallback_calls==0 &&
                         registry.find(game::battle_money_store)->counters.cpp_calls==1,
                         "whole setup renders its scaled money store once");
        }
        for(unsigned total:{0u,1u,32767u,65534u,65535u})
        for(unsigned award:{0u,1u,32767u,65534u,65535u})
        for(unsigned seed=0;seed<16;++seed) {
            probe::Setup setup{}; setup.seed=seed;
            auto machine=harness.prepare(game::money_add_unit,setup);
            machine.memory().write(0xc4e0,static_cast<std::uint8_t>(total));
            machine.memory().write(0xc4e1,static_cast<std::uint8_t>(total>>8));
            machine.cpu().hl.uint16_value=static_cast<std::uint16_t>(award);
            harness.run_cpp(machine,game::money_add_unit);
            const unsigned actual=machine.memory().read(0xc4e0)|(machine.memory().read(0xc4e1)<<8);
            probe::check(actual==std::min(65535u,total+award),"money must saturate at 65535");
        }
        for(unsigned rate:{100u,200u,400u}) {
            RoutineRegistry registry; game::register_battle_money_store(registry,game::Options{100,rate});
            Machine machine(rom->image);
            machine.cpu().sp.uint16_value=0xc100;
            for(unsigned reward=0;reward<65536;++reward) {
                machine.cpu().pc.uint16_value=game::battle_money_store.address;
                machine.cpu().hl.uint16_value=static_cast<std::uint16_t>(reward);
                machine.step(registry);
                if ((reward&255)==0) { machine.take_samples(); machine.take_psg_writes(); }
                const unsigned actual=machine.memory().read(0xc2dd)|(machine.memory().read(0xc2de)<<8);
                probe::check(actual==std::min(65535u,reward*rate/100),"battle store scaling");
                probe::check(machine.cpu().hl.uint16_value==reward,"battle store preserves original HL");
            }
        }
        std::cout<<"Battle setup and money probes passed.\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
