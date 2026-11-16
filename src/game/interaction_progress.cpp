// SPDX-License-Identifier: GPL-3.0-or-later
// Follow party and story progress through the interactions' decisions.
#include "game/interaction_progress.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit test_party_presence(Z& z,std::uint16_t at) {
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    return k::relative_if_exit(static_cast<std::uint16_t>(at+4),z.zero());
}
template<class Z> k::Exit test_story_progress(Z& z) {
    z.load_a_direct(0x4E0A);
    z.compare_immediate(0x4E0D);
    return k::relative_if_exit(0x4E0F,(z.r.f&k::carry_flag)!=0);
}
template<class Z> k::Exit compare_story_progress(Z& z,std::uint16_t at) {
    z.compare_immediate(at);
    return k::relative_if_exit(static_cast<std::uint16_t>(at+2),(z.r.f&k::carry_flag)==0);
}
}
void register_interaction_progress(RoutineRegistry& registry) {
    using k::conditional_at; using k::segment;
    k::add_segment(registry,{1,0x4E5B},segment({1,0x4DFB},{conditional_at(0x4E5D)}),[](auto& z) { return compare_story_progress(z,0x4E5B); });
    k::add_segment(registry,{1,0x4E69},segment({1,0x4DFB},{conditional_at(0x4E6B)}),[](auto& z) { return compare_story_progress(z,0x4E69); });
    k::add_segment(registry,{1,0x4A23},segment({1,0x49E6},{conditional_at(0x4A27)}),[](auto& z) { return test_party_presence(z,0x4A23); });
    k::add_segment(registry,{1,0x49EE},segment({1,0x49E6},{conditional_at(0x49F2)}),[](auto& z) { return test_party_presence(z,0x49EE); });
    k::add_segment(registry,{1,0x4C0D},segment({1,0x4C0D},{conditional_at(0x4C11)}),[](auto& z) { return test_party_presence(z,0x4C0D); });
    k::add_segment(registry,{1,0x4E0A},segment({1,0x4DFB},{conditional_at(0x4E0F)}),[](auto& z) { return test_story_progress(z); });
    k::add_segment(registry,{1,0x4E17},segment({1,0x4DFB},{conditional_at(0x4E19)}),[](auto& z) { return compare_story_progress(z,0x4E17); });
    k::add_segment(registry,{1,0x4E21},segment({1,0x4DFB},{conditional_at(0x4E23)}),[](auto& z) { return compare_story_progress(z,0x4E21); });
}
}
