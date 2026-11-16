// SPDX-License-Identifier: GPL-3.0-or-later
// Dialogues with a yes/no prompt followed by one of two replies.
#include "game/interaction_prompts.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit ask_interaction_question(Z& z,std::uint16_t at) {
    z.load_hl(at);
    return k::continues(static_cast<std::uint16_t>(at+3),k::call_cycles);
}
template<class Z> k::Exit select_interaction_reply(Z& z,std::uint16_t at,bool branch_when_zero) {
    z.load_hl(at);
    if (!z.relative_if(static_cast<std::uint16_t>(at+3),z.zero()==branch_when_zero))
        z.load_hl(static_cast<std::uint16_t>(at+5));
    return k::continues(static_cast<std::uint16_t>(at+8),k::jump_cycles);
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
}
