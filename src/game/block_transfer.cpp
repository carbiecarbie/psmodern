// SPDX-License-Identifier: GPL-3.0-or-later
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
    // BC and both pointers at the next transfer when a window ends.
    registry.add(entry,kit::segment(entry,{exit}),[=](const Machine& machine) {
        return cross_frame_final ? machine.block_copy_frame_cycles() : machine.block_copy_cycles();
    },[=](RoutineContext& context) {
        const unsigned count=context.copy_count();
        kit::run(context,[&](auto& z) {
            for (unsigned i=1;i<count;++i) z.block_copy_byte(entry.address);
            return kit::Exit{exit,z.bc()==1 ? 16u : 21u};
        });
    });
}
void register_block_transfers(RoutineRegistry& registry) {
    for (auto entry:block_transfer_entries) register_block_transfer(registry,entry);
}
}
