// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/joins.h"
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
        return z.final_return(0x6879);
    }
    z.increment_hl(0x6868);
    z.compare_register(0x6869,z.r.b);
    if (!z.relative_if(0x686A,!z.zero())) {
        z.load_from_hl(0x686C,z.r.a);
        z.compare_register(0x686D,z.r.c);
        if (z.relative_if(0x686E,z.zero())) {
            z.xor_a(0x6874);
            return z.final_return(0x6875);
        }
    }
    z.add_to_hl(0x6870,z.de());
    return z.final_jump(0x6871);
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
            return z.final_call(pc+2);
        });
    };
    const RoutineEntry cell{1,0x684A},choices{1,0x687A},retreat{1,0x688C};
    query(cell,0x684A);
    const std::vector<SegmentExit> record_exits{k::returned_at(0x6875),k::returned_at(0x6879),k::core_final_at(0x6871)};
    add(cell,0x684F,record_exits,{.slot2_mapper=true},[](auto& z) {
        z.compare_immediate(0x684F);
        if (!z.relative_if(0x6851,!z.zero())) return select_object_table(z);
        z.xor_a(0x6874);
        return z.final_return(0x6875);
    });
    add(cell,0x6863,record_exits,{},[](auto& z){return object_record(z);});

    query(choices,0x687A);
    for (std::uint16_t pc:{0x687F,0x6885}) {
        add(choices,pc,{k::returned_at(pc),k::core_final_at(pc+3)},{.scratch_stack=2},[pc](auto& z) {
            if (auto exit=z.final_return_if(pc,k::Condition::z)) return *exit;
            z.load_immediate(pc+1,z.r.b);
            return z.final_call(pc+3);
        });
    }
    query(retreat,0x688C);
    for (auto [pc,taken]:{std::pair{0x6891,0x68AF},std::pair{0x6898,0x68AC}}) {
        add(retreat,pc,{k::core_final_at(taken),k::core_final_at(pc+4)},{.scratch_stack=2},[pc,taken](auto& z) {
            if (z.relative_if(pc,pc==0x6891 ? k::Condition::z : k::Condition::nz)) return z.final_call(taken);
            z.load_immediate(pc+2,z.r.b);
            return z.final_call(pc+4);
        });
    }
    add(retreat,0x689F,{k::core_final_at(0x68A7),k::core_final_at(0x68A1)},{.scratch_stack=2},[](auto& z) {
        return z.relative_if(0x689F,k::Condition::nz) ? z.final_call(0x68A7) : z.final_call(0x68A1);
    });
    add(retreat,0x68A4,{k::core_final_at(0x68AC),k::core_final_at(0x68A7)},{.scratch_stack=2},[](auto& z) {
        z.rotate_right_circular_a(0x68A4);
        return z.relative_if(0x68A5,k::Condition::nc) ? z.final_call(0x68AC) : z.final_call(0x68A7);
    });
    add(retreat,0x68AA,{k::core_final_at(0x68AF)},{.scratch_stack=2},[](auto& z) {
        z.relative(0x68AA);
        return z.final_call(0x68AF);
    });
    query(retreat,0x68B2);
    add(retreat,0x68B7,{k::core_final_at(0x68B9)},{},[](auto& z) {
        z.load_immediate(0x68B7,z.r.b);
        return z.final_jump(0x68B9);
    });
}
}
namespace psmodern::game {
// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_dungeon_escape_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x688B},{1,0x687A},{kit::returned_at(0x688B)},kit::return_cycles,0,[](auto& z) { return z.final_return(0x688B); });
    kit::add_joining(registry,{1,0x68AF},{1,0x688C},{kit::core_final_at(0x68AF)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x68AF); });
}
}
