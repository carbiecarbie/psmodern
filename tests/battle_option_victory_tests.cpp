// SPDX-License-Identifier: GPL-3.0-or-later
// Whole battle setup and victory with original execution plus option hooks
// compared against the full translated registry with the same options.
#include "battle_effect_probe.h"
#include "game/money.h"
#include "game/experience.h"
#include "menu_probe.h"
#include <algorithm>
#include <optional>
#include <iostream>
using namespace psmodern;
namespace {
std::uint16_t word(const Machine& m,std::uint16_t a) {return m.peek(a)|(m.peek(a+1)<<8);}
void put_word(Machine& m,std::uint16_t a,unsigned value) {m.memory().write(a,value);m.memory().write(a+1,value>>8);}
// The reference (milestone 14): the original on the oracle's native machine,
// except that at an entry of an option hook (the three segments of `only`) the
// engine runs that one segment on the reference's state; the program runs with
// its full registry. At 100% the game is the original, so every program step
// also runs under lockstep with an oracle (this replaced the per-call verifier).
void step_reference(oracle::NativeMachine& reference,Machine& engine,RoutineRegistry& only) {
    const auto pc=reference.cpu().pc.uint16_value;const auto mapping=reference.memory().mapping(pc);
    if (!reference.cpu().resume && mapping.kind==Memory::Kind::rom && only.find({mapping.bank,pc})) {
        reference.settle();
        if (reference.pending_acceptance()==Console::Acceptance::none) {
            static_cast<Console&>(engine)=static_cast<const Console&>(reference);
            engine.step(only);
            static_cast<Console&>(reference)=static_cast<const Console&>(engine);
            return;
        }
    }
    reference.step();
}
void step_pair(Machine& fast,oracle::NativeMachine& native,RoutineRegistry& all,RoutineRegistry& only,const char* label,unsigned* base_money,bool verify) {
    std::optional<oracle::Lockstep> lockstep;
    if (verify) lockstep.emplace(fast,label);
    Machine engine=fast;
    for (unsigned steps=0;fast.cpu().pc.uint16_value!=0x0180;++steps) {
        probe::check(steps<30000000,std::string(label)+" did not return");
        auto pad=battle_effect_probe::press_both(fast);pad.one=false;
        fast.set_pad(0,pad);native.set_pad(0,pad);
        if (base_money && fast.cpu().pc.uint16_value==0x60bc && fast.memory().mapping(0x60bc).bank==1) *base_money=fast.cpu().hl.uint16_value;
        if (lockstep) { lockstep->oracle().set_pad(0,pad); lockstep->step(fast,&all); }
        else fast.step(all);
        while (native.cycles()<fast.cycles() || native.cpu().resume) step_reference(native,engine,only);
        probe::check(native.cycles()==fast.cycles(),std::string(label)+" cycle boundary");
        if ((steps&255)==0) {
            probe::check(native.take_psg_writes()==fast.take_psg_writes(),std::string(label)+" PSG order");
            native.take_samples();fast.take_samples();
            if (lockstep) { lockstep->oracle().take_psg_writes(); lockstep->oracle().take_samples(); }
        }
    }
    probe::check(native.take_psg_writes()==fast.take_psg_writes(),std::string(label)+" final PSG");
    native.take_samples();fast.take_samples();
    std::string diff;for(const auto& d:oracle::differences(fast,native,oracle::Scope::whole))diff+=" "+d;
    for(const auto& d:oracle::queue_differences(fast,native))diff+=" "+d;
    probe::check(diff.empty(),std::string(label)+" state:"+diff);
}
std::uint64_t calls(const RoutineRegistry& r,RoutineEntry e) {auto p=r.find(e);return p ? p->counters.cpp_calls+p->counters.fallback_calls : 0;}
void option_case(const probe::Rom& rom,const Machine& base,unsigned rate,unsigned cap_case) {
    game::Options opts;opts.experience_percent=rate;
    RoutineRegistry full;probe::check(game::register_routines(full,rom.sha1,opts),"registry");
    RoutineRegistry native;
    for (auto e:{game::battle_money_store,game::experience_award_unit,game::experience_add_entry}) {
        auto* p=full.find(e);probe::check(p!=nullptr,"option hook missing");
        native.add(e,p->spec,p->bound,p->body);
    }
    auto fast=map_probe::prepare_at(base,game::battle_setup_unit,{.return_address=0x0180,.iff1=true,.iff2=true});
    fast.memory().write(0xc2e6,1);
    oracle::NativeMachine original(fast);
    unsigned base_money=0;
    step_pair(fast,original,full,native,"battle setup",&base_money,rate==100);
    probe::check(calls(full,game::battle_money_store)==1 && calls(native,game::battle_money_store)==1,"money hook count");
    const unsigned money_reward=word(fast,0xc2dd),xp_reward=word(fast,0xc2d0);
    probe::check(money_reward && xp_reward && base_money,"fixture rewards are zero");
    probe::check(money_reward==std::min(65535u,base_money*rate/100),"money scaling after setup");
    const unsigned scaled_xp=std::min(65535u,xp_reward*rate/100);
    auto outcome=map_probe::prepare_at(fast,{0,0x1656},{.return_address=0x0180,.iff1=true,.iff2=true});
    outcome.memory().write(0xc2e6,1);outcome.memory().write(0xc29d,1);
    outcome.memory().write(0xc2df,0);outcome.memory().write(0xc4e2,0);
    for (unsigned k=0;k<4;++k) {outcome.memory().write(0xc400+16*k,k==0?1:0);outcome.memory().write(0xc405+16*k,cap_case==2 ? 1 : 30);}
    const unsigned starting_xp=cap_case ? 65535u-scaled_xp+1 : 0;
    const unsigned starting_money=cap_case ? 65535u-money_reward+1 : 0;
    put_word(outcome,0xc403,starting_xp);put_word(outcome,0xc4e0,starting_money);
    original=oracle::NativeMachine(outcome);fast=outcome;
    step_pair(fast,original,full,native,"victory",nullptr,rate==100);
    probe::check(calls(full,game::experience_award_unit)==1 && calls(native,game::experience_award_unit)==1,"experience display count");
    probe::check(calls(full,game::experience_add_entry)==1 && calls(native,game::experience_add_entry)==1,"experience add count");
    probe::check(word(fast,0xc403)==std::min(65535u,starting_xp+scaled_xp),"experience result");
    probe::check(word(fast,0xc4e0)==std::min(65535u,starting_money+money_reward),"money result");
    if (cap_case==2) probe::check(fast.peek(0xc405)==(rate==100 ? 1 : 2),"overflow level check follows option semantics");
    for (auto e:{game::battle_money_store,game::experience_award_unit,game::experience_add_entry}) {
        const auto& c=full.find(e)->counters;
        probe::check(!c.fallback_calls && c.cpp_calls==1,"option hooks are rendered, never refused, and run once");
    }
    std::cout<<"rate="<<rate<<" cap="<<cap_case<<" xp="<<xp_reward<<" money="<<money_reward<<" passed\n"<<std::flush;
}
}
int main(int argc,char** argv) {
    try {
    const auto inputs=map_probe::load(argc,argv);if(!inputs)return 77;
    const auto rec=replay::read(inputs->recording("first-battle"));
    const auto image=map_probe::start_image(*inputs,"first-battle",rec);if(!image)return 77;
    map_probe::Scenario scene(inputs->rom,*image,"victory-options");std::uint64_t from=0;
    probe::check(scene.play(rec,from,[](const Machine& m) {return m.peek(0xc29d)!=0 && m.peek(0xc268)!=0 && m.peek(0xc26e)==4;}),"battle scene");
    for (unsigned rate:{100u,200u,400u}) for(unsigned cap:{0u,1u,2u}) option_case(inputs->rom,scene.machine(),rate,cap);
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;} }
