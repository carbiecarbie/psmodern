// SPDX-License-Identifier: GPL-3.0-or-later
// Item handover and scene changes during story interactions.
#include "game/interaction_trade.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit choose_story_reply(Z& z) {
    z.load_hl(0x4F1E);
    if (!z.relative_if(0x4F21,z.zero())) {
        z.load_a_direct(0x4F23);
        z.or_a(0x4F26);
        if (!z.relative_if(0x4F27,!z.zero())) {
            z.load_immediate(0x4F29,z.r.a);
            z.store_a_direct(0x4F2B);
        }
        z.load_hl(0x4F2E);
    }
    return k::continues(0x4F31,k::jump_cycles);
}
template<class Z> k::Exit hand_over_item(Z& z) {
    z.push(0x4DC3,z.r.b,z.r.c);
    return k::Exit{k::core_final_at(0x4DC4,-2),k::call_cycles};
}
template<class Z> k::Exit acknowledge_handover(Z& z) {
    z.pop(0x4DC7,z.r.b,z.r.c);
    z.load_hl(0x4DC8);
    return k::Exit{k::core_final_at(0x4DCB,2),k::jump_cycles};
}
template<class Z> k::Exit prepare_story_scene(Z& z,std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(static_cast<std::uint16_t>(at+2));
    return k::continues(static_cast<std::uint16_t>(at+5),k::call_cycles);
}
template<class Z> k::Exit set_story_actor(Z& z) {
    z.load_immediate(0x4EA5,z.r.a);
    return k::continues(0x4EA7,k::call_cycles);
}
}
void register_interaction_trade(RoutineRegistry& registry) {
    using k::core_final_at; using k::segment;
    k::add_segment(registry,{1,0x4F1E},segment({1,0x4F15},{core_final_at(0x4F31)}),[](auto& z) { return choose_story_reply(z); });
    k::add_segment(registry,{1,0x4DC3},segment({1,0x4DAB},{core_final_at(0x4DC4,-2)},{.scratch_stack=4}),[](auto& z) { return hand_over_item(z); });
    k::add_segment(registry,{1,0x4DC7},segment({1,0x4DAB},{core_final_at(0x4DCB,2)},{.inherited_stack=2}),[](auto& z) { return acknowledge_handover(z); });
    for (std::uint16_t at:{0x4D86,0x4D91})
        k::add_segment(registry,{1,at},segment({1,0x4D7A},{core_final_at(static_cast<std::uint16_t>(at+5))},{.scratch_stack=2}),[at](auto& z) { return prepare_story_scene(z,at); });
    k::add_segment(registry,{1,0x4EA5},segment({1,0x4E9C},{core_final_at(0x4EA7)},{.scratch_stack=2}),[](auto& z) { return set_story_actor(z); });
}
}
