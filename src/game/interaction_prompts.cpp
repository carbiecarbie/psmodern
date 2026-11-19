// SPDX-License-Identifier: GPL-3.0-or-later
// Dialogues with a yes/no prompt followed by one of two replies.
#include "game/joins.h"
#include "game/interaction_prompts.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit ask_interaction_question(Z& z,std::uint16_t at) {
    z.load_hl(at);
    return z.final_call(static_cast<std::uint16_t>(at+3));
}
template<class Z> k::Exit select_interaction_reply(Z& z,std::uint16_t at,bool branch_when_zero) {
    z.load_hl(at);
    if (!z.relative_if(static_cast<std::uint16_t>(at+3),z.zero()==branch_when_zero))
        z.load_hl(static_cast<std::uint16_t>(at+5));
    return z.final_jump(static_cast<std::uint16_t>(at+8));
}
}
void register_interaction_prompts(RoutineRegistry& registry) {
    for (auto prompt:interaction_prompts) {
        k::add_segment(registry,prompt.entry,k::segment(prompt.entry,{k::core_final_at(static_cast<std::uint16_t>(prompt.entry.address+3))},{.scratch_stack=2}),
                       [prompt](auto& z) { return ask_interaction_question(z,prompt.entry.address); });
        k::add_segment(registry,prompt.answer,k::segment(prompt.entry,{k::core_final_at(static_cast<std::uint16_t>(prompt.answer.address+8))}),
                       [prompt](auto& z) { return select_interaction_reply(z,prompt.answer.address,prompt.branch_when_zero); });
    }
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_interaction_prompts_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4BC3},{1,0x4BBD},{kit::core_final_at(0x4BC3)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4BC3); });
    kit::add_joining(registry,{1,0x4ECB},{1,0x4EC5},{kit::core_final_at(0x4ECB)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4ECB); });
    kit::add_joining(registry,{1,0x5032},{1,0x502C},{kit::core_final_at(0x5032)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x5032); });
    kit::add_joining(registry,{1,0x50AC},{1,0x50A6},{kit::core_final_at(0x50AC)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x50AC); });
    kit::add_joining(registry,{1,0x50CC},{1,0x50C6},{kit::core_final_at(0x50CC)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x50CC); });
}
}
