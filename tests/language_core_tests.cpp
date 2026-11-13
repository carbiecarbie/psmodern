// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "game/routines.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace psmodern;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F> static void refuses(F run){try{run();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Expected invalid image refusal");}
int main(){try{
    static_assert(noexcept(std::declval<Machine&>().replace_rom(std::declval<const Machine::RomReplacement&>())));
    auto source=std::make_shared<Memory::Rom>(512*1024,0);
    auto target=std::make_shared<Memory::Rom>(*source);(*target)[0x123]=0x6d;
    Machine machine(source);
    machine.cpu().pc.uint16_value=0x120;machine.cpu().sp.uint16_value=0xc100;
    machine.cpu().hl.uint16_value=0x3456;
    machine.memory().write(0xc123,0x57);machine.memory().write(0xfffc,8);machine.memory().write(0x8000,0x9b);
    machine.set_pad(0,{.up=true,.one=true});machine.set_pause(true);machine.schedule_nmi(1000);
    machine.write_port(0x7f,0x83,machine.cycles());machine.advance(57);
    const Machine snapshot=machine;
    auto prepared=Machine::prepare_rom_replacement(target);
    const auto retained=std::move(prepared); // token stays valid through copy/move expressions
    machine.replace_rom(prepared);
    const auto difference=snapshot.differences(machine);
    check(difference.size()==1 && difference.front().field=="memory.rom","ROM installation changes no other full-machine field");
    check(snapshot.peek(0x123)==0 && machine.peek(0x123)==0x6d,"Existing copied machines retain their original ROM");
    machine.replace_rom(retained);
    machine.replace_rom(Machine::prepare_rom_replacement(source));
    check(machine.equivalent(snapshot),"Replacing back restores exact identity without touching device state");
    refuses([&]{Machine::prepare_rom_replacement({});});
    refuses([&]{Machine::prepare_rom_replacement(std::make_shared<Memory::Rom>(512*1024-1));});
    check(machine.equivalent(snapshot),"Preparation failures do not mutate a machine");
    for(unsigned encounter:{0u,25u,50u,100u,200u})for(unsigned experience:{100u,200u,400u}) {
        RoutineRegistry us,br;const game::Options options{encounter,experience};
        check(game::register_routines(us,"07fcf297be4f4c9d92cd3f119a7ac48467e06838",options) &&
              game::register_routines(br,"fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd",options),"Both identified ROMs share registration");
        app::guard_observed_instructions(us);app::guard_observed_instructions(br);
        check(us.entries().size()==br.entries().size(),"Equal language catalogue sizes");
        for(const auto& [entry,routine]:us.entries()) {
            const auto* other=br.find(entry);check(other!=nullptr,"Equal language entries");
            const auto& a=routine.spec;const auto& b=other->spec;
            check(a.unit==b.unit && a.slot2_mapper==b.slot2_mapper && a.scratch_stack==b.scratch_stack &&
                  a.single_instruction==b.single_instruction && a.force_admit==b.force_admit &&
                  a.absolute_wram_store==b.absolute_wram_store && a.inherited_stack==b.inherited_stack &&
                  a.exits.size()==b.exits.size() && std::equal(a.exits.begin(),a.exits.end(),b.exits.begin(),
                    [](const auto& x,const auto& y){return x.same_declaration(y);}),"Equal complete segment contracts in both languages");
            check(routine.body.target_type()==other->body.target_type() && routine.bound.target_type()==other->bound.target_type(),
                  "Shared callback implementations in both languages");
        }
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
