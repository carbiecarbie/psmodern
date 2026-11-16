// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include "game/interaction_messages.h"
#include "game/interaction_prompts.h"
#include "game/interaction_party.h"
#include "game/interaction_state.h"
#include "game/interaction_story.h"
#include "game/interaction_handoffs.h"
#include "game/interaction_replies.h"
#include "game/interaction_setup.h"
#include "game/interaction_progress.h"
#include "game/interaction_companion.h"
#include "game/interaction_letter.h"
#include "game/interaction_rescue.h"
#include "game/interaction_progression.h"
#include "game/interaction_sales.h"
#include "game/interaction_trade.h"
#include <iostream>
using namespace psmodern;
int main(int argc,char** argv) {
 try {
    if (argc!=3) return 77;
    auto rom=probe::load(argv[1],argv[2]); if (!rom) return 77;
    probe::Harness h(*rom);
    for (const auto entry:game::interaction_dispatch_entries) {
        for (unsigned value: {0u,1u,0x7fu,0xb6u}) {
            auto machine=h.prepare(entry,{});
            machine.memory().write(0xC2DB,static_cast<std::uint8_t>(value));
            machine.memory().write(0xC29E,static_cast<std::uint8_t>(value));
            const auto sp=machine.cpu().sp.uint16_value;
            h.run_cpp(machine,entry);
            const auto pc=machine.cpu().pc.uint16_value;
            if (entry.address==0x474B) probe::check(pc==(value ? 0x4752 : 0x1BE1),"event selection differs");
            if (entry.address==0x4752) probe::check(pc==0x4769 && machine.cpu().de.uint16_value==0x4771,"event table call differs");
            if (entry.address==0x4758) probe::check(pc==(value ? 0x3464 : 0x475F),"interaction close branch differs");
            if (entry.address==0x4765) probe::check(pc==0x2D25 && machine.cpu().sp.uint16_value==sp+2,"handler stack unwind differs");
        }
        auto interrupted=h.prepare(entry,{});
        interrupted.schedule_nmi(interrupted.cycles()+1);
        h.run_fallback(interrupted,entry,RoutineFallback::nmi);
    }
    for (const auto f:game::interaction_message_followups) for (unsigned seed=0;seed<32;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(f.entry,setup);
        h.run_cpp(machine,f.entry);
        probe::check(machine.cpu().pc.uint16_value==0x575a,"followup dialogue target differs");
    }
    for (const auto entry:game::interaction_message_entries) {
        for (unsigned seed=0;seed<32;++seed) {
            probe::Setup setup; setup.seed=seed; setup.banks[2]=static_cast<std::uint8_t>(seed%32);
            auto machine=h.prepare(entry,setup);
            h.run_cpp(machine,entry);
            probe::check(machine.cpu().pc.uint16_value==0x575a,"interaction did not reach dialogue dispatch");
        }
        auto interrupted=h.prepare(entry,{});
        interrupted.schedule_nmi(interrupted.cycles()+1);
        h.run_fallback(interrupted,entry,RoutineFallback::nmi);
    }
    for (unsigned position=0;position<256;++position) {
        probe::Setup setup; setup.seed=position;
        auto machine=h.prepare({1,0x4A7B},setup);
        machine.memory().write(0xc305,static_cast<std::uint8_t>(position));
        h.run_cpp(machine,{1,0x4A7B});
    }
    {
        auto machine=h.prepare(game::interaction_letter_copy,{});
        machine.cpu().hl.uint16_value=0x4d6c;
        machine.cpu().de.uint16_value=0xc240;
        machine.cpu().bc.uint16_value=8;
        h.run_cpp(machine,game::interaction_letter_copy);
        probe::check(machine.cpu().bc.uint16_value==0,"letter palette copy did not complete");
    }
    for (const auto item:game::interaction_trade_entries) for (unsigned seed=0;seed<256;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(item.entry,setup);
        machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(seed);
        machine.memory().write(0xc507,static_cast<std::uint8_t>(seed));
        h.run_cpp(machine,item.entry);
    }
    for (const auto item:game::interaction_sale_entries) for (unsigned flags=0;flags<256;++flags) {
        probe::Setup setup; setup.seed=flags;
        auto machine=h.prepare(item.entry,setup);
        machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(flags);
        h.run_cpp(machine,item.entry);
    }
    for (const auto entry:game::interaction_progression_entries) for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare(entry,setup);
        machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
        machine.memory().write(0xc4e0,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc4e1,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,entry);
    }
    for (const auto entry:game::interaction_rescue_entries) for (unsigned flags=0;flags<256;++flags) {
        probe::Setup setup; setup.seed=flags;
        auto machine=h.prepare(entry,setup);
        machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(flags);
        h.run_cpp(machine,entry);
    }
    for (const auto entry:game::interaction_letter_entries) for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare(entry,setup);
        machine.memory().write(0xc4f0,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,entry);
    }
    for (const auto entry:game::interaction_companion_entries) for (unsigned seed=0;seed<64;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(entry,setup); h.run_cpp(machine,entry);
    }
    for (const auto item:game::interaction_progress_segments) for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare(item.entry,setup);
        machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
        machine.memory().write(0xc410,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc4f0,static_cast<std::uint8_t>(value));
        machine.memory().write(0xc504,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,item.entry);
    }
    for (const auto item:game::interaction_dialogue_setup) for (unsigned seed=0;seed<64;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(item.entry,setup); h.run_cpp(machine,item.entry);
        probe::check(machine.cpu().pc.uint16_value==0x575a,"dialogue setup target differs");
    }
    for (const auto entry:game::interaction_enemy_setup) for (unsigned seed=0;seed<64;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(entry,setup); h.run_cpp(machine,entry);
        probe::check(machine.cpu().pc.uint16_value==0x5ffe,"enemy setup target differs");
    }
    // Price subtraction covers the threshold, both signed overflow edges and wraparound.
    for (unsigned money : {0u,1u,99u,100u,101u,399u,400u,401u,999u,1000u,1001u,1199u,1200u,1201u,0x7fffu,0x8000u,0x84afu,0xffffu}) {
        for (unsigned flags=0;flags<256;++flags) {
            probe::Setup setup; setup.seed=flags;
            for (auto entry : {RoutineEntry{1,0x4B1D},RoutineEntry{1,0x4E3C},RoutineEntry{1,0x4FB2},RoutineEntry{1,0x5087}}) {
            auto machine=h.prepare(entry,setup);
            machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(flags);
            machine.memory().write(0xc4e0,static_cast<std::uint8_t>(money));
            machine.memory().write(0xc4e1,static_cast<std::uint8_t>(money>>8));
            h.run_cpp(machine,entry);
            }
        }
    }
    for (const auto item:game::interaction_reply_segments) for (unsigned flags=0;flags<256;++flags) {
        probe::Setup setup; setup.seed=flags;
        auto machine=h.prepare(item.entry,setup);
        machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(flags);
        h.run_cpp(machine,item.entry);
    }
    for (const auto& f:game::interaction_dialogue_followups) for (unsigned seed=0;seed<32;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(f.entry,setup); h.run_cpp(machine,f.entry);
        probe::check(machine.cpu().pc.uint16_value==0x575a,"dialogue continuation target differs");
    }
    for (const auto& f:game::interaction_item_check_followups) for (unsigned seed=0;seed<32;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(f.entry,setup); h.run_cpp(machine,f.entry);
        probe::check(machine.cpu().pc.uint16_value==0x282e,"inventory continuation target differs");
    }
    auto test_handoffs=[&](const auto& entries,std::uint16_t destination) {
        for (auto entry:entries) for (unsigned seed=0;seed<64;++seed) {
            probe::Setup setup; setup.seed=seed;
            auto machine=h.prepare(entry,setup);
            h.run_cpp(machine,entry);
            probe::check(machine.cpu().pc.uint16_value==destination,"interaction handoff target differs");
        }
    };
    test_handoffs(game::interaction_dialogue_handoffs,0x575a);
    test_handoffs(game::interaction_item_check_entries,0x282e);
    for (unsigned direction=0;direction<256;++direction) {
        probe::Setup setup; setup.seed=direction;
        auto machine=h.prepare({1,0x4986},setup);
        machine.memory().write(0xc309,static_cast<std::uint8_t>(direction));
        h.run_cpp(machine,{1,0x4986});
    }
    for (const auto item:game::interaction_story_segments) for (unsigned seed=0;seed<32;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare(item.entry,setup);
        if (item.entry.address==0x48e6) machine.cpu().hl.uint16_value=0xc501;
        h.run_cpp(machine,item.entry);
    }
    auto test_state=[&](const auto& entries) {
        for (const auto item:entries) for (unsigned value=0;value<256;++value) {
            probe::Setup setup; setup.seed=value;
            auto machine=h.prepare(item.entry,setup);
            machine.memory().write(item.state,static_cast<std::uint8_t>(value));
            h.run_cpp(machine,item.entry);
        }
    };
    test_state(game::interaction_state_zero);
    test_state(game::interaction_state_compare);
    for (const auto entry:game::interaction_party_entries) for (unsigned count=0;count<256;++count) {
        probe::Setup setup; setup.seed=count;
        auto machine=h.prepare(entry,setup);
        machine.memory().write(0xc4f0,static_cast<std::uint8_t>(count));
        h.run_cpp(machine,entry);
        probe::check(machine.cpu().pc.uint16_value==0x575a,"party dialogue did not reach text dispatch");
    }
    for (const auto prompt:game::interaction_prompts) {
        for (unsigned flags=0;flags<256;++flags) {
            probe::Setup setup; setup.seed=flags;
            for (const auto entry:{prompt.entry,prompt.answer}) {
                auto machine=h.prepare(entry,setup);
                machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(flags);
                h.run_cpp(machine,entry);
                probe::check(machine.cpu().pc.uint16_value==0x575a,"prompt did not reach dialogue dispatch");
            }
        }
    }
    std::cout<<"Interaction dialogue entries verified.\n";
    return 0;
 } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
