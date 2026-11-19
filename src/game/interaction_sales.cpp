// SPDX-License-Identifier: GPL-3.0-or-later
// Gas shield and story purchase: test balance before committing the sale.
#include "game/joins.h"
#include "game/interaction_sales.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k=menu_kit;
template<class Z> k::Exit check_sale_price(Z& z,std::uint16_t at) {
    z.load_de(at);
    z.load_hl_direct(static_cast<std::uint16_t>(at+3));
    z.or_a(static_cast<std::uint16_t>(at+6));
    z.subtract_hl_with_carry(static_cast<std::uint16_t>(at+7),z.de());
    return z.final_relative_if(static_cast<std::uint16_t>(at+9),k::Condition::nc);
}
template<class Z> k::Exit pay_for_shield(Z& z) {
    z.store_hl_direct(0x4FC3);
    z.load_hl(0x4FC6);
    return z.final_call(0x4FC9);
}
template<class Z> k::Exit find_shield(Z& z) {
    z.load_immediate(0x4FCC,z.r.a);
    z.store_a_direct(0x4FCE);
    return z.final_call(0x4FD1);
}
template<class Z> k::Exit commit_story_purchase(Z& z) {
    z.store_hl_direct(0x5098);
    z.load_immediate(0x509B,z.r.a);
    z.store_a_direct(0x509D);
    z.load_hl(0x50A0);
    return z.final_jump(0x50A3);
}
}
void register_interaction_sales(RoutineRegistry& registry) {
    using k::core_final_at; using k::segment;
    k::add_segment(registry,{1,0x4FB2},segment({1,0x4F9B},{k::conditional_at(0x4FBB)}),[](auto& z) { return check_sale_price(z,0x4FB2); });
    k::add_segment(registry,{1,0x5087},segment({1,0x5070},{k::conditional_at(0x5090)}),[](auto& z) { return check_sale_price(z,0x5087); });
    k::add_segment(registry,{1,0x4FC3},segment({1,0x4F9B},{core_final_at(0x4FC9)},{.scratch_stack=2}),[](auto& z) { return pay_for_shield(z); });
    k::add_segment(registry,{1,0x4FCC},segment({1,0x4F9B},{core_final_at(0x4FD1)},{.scratch_stack=2}),[](auto& z) { return find_shield(z); });
    k::add_segment(registry,{1,0x5098},segment({1,0x5070},{core_final_at(0x50A3)}),[](auto& z) { return commit_story_purchase(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_interaction_sales_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4FD4},{1,0x4F9B},{kit::returned_at(0x4FD4),kit::core_final_at(0x4FD4)},kit::taken_conditional_return_cycles,0,[](auto& z) { if (auto exit=z.final_return_if(0x4FD4,kit::Condition::z)) return *exit; return kit::fall_through(0x4FD4,0x4FD4+1); });
    kit::add_joining(registry,{1,0x4FD5},{1,0x4F9B},{kit::core_final_at(0x4FD5)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x4FD5); });
}
}
