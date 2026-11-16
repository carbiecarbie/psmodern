// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/battle_sprites.h"
#include "game/map_kit.h"

namespace psmodern::game::battle_sprites {
namespace {
namespace k=map_kit;
constexpr auto obj=k::Index::iy;
template<class Z>
k::Exit read_frame_timer(Z& z) {
    z.decrement_indexed(0x5DB1,obj);
    if (!z.sign()) return k::returns(0x5DB4,k::taken_conditional_return_cycles);
    z.return_not_taken(0x5DB4);
    z.load_from_indexed(0x5DB5,obj,z.r.a);
    z.store_to_indexed(0x5DB8,obj,z.r.a);
    z.load_hl(0x5DBB);
    z.store_to_hl(0x5DBE);
    z.load_from_indexed(0x5DC0,obj,z.r.c);
    z.copy(0x5DC3,z.r.l,z.r.c);
    z.load_immediate(0x5DC4,z.r.h);
    z.test_bit_indexed(0x5DC6,obj,7);
    z.load_from_indexed(0x5DCA,obj,z.r.e);
    z.load_from_indexed(0x5DCD,obj,z.r.d);
    return k::relative_if_exit(0x5DD0,!z.zero());
}
template<class Z>
k::Exit read_frame(Z& z) {
    z.add_to_hl(0x5DD8,z.de());
    z.load_from_hl(0x5DD9,z.r.a);
    z.or_a(0x5DDA);
    return k::relative_if_exit(0x5DDB,!z.zero());
}
template<class Z>
k::Exit set_frame(Z& z) {
    z.increment(0x5DEE,z.r.c);
    z.store_to_indexed(0x5DEF,obj,z.r.c);
    z.store_to_indexed(0x5DF2,obj,z.r.a);
    return k::returns(0x5DF5,k::return_cycles);
}
}
void register_animation_sequences(RoutineRegistry& r) {
    auto add=[&](std::uint16_t pc,std::vector<SegmentExit> exits,k::Permissions permissions,auto logic) {
        k::add_segment(r,{1,pc},k::segment(sequence,std::move(exits),permissions),logic);
    };
    for (std::uint16_t pc:{0x5DA0,0x5EAE,0x5EEA}) {
        auto spec=k::segment(sequence,{k::core_final_at(pc)},{.scratch_stack=2});
        spec.single_instruction=true;
        r.add({1,pc},std::move(spec),[](const Machine&){return k::call_cycles;},
              [pc](RoutineContext& c){c.finish_core(pc);});
        add(pc+3,{k::returned_at(pc+6)},{},[pc](auto& z) {
            z.load_from_indexed(pc+3,obj,z.r.a);
            return k::returns(pc+6,k::return_cycles);
        });
    }
    // The first type waits for the active animation and for the effect object to leave.
    add(0x5DA7,{k::returned_at(0x5DAB),k::returned_at(0x5DB0),k::returned_at(0x5DB4),k::conditional_at(0x5DD0)},
        {.slot2_mapper=true},[](auto& z) {
            z.load_a_direct(0x5DA7);
            z.or_a(0x5DAA);
            if (z.zero()) return k::returns(0x5DAB,k::taken_conditional_return_cycles);
            z.return_not_taken(0x5DAB);
            z.load_a_direct(0x5DAC);
            z.or_a(0x5DAF);
            if (!z.zero()) return k::returns(0x5DB0,k::taken_conditional_return_cycles);
            z.return_not_taken(0x5DB0);
            return read_frame_timer(z);
        });
    add(0x5DB1,{k::returned_at(0x5DB4),k::conditional_at(0x5DD0)},{.slot2_mapper=true},
        [](auto& z){return read_frame_timer(z);});
    for (std::uint16_t pc:{0x5DD2,0x5DD8}) add(pc,{k::conditional_at(0x5DDB)},{},[pc](auto& z) {
        if (pc==0x5DD2) {
            z.load_from_indexed(0x5DD2,obj,z.r.e);
            z.load_from_indexed(0x5DD5,obj,z.r.d);
        }
        return read_frame(z);
    });
    add(0x5DDD,{k::conditional_at(0x5DE1)},{},[](auto& z) {
        z.test_bit_indexed(0x5DDD,obj,0);
        return k::relative_if_exit(0x5DE1,z.zero());
    });
    for (std::uint16_t pc:{0x5DE3,0x5DEE}) add(pc,{k::returned_at(0x5DF5)},{},[pc](auto& z) {
        if (pc==0x5DE3) {
            z.store_a_direct(0x5DE3);
            z.store_to_indexed(0x5DE6,obj,z.r.a);
            z.store_a_direct(0x5DE9);
            z.copy(0x5DEC,z.r.c,z.r.a);
            z.load_a_from_pair(0x5DED,z.de());
        }
        return set_frame(z);
    });
    // The first terminator selects the impact effect. The second clears its
    // activity and restarts at the first image of the sequence.
    add(0x5DF6,{k::conditional_at(0x5E02)},{},[](auto& z) {
        z.set_bit_indexed(0x5DF6,obj,0);
        z.increment(0x5DFA,z.r.c);
        z.store_to_indexed(0x5DFB,obj,z.r.c);
        z.test_bit_indexed(0x5DFE,obj,7);
        return k::relative_if_exit(0x5E02,z.zero());
    });
    add(0x5E04,{k::conditional_at(0x5E09)},{},[](auto& z) {
        z.load_a_direct(0x5E04);
        z.compare_immediate(0x5E07);
        return k::relative_if_exit(0x5E09,z.zero());
    });
    for (std::uint16_t pc:{0x5E0B,0x5E17}) add(pc,{k::returned_at(pc+5)},{},[pc](auto& z) {
        z.load_immediate(pc,z.r.a);
        z.store_a_direct(pc+2);
        return k::returns(pc+5,k::return_cycles);
    });
    add(0x5E11,{k::conditional_at(0x5E15)},{},[](auto& z) {
        z.load_a_direct(0x5E11);
        z.or_a(0x5E14);
        return k::relative_if_exit(0x5E15,!z.zero());
    });
    add(0x5E1D,{k::core_final_at(0x5E22)},{.scratch_stack=2},[](auto& z) {
        z.load_immediate(0x5E1D,z.r.a);
        z.store_a_direct(0x5E1F);
        return k::continues(0x5E22,k::call_cycles);
    });
    add(0x5E25,{k::core_final_at(0x5E28)},{},[](auto& z) {
        z.load_a_direct(0x5E25);
        return k::continues(0x5E28,k::jump_cycles);
    });
    add(0x5E2B,{k::returned_at(0x5E2D)},{},[](auto& z) {
        z.load_immediate(0x5E2B,z.r.a);
        return k::returns(0x5E2D,k::return_cycles);
    });
    // Four different frame lists are chosen by the current battle target.
    add(0x5EB5,{k::returned_at(0x5EB9),k::returned_at(0x5EBD),k::core_final_at(0x5EE7)},
        {.slot2_mapper=true},[](auto& z) {
            z.load_a_direct(0x5EB5);
            z.or_a(0x5EB8);
            if (z.zero()) return k::returns(0x5EB9,k::taken_conditional_return_cycles);
            z.return_not_taken(0x5EB9);
            z.decrement_indexed(0x5EBA,obj);
            if (!z.sign()) return k::returns(0x5EBD,k::taken_conditional_return_cycles);
            z.return_not_taken(0x5EBD);
            z.load_from_indexed(0x5EBE,obj,z.r.a);
            z.store_to_indexed(0x5EC1,obj,z.r.a);
            z.load_hl(0x5EC4);
            z.store_to_hl(0x5EC7);
            z.load_a_direct(0x5EC9);
            z.or_a(0x5ECC);
            z.load_de(0x5ECD);
            if (!z.relative_if(0x5ED0,z.zero())) {
                z.decrement(0x5ED2,z.r.a);
                z.load_de(0x5ED3);
                if (!z.relative_if(0x5ED6,z.zero())) {
                    z.decrement(0x5ED8,z.r.a);
                    z.load_de(0x5ED9);
                    if (!z.relative_if(0x5EDC,z.zero())) z.load_de(0x5EDE);
                }
            }
            z.load_from_indexed(0x5EE1,obj,z.r.c);
            z.copy(0x5EE4,z.r.l,z.r.c);
            z.load_immediate(0x5EE5,z.r.h);
            return k::continues(0x5EE7,k::jump_cycles);
        });
    // The large battle animation uploads two tilemap variants, plays the common
    // frame stream, then restores the original tiles when its phase completes.
    add(0x5EF1,{k::returned_at(0x5EF5),k::conditional_at(0x5EFB)},{},[](auto& z) {
        z.load_a_direct(0x5EF1);
        z.or_a(0x5EF4);
        if (z.zero()) return k::returns(0x5EF5,k::taken_conditional_return_cycles);
        z.return_not_taken(0x5EF5);
        z.load_from_indexed(0x5EF6,obj,z.r.a);
        z.compare_immediate(0x5EF9);
        return k::relative_if_exit(0x5EFB,!z.carry());
    });
    add(0x5EFD,{k::returned_at(0x5F00),k::conditional_at(0x5F0C)},{},[](auto& z) {
        z.decrement_indexed(0x5EFD,obj);
        if (!z.sign()) return k::returns(0x5F00,k::taken_conditional_return_cycles);
        z.return_not_taken(0x5F00);
        z.store_immediate_indexed(0x5F01,obj);
        z.increment_indexed(0x5F05,obj);
        z.or_a(0x5F08);
        z.load_hl(0x5F09);
        return k::relative_if_exit(0x5F0C,z.zero());
    });
    for (std::uint16_t pc:{0x5F0E,0x5F11})
        add(pc,{k::core_final_at(0x5F19)},{.slot2_mapper=true},[pc](auto& z) {
            if (pc==0x5F0E) z.load_hl(0x5F0E);
            z.load_immediate(0x5F11,z.r.a);
            z.store_a_direct(0x5F13);
            z.load_de(0x5F16);
            return k::continues(0x5F19,10);
        });
    for (std::uint16_t pc:{0x5F1C,0x5F1D,0x5F20,0x5F26}) {
        auto exit=k::core_final_at(pc);
        if (pc==0x5F1C) exit.interrupt_mask=InterruptMask::disable;
        if (pc==0x5F20) exit.interrupt_mask=InterruptMask::enable;
        const bool call=exit.interrupt_mask==InterruptMask::none;
        auto spec=k::segment(sequence,{exit},{.scratch_stack=call ? 2u : 0u});
        spec.single_instruction=true;
        r.add({1,pc},std::move(spec),[call](const Machine&){return call ? k::call_cycles : 4u;},
              [pc](RoutineContext& c){c.finish_core(pc);});
    }
    add(0x5F22,{k::conditional_at(0x5F24)},{},[](auto& z) {
        z.compare_immediate(0x5F22);
        return k::relative_if_exit(0x5F24,!z.carry());
    });
    add(0x5F29,{k::returned_at(0x5F38),k::returned_at(0x5F44)},{},[](auto& z) {
        z.load_from_indexed(0x5F29,obj,z.r.a);
        z.compare_immediate(0x5F2C);
        if (!z.relative_if(0x5F2E,!z.zero())) z.store_immediate_indexed(0x5F30,obj);
        z.load_a_direct(0x5F34);
        z.or_a(0x5F37);
        if (!z.zero()) return k::returns(0x5F38,k::taken_conditional_return_cycles);
        z.return_not_taken(0x5F38);
        z.decrement(0x5F39,z.r.a);
        z.store_a_direct(0x5F3A);
        z.increment_indexed(0x5F3D,obj);
        z.store_immediate_indexed(0x5F40,obj);
        return k::returns(0x5F44,k::return_cycles);
    });
    add(0x5F45,{k::returned_at(0x5F48),k::conditional_at(0x5F55)},{},[](auto& z) {
        z.decrement_indexed(0x5F45,obj);
        if (!z.sign()) return k::returns(0x5F48,k::taken_conditional_return_cycles);
        z.return_not_taken(0x5F48);
        z.store_immediate_indexed(0x5F49,obj);
        z.increment_indexed(0x5F4D,obj);
        z.compare_immediate(0x5F50);
        z.load_hl(0x5F52);
        return k::relative_if_exit(0x5F55,!z.zero());
    });
    add(0x5F57,{k::core_final_at(0x5F61)},{},[](auto& z) {
        z.xor_a(0x5F57);
        z.store_to_indexed(0x5F58,obj,z.r.a);
        z.store_a_direct(0x5F5B);
        z.load_hl(0x5F5E);
        return k::continues(0x5F61,12);
    });

}
}
