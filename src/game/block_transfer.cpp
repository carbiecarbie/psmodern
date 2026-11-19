// SPDX-License-Identifier: GPL-3.0-or-later
// The game's block copies (LDIR), each a registered slice of at most sixteen
// transfers whose every transfer, the last one included, is rendered here
// (milestone 14). Each transfer is an instruction of its own: an interrupt or
// a frame end is taken between two of them exactly as in the original.
#include "game/joins.h"
#include "game/dungeon_kit.h"
#include "game/routines.h"

namespace psmodern::game {
void register_block_transfer(RoutineRegistry& registry,RoutineEntry entry) {
    register_block_transfer(registry,entry,false);
}
void register_block_transfer(RoutineRegistry& registry,RoutineEntry entry,bool cross_frame_final) {
    SegmentExit exit{RoutineExit::copy_repeat,entry.address};
    exit.native_frame_tail=cross_frame_final;
    // Copy in source order, including overlap. Leave the remaining count in
    // BC and both pointers at the next transfer when a slice ends. A slice is
    // the next sixteen transfers (or the rest); the two scene palette copies
    // declared with a frame tail keep the slice the machine computes for them
    // (Machine::block_copy_frame_cycles).
    registry.add_exact(entry,kit::segment(entry,{exit}),[=](const Machine& machine) -> std::uint64_t {
        if (cross_frame_final) return machine.block_copy_frame_cycles();
        const auto remaining=machine.cpu().bc.uint16_value;
        return copy_duration(remaining,copy_iterations(remaining));
    },[=](RoutineContext& context) {
        const unsigned count=context.copy_count();
        kit::run(context,[&](auto& z) { return z.final_block_copy(entry.address,count); });
    });
}
void register_block_transfers(RoutineRegistry& registry) {
    for (auto entry:block_transfer_entries) register_block_transfer(registry,entry);
    // The instructions that follow three copies, each a single-instruction
    // segment (joining code between C++ segments, milestone 14), with the
    // copy as its unit: the map-mode copy at 00:0B67 returns to its caller;
    // the copy at 00:15E7 goes on to update the map objects (01:576A), and
    // the one at 01:4061 to decode the name-entry keyboard (01:4166).
    auto single=[&](RoutineEntry entry, RoutineEntry unit, SegmentExit exit, unsigned scratch, std::uint64_t cycles, auto logic) {
        auto spec=kit::segment(unit,{exit},{.scratch_stack=scratch});
        spec.single_instruction=true;
        kit::add_segment(registry,entry,std::move(spec),[cycles](const Machine&) { return cycles; },logic);
    };
    single({0,0x0B69},{0,0x0B67},kit::returned_at(0x0B69),0,kit::return_cycles,[](auto& z) { return z.final_return(0x0B69); });
    single({0,0x15E9},{0,0x15E7},kit::core_final_at(0x15E9),2,kit::call_cycles,[](auto& z) { return z.final_call(0x15E9); });
    single({1,0x4063},{1,0x4061},kit::core_final_at(0x4063),2,kit::call_cycles,[](auto& z) { return z.final_call(0x4063); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_block_transfer_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4CF8},{1,0x4CF6},{kit::core_final_at(0x4CF8)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4CF8); });
}
}
