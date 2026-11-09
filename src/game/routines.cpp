// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/routines.h"
#include "rom/rom.h"
#include <bit>
#include <stdexcept>

namespace psmodern::game {
namespace {
void poll_controller(RoutineContext& context) {
    auto& cpu=context.cpu();

    // Read the active-low controller port at the original T3 offset. All ROM
    // operands still come from the player's image, in their original bus order.
    context.fetch_opcode(0x0339);
    const auto port=context.read(0x033a);
    const auto current=static_cast<std::uint8_t>(~context.read_port(
        (cpu.af.uint8_values.at_1<<8)|port,10));
    context.advance(1);
    context.fetch_opcode(0x033b);
    const auto low=context.read(0x033c), high=context.read(0x033d);
    const auto held_address=static_cast<std::uint16_t>(low|(high<<8));
    context.advance(10);

    context.fetch_opcode(0x033e); context.advance(4);
    context.fetch_opcode(0x033f); context.advance(4);
    context.fetch_opcode(0x0340);
    const auto previous=context.read(held_address);
    context.advance(7);
    context.fetch_opcode(0x0341); context.write(held_address,current); context.advance(7);
    context.fetch_opcode(0x0342); context.advance(6);

    // A held button is recorded every poll; an edge is recorded only once.
    const auto pressed=static_cast<std::uint8_t>(current & ~previous);
    context.fetch_opcode(0x0343); context.advance(4);
    context.fetch_opcode(0x0344);
    context.write(static_cast<std::uint16_t>(held_address+1),pressed);
    context.advance(7);
    cpu.bc.uint8_values.at_1=current;
    cpu.hl.uint16_value=static_cast<std::uint16_t>(held_address+1);
    cpu.af.uint8_values.at_1=pressed;
    cpu.af.uint8_values.at_0=static_cast<std::uint8_t>((pressed&0xa8)|0x10|
        (pressed==0 ? 0x40 : 0)|((std::popcount(pressed)&1)==0 ? 0x04 : 0));
    // The native return restores PC, SP, MEMPTR, Q and core bookkeeping. The
    // fixed 70-cycle bound includes these final ten cycles and the tenth fetch.
    context.finish_return(0x0345);
}
}
bool register_routines(RoutineRegistry& registry, std::string_view digest) {
    try { (void)identify_digest(digest); }
    catch (const std::runtime_error&) { return false; }
    registry.add(controller_poll_entry,[](const Machine&) { return 70; },poll_controller);
    return true;
}
}
