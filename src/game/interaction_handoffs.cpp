// SPDX-License-Identifier: GPL-3.0-or-later
// Handoffs from story interactions to dialogue and inventory helpers.
#include "game/joins.h"
#include "game/interaction_handoffs.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit show_story_intro(Z& z,std::uint16_t at) {
    z.load_hl(at);
    return z.final_call(static_cast<std::uint16_t>(at+3));
}
// Joining stretches (milestone 14): the instruction that runs when a dialogue
// or inventory call returns into one of these interactions, before the next
// entry. Each is one instruction: the yes/no window (00:2D19), the window
// close (00:3464), a scene-fade helper (01:7B05), or the branch that picks
// the reply after the answer.
template<class Z> k::Exit after_call_open_prompt(Z& z,std::uint16_t at) { return z.final_call(at); }
template<class Z> k::Exit after_prompt_branch(Z& z,std::uint16_t at) { return z.final_relative_if(at,k::Condition::z); }
template<class Z> k::Exit find_story_item(Z& z,std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    return z.final_call(static_cast<std::uint16_t>(at+2));
}
}
void register_interaction_handoffs(RoutineRegistry& registry) {
    for (auto followup:interaction_dialogue_followups) {
        const auto call=static_cast<std::uint16_t>(followup.entry.address+3);
        k::add_segment(registry,followup.entry,k::segment(followup.unit,{k::core_final_at(call)},{.scratch_stack=2}),
                       [followup](auto& z) { return show_story_intro(z,followup.entry.address); });
    }
    for (auto followup:interaction_item_check_followups) {
        const auto call=static_cast<std::uint16_t>(followup.entry.address+2);
        k::add_segment(registry,followup.entry,k::segment(followup.unit,{k::core_final_at(call)},{.scratch_stack=2}),
                       [followup](auto& z) { return find_story_item(z,followup.entry.address); });
    }
    for (auto entry:interaction_dialogue_handoffs) {
        const auto call=static_cast<std::uint16_t>(entry.address+3);
        k::add_segment(registry,entry,k::segment(entry,{k::core_final_at(call)},{.scratch_stack=2}),
                       [entry](auto& z) { return show_story_intro(z,entry.address); });
    }
    // The joining stretches (catalogued with the reply segments).
    for (std::uint16_t at:{0x49C3,0x4A5D,0x4CD5,0x4CD8,0x4D01,0x4D48}) {
        const RoutineEntry entry{1,at};
        const RoutineEntry unit=at<0x4A00 ? RoutineEntry{1,0x49B5} : at<0x4C00 ? RoutineEntry{1,0x4A50} : RoutineEntry{1,0x4C70};
        auto spec=k::segment(unit,{k::core_final_at(at)},{.scratch_stack=2});
        spec.single_instruction=true;
        k::add_segment(registry,entry,std::move(spec),[at](auto& z) { return after_call_open_prompt(z,at); });
    }
    for (std::uint16_t at:{0x4A60,0x4A6D}) {
        auto spec=k::segment({1,0x4A50},{k::conditional_at(at)});
        spec.single_instruction=true;
        k::add_segment(registry,{1,at},std::move(spec),[at](auto& z) { return after_prompt_branch(z,at); });
    }
    for (auto entry:interaction_item_check_entries) {
        const auto call=static_cast<std::uint16_t>(entry.address+2);
        k::add_segment(registry,entry,k::segment(entry,{k::core_final_at(call)},{.scratch_stack=2}),
                       [entry](auto& z) { return find_story_item(z,entry.address); });
    }
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_interaction_handoffs_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4946},{1,0x4940},{kit::core_final_at(0x4946)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4946); });
    kit::add_joining(registry,{1,0x496B},{1,0x4966},{kit::conditional_at(0x496B)},12,0,[](auto& z) { return z.final_relative_if(0x496B,kit::Condition::z); });
    kit::add_joining(registry,{1,0x4978},{1,0x4973},{kit::conditional_at(0x4978)},12,0,[](auto& z) { return z.final_relative_if(0x4978,kit::Condition::z); });
    kit::add_joining(registry,{1,0x49FA},{1,0x49E6},{kit::core_final_at(0x49FA)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x49FA); });
    kit::add_joining(registry,{1,0x49FD},{1,0x49E6},{kit::conditional_at(0x49FD)},12,0,[](auto& z) { return z.final_relative_if(0x49FD,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x4A2E},{1,0x49E6},{kit::conditional_at(0x4A2E)},12,0,[](auto& z) { return z.final_relative_if(0x4A2E,kit::Condition::z); });
    kit::add_joining(registry,{1,0x4A97},{1,0x4A50},{kit::returned_at(0x4A97),kit::core_final_at(0x4A97)},kit::taken_conditional_return_cycles,0,[](auto& z) { if (auto exit=z.final_return_if(0x4A97,kit::Condition::nz)) return *exit; return kit::fall_through(0x4A97,0x4A97+1); });
    kit::add_joining(registry,{1,0x4AD9},{1,0x4AD3},{kit::core_final_at(0x4AD9)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4AD9); });
    kit::add_joining(registry,{1,0x4ADC},{1,0x4AD3},{kit::conditional_at(0x4ADC)},12,0,[](auto& z) { return z.final_relative_if(0x4ADC,kit::Condition::z); });
    kit::add_joining(registry,{1,0x4AEA},{1,0x4AD3},{kit::core_final_at(0x4AEA)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4AEA); });
    kit::add_joining(registry,{1,0x4AED},{1,0x4AD3},{kit::conditional_at(0x4AED)},12,0,[](auto& z) { return z.final_relative_if(0x4AED,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x4AFB},{1,0x4AD3},{kit::core_final_at(0x4AFB)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4AFB); });
    kit::add_joining(registry,{1,0x4AFE},{1,0x4AD3},{kit::conditional_at(0x4AFE)},12,0,[](auto& z) { return z.final_relative_if(0x4AFE,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x4BA3},{1,0x4B9D},{kit::core_final_at(0x4BA3)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4BA3); });
    kit::add_joining(registry,{1,0x4C35},{1,0x4C0D},{kit::conditional_at(0x4C35)},12,0,[](auto& z) { return z.final_relative_if(0x4C35,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x4C7C},{1,0x4C70},{kit::core_final_at(0x4C7C)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4C7C); });
    kit::add_joining(registry,{1,0x4D80},{1,0x4D7A},{kit::core_final_at(0x4D80)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4D80); });
    kit::add_joining(registry,{1,0x4D83},{1,0x4D7A},{kit::core_final_at(0x4D83)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4D83); });
    kit::add_joining(registry,{1,0x4DB1},{1,0x4DAB},{kit::core_final_at(0x4DB1)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4DB1); });
    kit::add_joining(registry,{1,0x4DB4},{1,0x4DAB},{kit::conditional_at(0x4DB4)},12,0,[](auto& z) { return z.final_relative_if(0x4DB4,kit::Condition::z); });
    kit::add_joining(registry,{1,0x4DC1},{1,0x4DAB},{kit::conditional_at(0x4DC1)},12,0,[](auto& z) { return z.final_relative_if(0x4DC1,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x4DDA},{1,0x4DD4},{kit::core_final_at(0x4DDA)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4DDA); });
    kit::add_joining(registry,{1,0x4DDD},{1,0x4DD4},{kit::conditional_at(0x4DDD)},12,0,[](auto& z) { return z.final_relative_if(0x4DDD,kit::Condition::z); });
    kit::add_joining(registry,{1,0x4DEA},{1,0x4DD4},{kit::conditional_at(0x4DEA)},12,0,[](auto& z) { return z.final_relative_if(0x4DEA,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x4DEC},{1,0x4DD4},{kit::core_final_at(0x4DEC)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4DEC); });
    kit::add_joining(registry,{1,0x4E7C},{1,0x4DFB},{kit::conditional_at(0x4E7C)},12,0,[](auto& z) { return z.final_relative_if(0x4E7C,kit::Condition::z); });
    kit::add_joining(registry,{1,0x4E89},{1,0x4DFB},{kit::conditional_at(0x4E89)},12,0,[](auto& z) { return z.final_relative_if(0x4E89,kit::Condition::z); });
    kit::add_joining(registry,{1,0x4F1B},{1,0x4F15},{kit::core_final_at(0x4F1B)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4F1B); });
    kit::add_joining(registry,{1,0x4F5E},{1,0x4F58},{kit::core_final_at(0x4F5E)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4F5E); });
    kit::add_joining(registry,{1,0x50E0},{1,0x50DA},{kit::core_final_at(0x50E0)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x50E0); });
    kit::add_joining(registry,{1,0x50E3},{1,0x50DA},{kit::conditional_at(0x50E3)},12,0,[](auto& z) { return z.final_relative_if(0x50E3,kit::Condition::z); });
    kit::add_joining(registry,{1,0x50F0},{1,0x50DA},{kit::conditional_at(0x50F0)},12,0,[](auto& z) { return z.final_relative_if(0x50F0,kit::Condition::nz); });
    kit::add_joining(registry,{1,0x50F2},{1,0x50DA},{kit::core_final_at(0x50F2)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x50F2); });
}
}
