// SPDX-License-Identifier: GPL-3.0-or-later
// The game's random-number routine (00:05B1), rewritten as one exact segment
// on the shared rendering kit: every instruction, its final return included,
// is rendered, and an interrupt can be taken between any two of them.
#include "game/dungeon_kit.h"
#include "game/routines.h"
#include "machine/machine.h"

namespace psmodern::game {
namespace {
namespace k=kit;
constexpr std::uint16_t return_address=0x05CF;
// Cycles including the final return: 151 when the new state is nonzero; the
// recovery path skips no branch (7 instead of 12 cycles) and loads a fixed
// state (10 cycles), five more in all. The longer one is the bound when the
// estimate cannot know the path (a state read from the open bus).
constexpr std::uint64_t recovery_cycles=156;

// 00:05B1-05CF. The two-byte state shifts left by one and takes in, at bit 0,
// a mix of its own bits; a zero state would stay zero forever and is replaced
// by a fixed one. The draw returned in A is the refresh counter mixed with
// the new state's low byte; the counter never enters the stored state. HL is
// kept on the stack meanwhile and read back from there, whatever the stack
// holds then (a stack in ROM, or on the state itself, reads back something
// else, as in the original).
template<class Z> k::Exit advance_random_state(Z& z) {
    auto& r=z.r;
    z.push(0x05B1,r.h,r.l);
    z.load_hl_direct(0x05B2);
    // Mix the state's bits into one carry, then double the state and add it.
    z.copy(0x05B5,r.a,r.h);
    z.rotate_right_circular_a(0x05B6);
    z.rotate_right_circular_a(0x05B7);
    z.xor_register(0x05B8,r.h);
    z.rotate_right_circular_a(0x05B9);
    z.xor_register(0x05BA,r.l);
    for (std::uint16_t at=0x05BB;at<0x05BF;++at) z.rotate_right_circular_a(at);
    z.xor_register(0x05BF,r.l);
    z.rotate_right_a(0x05C0);
    z.add_hl_with_carry(0x05C1,z.hl());
    // The recovery: the fixed state is the operand of the load at 00:05C5.
    if (!z.relative_if(0x05C3,k::Condition::nz)) z.load_hl(0x05C5);
    z.load_a_from_refresh(0x05C8);
    z.xor_register(0x05CA,r.l);
    z.store_hl_direct(0x05CB);
    z.pop(0x05CE,r.h,r.l);
    return z.final_return(return_address);
}
}
void register_random(RoutineRegistry& registry) {
    registry.add_exact(random_entry,SegmentSpec{random_entry,{{RoutineExit::returned,return_address}},false,2},
        [](const Machine& machine) -> std::uint64_t {
            k::Estimate bus(machine);
            auto registers=k::load(machine.cpu());
            k::Instructions<k::Estimate> z(bus,registers);
            (void)advance_random_state(z);
            return bus.uncertain ? recovery_cycles : bus.cycles();
        },
        [](RoutineContext& context) {
            k::Live bus(context);
            auto registers=k::load(context.cpu());
            k::Instructions<k::Live> z(bus,registers);
            k::Exit exit{};
            {
                k::Sync<k::Registers,&k::store,&k::load> sync(registers);
                RoutineContext::SyncScope scope(context,sync);
                exit=advance_random_state(z);
            }
            k::store(context.cpu(),registers);
            k::finish(context,exit);
        });
}
}
