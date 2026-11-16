// SPDX-License-Identifier: GPL-3.0-or-later
// Apply the consequences of selected yes/no dialogue replies.
#include "game/interaction_replies.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit prepare_passport_price(Z& z) {
    z.load_hl(0x4B06);
    z.store_hl_direct(0x4B09);
    z.load_hl(0x4B0C);
    return k::continues(0x4B0F,k::call_cycles);
}
template<class Z> k::Exit check_passport_price(Z& z) {
    z.load_de(0x4B1D);
    z.load_hl_direct(0x4B20);
    z.or_a(0x4B23);
    z.subtract_hl_with_carry(0x4B24,z.de());
    return k::relative_if_exit(0x4B26,!z.carry());
}
template<class Z> k::Exit report_passport_shortfall(Z& z) {
    z.load_hl(0x4B28);
    return k::continues(0x4B2B,k::jump_cycles);
}
template<class Z> k::Exit pay_passport_price(Z& z) {
    z.store_hl_direct(0x4B2E);
    z.load_hl(0x4B31);
    return k::continues(0x4B34,k::call_cycles);
}
template<class Z> k::Exit find_passport(Z& z) {
    z.load_immediate(0x4B37,z.r.a);
    z.store_a_direct(0x4B39);
    return k::continues(0x4B3C,k::call_cycles);
}
template<class Z> k::Exit mark_story_destination(Z& z) {
    z.load_immediate(0x49D5,z.r.a);
    z.store_a_direct(0x49D7);
    z.load_hl(0x49DA);
    return k::continues(0x49DD,k::jump_cycles);
}
// Continue the story only when the prompt or inventory result permits it.
template<class Z> k::Exit check_story_reply(Z& z,std::uint16_t at) {
    z.load_hl(at);
    return k::relative_if_exit(static_cast<std::uint16_t>(at+3),!z.zero());
}
// The chosen destination record depends on the current map position.
template<class Z> k::Exit choose_transfer_destination(Z& z) {
    z.load_a_direct(0x4A7B);
    z.compare_immediate(0x4A7E);
    z.load_hl(0x4A80);
    if (!z.relative_if(0x4A83,!z.zero()))
        z.load_hl(0x4A85);
    return k::continues(0x4A88,k::call_cycles);
}
template<class Z> k::Exit choose_story_reply(Z& z) {
    z.load_hl(0x4949);
    if (!z.relative_if(0x494C,z.zero())) {
        z.load_hl(0x494E);
        z.store_hl_direct(0x4951);
        z.load_hl(0x4954);
    }
    return k::continues(0x4957,k::jump_cycles);
}
template<class Z> k::Exit choose_flagged_reply(Z& z,std::uint16_t at) {
    z.load_hl(at);
    if (!z.relative_if(static_cast<std::uint16_t>(at+3),!z.zero())) {
        z.load_immediate(static_cast<std::uint16_t>(at+5),z.r.a);
        z.store_a_direct(static_cast<std::uint16_t>(at+7));
        z.load_hl(static_cast<std::uint16_t>(at+10));
    }
    return k::continues(static_cast<std::uint16_t>(at+13),k::jump_cycles);
}
template<class Z> k::Exit classify_item_reply(Z& z) {
    z.load_hl(0x4BA6);
    return k::relative_if_exit(0x4BA9,!z.zero());
}
template<class Z> k::Exit find_reply_item(Z& z) {
    z.load_immediate(0x4BAB,z.r.a);
    return k::continues(0x4BAD,k::call_cycles);
}
template<class Z> k::Exit clear_story_marker(Z& z) {
    z.load_hl(0x4BB2);
    z.store_to_hl(0x4BB5);
    z.load_hl(0x4BB7);
    return k::continues(0x4BBA,k::jump_cycles);
}
}
void register_interaction_replies(RoutineRegistry& registry) {
    using k::core_final_at; using k::segment;
    k::add_segment(registry,{1,0x4B06},segment({1,0x4AD3},{core_final_at(0x4B0F)},{.scratch_stack=2}),[](auto& z) { return prepare_passport_price(z); });
    k::add_segment(registry,{1,0x4B1D},segment({1,0x4AD3},{k::conditional_at(0x4B26)}),[](auto& z) { return check_passport_price(z); });
    k::add_segment(registry,{1,0x4B28},segment({1,0x4AD3},{core_final_at(0x4B2B)}),[](auto& z) { return report_passport_shortfall(z); });
    k::add_segment(registry,{1,0x4B2E},segment({1,0x4AD3},{core_final_at(0x4B34)},{.scratch_stack=2}),[](auto& z) { return pay_passport_price(z); });
    k::add_segment(registry,{1,0x4B37},segment({1,0x4AD3},{core_final_at(0x4B3C)},{.scratch_stack=2}),[](auto& z) { return find_passport(z); });
    k::add_segment(registry,{1,0x49D5},segment({1,0x49B5},{core_final_at(0x49DD)}),[](auto& z) { return mark_story_destination(z); });
    k::add_segment(registry,{1,0x49C6},segment({1,0x49B5},{k::conditional_at(0x49C9)}),[](auto& z) { return check_story_reply(z,0x49C6); });
    k::add_segment(registry,{1,0x49D0},segment({1,0x49B5},{k::conditional_at(0x49D3)}),[](auto& z) { return check_story_reply(z,0x49D0); });
    k::add_segment(registry,{1,0x4A7B},segment({1,0x4A50},{core_final_at(0x4A88)},{.scratch_stack=2}),[](auto& z) { return choose_transfer_destination(z); });
    k::add_segment(registry,{1,0x4949},segment({1,0x4940},{core_final_at(0x4957)}),[](auto& z) { return choose_story_reply(z); });
    k::add_segment(registry,{1,0x4AAE},segment({1,0x4AA9},{core_final_at(0x4ABB)}),[](auto& z) { return choose_flagged_reply(z,0x4AAE); });
    k::add_segment(registry,{1,0x4AC3},segment({1,0x4ABE},{core_final_at(0x4AD0)}),[](auto& z) { return choose_flagged_reply(z,0x4AC3); });
    k::add_segment(registry,{1,0x4F61},segment({1,0x4F58},{core_final_at(0x4F6E)}),[](auto& z) { return choose_flagged_reply(z,0x4F61); });
    k::add_segment(registry,{1,0x4BA6},segment({1,0x4B9D},{k::conditional_at(0x4BA9)}),[](auto& z) { return classify_item_reply(z); });
    k::add_segment(registry,{1,0x4BAB},segment({1,0x4B9D},{core_final_at(0x4BAD)},{.scratch_stack=2}),[](auto& z) { return find_reply_item(z); });
    k::add_segment(registry,{1,0x4BB2},segment({1,0x4B9D},{core_final_at(0x4BBA)}),[](auto& z) { return clear_story_marker(z); });
}
}
