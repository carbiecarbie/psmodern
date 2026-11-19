// SPDX-License-Identifier: GPL-3.0-or-later
// Test scaffolding of milestone 14 for the authored programs of the mechanism
// tests. The program has no processor core: an address where no segment
// begins stops it. These tests run small authored programs whose callers,
// idle loops and handlers they do not render; oracle::Scaffold (one scope,
// usually a test's main) lets the pinned core of the test oracle run those
// instructions on the engine's register file (Machine::set_scaffold_step).
// The game's probes and checkpoint runs require that no scaffolding step ran.
//
// A body written before milestone 14 may still leave its final instruction to
// the core: it ends at the declared exit and the machine continues at the
// final's own address, where no segment begins, so the scaffolding runs it.
#pragma once
#include "game/dungeon_kit.h"
#include "machine/routines.h"
#include "oracle/native_machine.h"

namespace psmodern::scaffold {
inline void native_final(RoutineContext& context, RoutineExit kind, std::uint16_t address) {
    context.finish({kind,address},address);
}
inline void native_return(RoutineContext& context, std::uint16_t address) { native_final(context,RoutineExit::returned,address); }
inline void native_core_final(RoutineContext& context, std::uint16_t address) { native_final(context,RoutineExit::core_final,address); }
// The kit form. `cycles` (the final's cost on the path the body found) is not
// part of the call any more: the final is a step of its own on the
// scaffolding, so the segment's bound is the body alone.
inline game::kit::Exit returns(std::uint16_t address, unsigned /*cycles*/) {
    return {{RoutineExit::returned,address},0,address};
}
inline game::kit::Exit continues(std::uint16_t address, unsigned /*cycles*/) {
    return {{RoutineExit::core_final,address},0,address};
}
// Conditional finals left to the core: JP cc,nn (10 either way), JR cc,e (12
// taken, 7 not), DJNZ (13 taken, 8 not).
inline game::kit::Exit jump_if_exit(std::uint16_t address) { return continues(address,10); }
inline game::kit::Exit relative_if_exit(std::uint16_t address, bool taken) { return continues(address,taken ? 12 : 7); }
template<class Z> game::kit::Exit decrement_jump_exit(const Z& z, std::uint16_t address) {
    return continues(address,z.r.b!=1 ? 13 : 8);
}
}
