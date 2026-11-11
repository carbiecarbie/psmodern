// SPDX-License-Identifier: GPL-3.0-or-later
// The game's random-number routine (00:05B1), rewritten as one segment that
// ends with its original return executed by the core.
#include "game/routines.h"
#include "machine/machine.h"
#include <bit>

namespace psmodern::game {
namespace {
constexpr std::uint8_t carry_flag=0x01, parity_flag=0x04, undocumented_flags=0x28, half_flag=0x10,
    zero_flag=0x40, sign_flag=0x80;
constexpr std::uint16_t return_address=0x05CF;
// Cycles including the final return: 151 when the new state is nonzero; the
// recovery path skips no branch (7 instead of 12 cycles) and loads a fixed
// state (10 cycles), five more in all.
constexpr std::uint64_t normal_cycles=151, recovery_cycles=normal_cycles+5;

struct Registers { std::uint8_t a, f; std::uint16_t hl; };

// Sign, zero, parity and the two undocumented bits of a logical result.
std::uint8_t logic_flags(std::uint8_t value) {
    return static_cast<std::uint8_t>((value&(sign_flag|undocumented_flags))|(value ? 0 : zero_flag)|
        (std::popcount(value)%2 ? 0 : parity_flag));
}
// Rotations keep sign, zero and parity, clear the half-carry and subtract
// flags, copy the undocumented bits of the result and put the bit shifted out
// in carry. The circular form re-enters that bit at the top; the other form
// enters the previous carry.
void rotate(Registers& r, bool circular) {
    const auto out=static_cast<std::uint8_t>(r.a&1);
    const auto top=circular ? out : static_cast<std::uint8_t>(r.f&carry_flag);
    r.a=static_cast<std::uint8_t>((r.a>>1)|(top<<7));
    r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(r.a&undocumented_flags)|out);
}
void exclusive_or(Registers& r, std::uint8_t value) { r.a^=value; r.f=logic_flags(r.a); }

// 00:05B5-05C2: mix the state bytes into one bit, then double the state and
// add that bit (a 16-bit add with carry of the pair to itself).
void mix(Registers& r) {
    const auto high=static_cast<std::uint8_t>(r.hl>>8), low=static_cast<std::uint8_t>(r.hl);
    r.a=high;
    rotate(r,true); rotate(r,true);
    exclusive_or(r,high);
    rotate(r,true);
    exclusive_or(r,low);
    for (unsigned i=0;i<4;++i) rotate(r,true);
    exclusive_or(r,low);
    rotate(r,false); // the carry entering here is the one the last mix cleared
    const unsigned before=r.hl, sum=before+before+(r.f&carry_flag);
    const auto result=static_cast<std::uint16_t>(sum);
    // Both operands are equal, so the half carry out of bit 11 is bit 12 of the
    // sum, and signed overflow is a sign change.
    r.f=static_cast<std::uint8_t>(((result>>8)&(sign_flag|undocumented_flags))|(result ? 0 : zero_flag)|
        ((sum>>8)&half_flag)|(((before^result)&0x8000) ? parity_flag : 0)|((sum>>16)&1));
    r.hl=result;
}

std::uint16_t peek_word(const Machine& machine, std::uint16_t address) {
    return static_cast<std::uint16_t>(machine.peek(address)|
        (machine.peek(static_cast<std::uint16_t>(address+1))<<8));
}
std::uint16_t read_word(RoutineContext& context, std::uint16_t address) {
    const auto low=context.read(address);
    const auto high=context.read(static_cast<std::uint16_t>(address+1));
    return static_cast<std::uint16_t>(low|(high<<8));
}

// The exact cost of the path this machine will take, which depends on the
// state as the routine will read it: after the two scratch stack writes,
// which land on the state if the stack points at it, and as the open bus if
// work RAM is disabled. Memory is copied so that nothing here has an effect.
std::uint64_t random_cycles(const Machine& machine) {
    const auto& cpu=machine.cpu();
    Memory memory=machine.memory();
    memory.write(static_cast<std::uint16_t>(cpu.sp.uint16_value-1),cpu.hl.uint8_values.at_1);
    memory.write(static_cast<std::uint16_t>(cpu.sp.uint16_value-2),cpu.hl.uint8_values.at_0);
    const auto address=peek_word(machine,0x05B3);
    auto byte=[&](std::uint16_t at, std::uint8_t bus) {
        return memory.mapping(at).kind==Memory::Kind::unmapped ? bus : memory.read(at);
    };
    // An unmapped read returns the last byte on the bus: the operand's high
    // byte, or the state's low byte when that one was mapped.
    const auto low=byte(address,machine.peek(0x05B4));
    const auto high=byte(static_cast<std::uint16_t>(address+1),low);
    Registers r{0,0,static_cast<std::uint16_t>(low|(high<<8))};
    mix(r);
    return r.hl ? normal_cycles : recovery_cycles;
}

void advance_random_state(RoutineContext& context) {
    auto& cpu=context.cpu();
    Registers r{cpu.af.uint8_values.at_1,cpu.af.uint8_values.at_0,cpu.hl.uint16_value};
    // Save the caller's pair below the stack pointer through the bus, high byte
    // first. SP itself stays at its entry value for the native return.
    const auto saved_high=static_cast<std::uint16_t>(cpu.sp.uint16_value-1);
    const auto saved_low=static_cast<std::uint16_t>(cpu.sp.uint16_value-2);
    context.fetch_opcode(0x05B1); context.advance(5);
    context.write(saved_high,static_cast<std::uint8_t>(r.hl>>8)); context.advance(3);
    context.write(saved_low,static_cast<std::uint8_t>(r.hl)); context.advance(3);

    // Load the two-byte state; its address is the instruction's operand.
    context.fetch_opcode(0x05B2);
    const auto state_address=read_word(context,0x05B3);
    r.hl=read_word(context,state_address);
    context.advance(16);

    // Twelve one-byte instructions of four cycles, then the two-byte add.
    for (std::uint16_t address=0x05B5;address<=0x05C0;++address) { context.fetch_opcode(address); context.advance(4); }
    context.fetch_opcode(0x05C1); context.fetch_opcode(0x05C2); context.advance(15);
    mix(r);

    // A zero state would stay zero forever: replace it with a fixed one, the
    // operand of the instruction at 00:05C5. The branch reads its offset
    // either way.
    context.fetch_opcode(0x05C3); (void)context.read(0x05C4);
    if (r.f&zero_flag) {
        context.advance(7);
        context.fetch_opcode(0x05C5);
        r.hl=read_word(context,0x05C6);
        context.advance(10);
    } else context.advance(12);

    // 00:05C8: read the refresh counter. Its low seven bits have counted every
    // opcode fetch so far, including both of this instruction's; bit 7 is
    // the caller's. Parity/overflow receives the interrupt flip-flop IFF2.
    context.fetch_opcode(0x05C8); context.fetch_opcode(0x05C9); context.advance(9);
    r.a=cpu.r;
    r.f=static_cast<std::uint8_t>((r.a&(sign_flag|undocumented_flags))|(r.a ? 0 : zero_flag)|
        (cpu.iff2 ? parity_flag : 0)|(r.f&carry_flag));
    // The draw is that counter mixed with the new state's low byte; the
    // counter never enters the stored state.
    context.fetch_opcode(0x05CA); context.advance(4);
    exclusive_or(r,static_cast<std::uint8_t>(r.hl));

    // Store the new state, low byte first, at the operand's address.
    context.fetch_opcode(0x05CB);
    const auto store_address=read_word(context,0x05CC);
    context.write(store_address,static_cast<std::uint8_t>(r.hl));
    context.write(static_cast<std::uint16_t>(store_address+1),static_cast<std::uint8_t>(r.hl>>8));
    context.advance(16);

    // Restore the caller's pair through the bus: whatever the stack bytes hold
    // now, as natively (a stack in ROM, or on the state itself, reads back
    // something else).
    context.fetch_opcode(0x05CE);
    const auto low=context.read(saved_low), high=context.read(saved_high);
    context.advance(10);
    cpu.af.uint8_values.at_1=r.a;
    cpu.af.uint8_values.at_0=r.f;
    cpu.hl.uint16_value=static_cast<std::uint16_t>(low|(high<<8));
    context.finish_return(return_address);
}
}
void register_random(RoutineRegistry& registry) {
    registry.add(random_entry,SegmentSpec{random_entry,{{RoutineExit::returned,return_address}},false,2},
        random_cycles,advance_random_state);
}
}
