// SPDX-License-Identifier: GPL-3.0-or-later
// Story-flag updates and handoffs for the first two interactions.
#include "game/interaction_story.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
// Preserve the inventory result while the existing item-removal helper runs.
template<class Z> k::Exit remove_story_item(Z& z) {
    z.push(0x4A98,z.r.b,z.r.c);
    return k::continues(0x4A99,k::call_cycles);
}
template<class Z> k::Exit finish_item_removal(Z& z) {
    z.pop(0x4A9C,z.r.b,z.r.c);
    z.load_hl(0x4A9D);
    return k::continues(0x4AA0,k::jump_cycles);
}
// After the item check, the game chooses a dialogue then records the
// direction-derived destination when that dialogue returns.
template<class Z> k::Exit show_item_destination(Z& z) {
    z.load_hl(0x4980);
    return k::continues(0x4983,k::call_cycles);
}
template<class Z> k::Exit remember_item_destination(Z& z) {
    z.load_a_direct(0x4986);
    z.rotate_right_circular_a(0x4989);
    z.decrement(0x498A,z.r.a);
    z.and_immediate(0x498B);
    z.store_a_direct(0x498D);
    return k::returns(0x4990,k::return_cycles);
}
template<class Z> k::Exit begin_first_story(Z& z) {
    z.store_to_hl(0x48E6);
    z.load_hl(0x48E8);
    return k::continues(0x48EB,k::call_cycles);
}
template<class Z> k::Exit show_following_story(Z& z) {
    z.load_hl(0x48EE);
    return k::continues(0x48F1,k::call_cycles);
}
template<class Z> k::Exit finish_first_story(Z& z) {
    z.load_immediate(0x48F4,z.r.a);
    z.store_a_direct(0x48F6);
    return k::continues(0x48F9,k::jump_cycles);
}
template<class Z> k::Exit explain_story_item(Z& z) {
    z.load_hl(0x4902);
    return k::continues(0x4905,k::call_cycles);
}
template<class Z> k::Exit give_story_item(Z& z) {
    z.load_immediate(0x4908,z.r.a);
    z.store_a_direct(0x490A);
    return k::continues(0x490D,k::call_cycles);
}
template<class Z> k::Exit check_story_item(Z& z) {
    z.load_immediate(0x4910,z.r.a);
    return k::continues(0x4912,k::call_cycles);
}
template<class Z> k::Exit finish_story_item(Z& z) {
    z.load_hl(0x491C);
    return k::continues(0x491F,k::jump_cycles);
}
template<class Z> k::Exit mark_story_item(Z& z) {
    z.load_immediate(0x4917,z.r.a);
    z.store_a_direct(0x4919);
    return finish_story_item(z);
}
}
void register_interaction_story(RoutineRegistry& registry) {
    using k::core_final_at; using k::segment;
    k::add_segment(registry,{1,0x4A98},segment({1,0x4A50},{core_final_at(0x4A99,-2)},{.scratch_stack=4}),[](auto& z) { return remove_story_item(z); });
    k::add_segment(registry,{1,0x4A9C},segment({1,0x4A50},{core_final_at(0x4AA0,2)},{.inherited_stack=2}),[](auto& z) { return finish_item_removal(z); });
    k::add_segment(registry,{1,0x4980},segment({1,0x4973},{core_final_at(0x4983)},{.scratch_stack=2}),[](auto& z) { return show_item_destination(z); });
    k::add_segment(registry,{1,0x4986},segment({1,0x4973},{k::returned_at(0x4990)}),[](auto& z) { return remember_item_destination(z); });
    k::add_segment(registry,{1,0x48E6},segment({1,0x48DF},{core_final_at(0x48EB)},{.scratch_stack=2}),[](auto& z) { return begin_first_story(z); });
    k::add_segment(registry,{1,0x48EE},segment({1,0x48DF},{core_final_at(0x48F1)},{.scratch_stack=2}),[](auto& z) { return show_following_story(z); });
    k::add_segment(registry,{1,0x48F4},segment({1,0x48DF},{core_final_at(0x48F9)}),[](auto& z) { return finish_first_story(z); });
    k::add_segment(registry,{1,0x4902},segment({1,0x48FC},{core_final_at(0x4905)},{.scratch_stack=2}),[](auto& z) { return explain_story_item(z); });
    k::add_segment(registry,{1,0x4908},segment({1,0x48FC},{core_final_at(0x490D)},{.scratch_stack=2}),[](auto& z) { return give_story_item(z); });
    k::add_segment(registry,{1,0x4910},segment({1,0x48FC},{core_final_at(0x4912)},{.scratch_stack=2}),[](auto& z) { return check_story_item(z); });
    k::add_segment(registry,{1,0x491C},segment({1,0x48FC},{core_final_at(0x491F)}),[](auto& z) { return finish_story_item(z); });
    k::add_segment(registry,{1,0x4917},segment({1,0x48FC},{core_final_at(0x491F)}),[](auto& z) { return mark_story_item(z); });
}
}
