// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_view.h"
#include "game/map_kit.h"

namespace psmodern::game::dungeon_view {
namespace {
namespace k=map_kit;

// A spell can use a type-eight cell only if its floor and cell have no
// matching object record. Test one seven-byte record per segment.
template<class Z> k::Exit object_record(Z& z) {
    z.load_from_hl(0x6863,z.r.a);
    z.compare_immediate(0x6864);
    if (z.relative_if(0x6866,z.zero())) {
        z.load_immediate(0x6876,z.r.a);
        z.or_a(0x6878);
        return k::returns(0x6879,k::return_cycles);
    }
    z.increment_hl(0x6868);
    z.compare_register(0x6869,z.r.b);
    if (!z.relative_if(0x686A,!z.zero())) {
        z.load_from_hl(0x686C,z.r.a);
        z.compare_register(0x686D,z.r.c);
        if (z.relative_if(0x686E,z.zero())) {
            z.xor_a(0x6874);
            return k::returns(0x6875,k::return_cycles);
        }
    }
    z.add_to_hl(0x6870,z.de());
    return k::continues(0x6871,k::jump_cycles);
}
template<class Z> k::Exit select_object_table(Z& z) {
    z.copy(0x6853,z.r.c,z.r.l);
    z.load_a_direct(0x6854);
    z.copy(0x6857,z.r.b,z.r.a);
    z.load_hl(0x6858);
    z.store_to_hl(0x685B);
    z.load_hl(0x685D);
    z.load_de(0x6860);
    return object_record(z);
}
}
void register_escape(RoutineRegistry& registry) {
    auto add=[&](RoutineEntry unit,std::uint16_t pc,std::vector<SegmentExit> exits,k::Permissions perms,auto logic) {
        k::add_segment(registry,{1,pc},k::segment(unit,std::move(exits),perms),logic);
    };
    auto query=[&](RoutineEntry unit,std::uint16_t pc) {
        add(unit,pc,{k::core_final_at(pc+2)},{.scratch_stack=2},[pc](auto& z) {
            z.load_immediate(pc,z.r.b);
            return k::continues(pc+2,k::call_cycles);
        });
    };
    const RoutineEntry cell{1,0x684A},choices{1,0x687A},retreat{1,0x688C};
    query(cell,0x684A);
    const std::vector<SegmentExit> record_exits{k::returned_at(0x6875),k::returned_at(0x6879),k::core_final_at(0x6871)};
    add(cell,0x684F,record_exits,{.slot2_mapper=true},[](auto& z) {
        z.compare_immediate(0x684F);
        if (!z.relative_if(0x6851,!z.zero())) return select_object_table(z);
        z.xor_a(0x6874);
        return k::returns(0x6875,k::return_cycles);
    });
    add(cell,0x6863,record_exits,{},[](auto& z){return object_record(z);});

    query(choices,0x687A);
    for (std::uint16_t pc:{0x687F,0x6885}) {
        add(choices,pc,{k::returned_at(pc),k::core_final_at(pc+3)},{.scratch_stack=2},[pc](auto& z) {
            if (z.zero()) return k::returns(pc,k::taken_conditional_return_cycles);
            z.return_not_taken(pc);
            z.load_immediate(pc+1,z.r.b);
            return k::continues(pc+3,k::call_cycles);
        });
    }
    query(retreat,0x688C);
    for (auto [pc,taken]:{std::pair{0x6891,0x68AF},std::pair{0x6898,0x68AC}}) {
        add(retreat,pc,{k::core_final_at(taken),k::core_final_at(pc+4)},{.scratch_stack=2},[pc,taken](auto& z) {
            if (z.relative_if(pc,pc==0x6891 ? z.zero() : !z.zero())) return k::continues(taken,k::call_cycles);
            z.load_immediate(pc+2,z.r.b);
            return k::continues(pc+4,k::call_cycles);
        });
    }
    add(retreat,0x689F,{k::core_final_at(0x68A7),k::core_final_at(0x68A1)},{.scratch_stack=2},[](auto& z) {
        return k::continues(z.relative_if(0x689F,!z.zero()) ? 0x68A7 : 0x68A1,k::call_cycles);
    });
    add(retreat,0x68A4,{k::core_final_at(0x68AC),k::core_final_at(0x68A7)},{.scratch_stack=2},[](auto& z) {
        z.rotate_right_circular_a(0x68A4);
        return k::continues(z.relative_if(0x68A5,!z.carry()) ? 0x68AC : 0x68A7,k::call_cycles);
    });
    add(retreat,0x68AA,{k::core_final_at(0x68AF)},{.scratch_stack=2},[](auto& z) {
        z.relative(0x68AA);
        return k::continues(0x68AF,k::call_cycles);
    });
    query(retreat,0x68B2);
    add(retreat,0x68B7,{k::core_final_at(0x68B9)},{},[](auto& z) {
        z.load_immediate(0x68B7,z.r.b);
        return k::continues(0x68B9,k::jump_cycles);
    });
}
}
