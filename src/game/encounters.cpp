// SPDX-License-Identifier: GPL-3.0-or-later
// The random-encounter checks, rewritten as segments (milestone 4, part C):
// the map check (01:5F63) with its shared monster-group choice (01:5FD8) and
// failure ending, and the dungeon game-mode handler (00:0F3C). Every segment
// renders the original instructions from its entry for any machine state and
// ends with one instruction executed by the core: a return, or a call or jump
// into original code (which runs natively, including the random-number
// routine, which is a segment of its own).
//
// Each segment is written once, as a sequence of instruction renderings over a
// `Bus`. Two buses run it: the live one performs the accesses through the
// routine context, and an estimating one performs the same sequence on a copy
// of the machine's memory to learn the exact cost of the path this machine will
// take, which is the segment's bound.
#include "game/encounters.h"
#include "machine/machine.h"
#include <algorithm>
#include <bit>
#include <utility>

namespace psmodern::game {
namespace {
constexpr std::uint8_t carry_flag=0x01, subtract_flag=0x02, parity_flag=0x04, x_flag=0x08, half_flag=0x10,
    y_flag=0x20, zero_flag=0x40, sign_flag=0x80;

struct Registers { std::uint8_t a, f, b, c, d, e, h, l; };
Registers load(const Z80& cpu) {
    return {cpu.af.uint8_values.at_1,cpu.af.uint8_values.at_0,cpu.bc.uint8_values.at_1,cpu.bc.uint8_values.at_0,
        cpu.de.uint8_values.at_1,cpu.de.uint8_values.at_0,cpu.hl.uint8_values.at_1,cpu.hl.uint8_values.at_0};
}
void store(Z80& cpu, const Registers& r) {
    cpu.af.uint8_values.at_1=r.a; cpu.af.uint8_values.at_0=r.f;
    cpu.bc.uint8_values.at_1=r.b; cpu.bc.uint8_values.at_0=r.c;
    cpu.de.uint8_values.at_1=r.d; cpu.de.uint8_values.at_0=r.e;
    cpu.hl.uint8_values.at_1=r.h; cpu.hl.uint8_values.at_0=r.l;
}
std::uint16_t pair(std::uint8_t high, std::uint8_t low) { return static_cast<std::uint16_t>((high<<8)|low); }

// The last instruction of a segment, executed by the core, and the cycles it
// takes on the path the segment found.
struct Exit { SegmentExit segment; unsigned cycles; };
Exit returns(std::uint16_t address, unsigned cycles) { return {{RoutineExit::returned,address},cycles}; }
Exit continues(std::uint16_t address, unsigned cycles) { return {{RoutineExit::core_final,address},cycles}; }
constexpr unsigned call_cycles=17, untaken_call_cycles=10, return_cycles=10, taken_conditional_return_cycles=11;

// Flags of the arithmetic and logic instructions the checks use.
std::uint8_t sign_zero_xy(std::uint8_t value) {
    return static_cast<std::uint8_t>((value&(sign_flag|y_flag|x_flag))|(value ? 0 : zero_flag));
}
std::uint8_t parity(std::uint8_t value) { return std::popcount(value)%2 ? 0 : parity_flag; }
void add8(Registers& r, std::uint8_t operand, unsigned carry) {
    const unsigned sum=r.a+operand+carry;
    const auto result=static_cast<std::uint8_t>(sum);
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|((r.a^operand^result)&half_flag)|
        ((~(r.a^operand)&(r.a^result)&0x80) ? parity_flag : 0)|(sum>0xff ? carry_flag : 0));
    r.a=result;
}
void compare8(Registers& r, std::uint8_t operand) {
    const auto result=static_cast<std::uint8_t>(r.a-operand);
    r.f=static_cast<std::uint8_t>((result&sign_flag)|(result ? 0 : zero_flag)|(operand&(y_flag|x_flag))|
        ((r.a^operand^result)&half_flag)|(((r.a^operand)&(r.a^result)&0x80) ? parity_flag : 0)|
        subtract_flag|(r.a<operand ? carry_flag : 0));
}
void logic_flags(Registers& r, bool and_operation) {
    r.f=static_cast<std::uint8_t>(sign_zero_xy(r.a)|parity(r.a)|(and_operation ? half_flag : 0));
}
// Shift or rotate right of one byte; the bit shifted out goes to carry and the
// bit shifted in is given.
std::uint8_t shift_right(Registers& r, std::uint8_t value, unsigned entering) {
    const auto result=static_cast<std::uint8_t>((value>>1)|(entering<<7));
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|parity(result)|(value&1));
    return result;
}
// 16-bit addition into HL: sign, zero and parity stay, the rest comes from the
// sum (undocumented bits and half carry from the high byte).
void add_hl(Registers& r, std::uint16_t operand) {
    const unsigned hl=pair(r.h,r.l), sum=hl+operand;
    r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|((sum>>8)&(y_flag|x_flag))|
        (((hl^operand^sum)>>8)&half_flag)|((sum>>16)&1));
    r.h=static_cast<std::uint8_t>(sum>>8); r.l=static_cast<std::uint8_t>(sum);
}

// One bus per way of running a segment: fetch (opcode read and refresh), read,
// write, and time.
class Live {
public:
    explicit Live(RoutineContext& context) : context_(context) {}
    void fetch(std::uint16_t address) { (void)context_.fetch_opcode(address); }
    std::uint8_t read(std::uint16_t address) { return context_.read(address); }
    void write(std::uint16_t address, std::uint8_t value) { context_.write(address,value); }
    void advance(std::uint64_t cycles) { context_.advance(cycles); }
private:
    RoutineContext& context_;
};
// The same accesses on a copy of memory, counting cycles. A read of an
// unmapped address returns the bus value the real read would see only by
// accident, so the estimate is then not exact (the caller uses its maximum).
class Estimate {
public:
    explicit Estimate(const Machine& machine) : machine_(machine), memory_(machine.memory()) {}
    void fetch(std::uint16_t) {}
    std::uint8_t read(std::uint16_t address) {
        if (memory_.mapping(address).kind==Memory::Kind::unmapped) { uncertain=true; return machine_.peek(address); }
        return memory_.read(address);
    }
    void write(std::uint16_t address, std::uint8_t value) { memory_.write(address,value); }
    void advance(std::uint64_t cycles) { cycles_+=cycles; }
    std::uint64_t cycles() const { return cycles_; }
    bool uncertain=false;
private:
    const Machine& machine_;
    Memory memory_;
    std::uint64_t cycles_=0;
};

// The original instructions the checks contain, each given the address it has
// in the ROM. Opcode fetches, operand reads and data accesses follow the bus
// order of the Z80; operands (addresses, constants, the bank number) are read
// from the image, never written here.
template<class Bus> class Instructions {
public:
    Instructions(Bus& bus, Registers& registers) : r(registers), bus_(bus) {}
    Registers& r;

    std::uint16_t hl() const { return pair(r.h,r.l); }
    std::uint16_t de() const { return pair(r.d,r.e); }
    std::uint16_t bc() const { return pair(r.b,r.c); }

    void one_byte(std::uint16_t at, unsigned cycles) { bus_.fetch(at); bus_.advance(cycles); }
    void load_hl(std::uint16_t at) { load_pair(at,r.h,r.l); }
    void load_de(std::uint16_t at) { load_pair(at,r.d,r.e); }
    void store_to_hl(std::uint16_t at) { // ld (hl),n
        bus_.fetch(at); const auto value=operand(at+1); bus_.write(hl(),value); bus_.advance(10);
    }
    void load_immediate(std::uint16_t at, std::uint8_t& to) { bus_.fetch(at); to=operand(at+1); bus_.advance(7); }
    void copy(std::uint16_t at, std::uint8_t& to, std::uint8_t from) { one_byte(at,4); to=from; }
    void load_from_hl(std::uint16_t at, std::uint8_t& to) { bus_.fetch(at); to=bus_.read(hl()); bus_.advance(7); }
    void load_a_direct(std::uint16_t at) { // ld a,(nn)
        bus_.fetch(at); const auto address=operand_word(at+1); r.a=bus_.read(address); bus_.advance(13);
    }
    void store_a_direct(std::uint16_t at) { // ld (nn),a
        bus_.fetch(at); const auto address=operand_word(at+1); bus_.write(address,r.a); bus_.advance(13);
    }
    void load_de_direct(std::uint16_t at) { // ld de,(nn): two opcode bytes, low byte read first
        bus_.fetch(at); bus_.fetch(at+1);
        const auto address=operand_word(at+2);
        r.e=bus_.read(address); r.d=bus_.read(static_cast<std::uint16_t>(address+1));
        bus_.advance(20);
    }
    void increment_hl(std::uint16_t at) { one_byte(at,6); const auto v=static_cast<std::uint16_t>(hl()+1); r.h=v>>8; r.l=static_cast<std::uint8_t>(v); }
    void compare_immediate(std::uint16_t at) { bus_.fetch(at); compare8(r,operand(at+1)); bus_.advance(7); }
    void add_immediate(std::uint16_t at) { bus_.fetch(at); add8(r,operand(at+1),0); bus_.advance(7); }
    void and_immediate(std::uint16_t at) { bus_.fetch(at); r.a&=operand(at+1); logic_flags(r,true); bus_.advance(7); }
    void add_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); add8(r,value,0); }
    void add_with_carry(std::uint16_t at, std::uint8_t value) { one_byte(at,4); add8(r,value,r.f&carry_flag); }
    void add_from_hl(std::uint16_t at) { bus_.fetch(at); const auto v=bus_.read(hl()); add8(r,v,0); bus_.advance(7); }
    void or_a(std::uint16_t at) { one_byte(at,4); logic_flags(r,false); }
    void xor_a(std::uint16_t at) { one_byte(at,4); r.a=0; logic_flags(r,false); }
    void shift_right_logical(std::uint16_t at, std::uint8_t& value) { // srl r
        bus_.fetch(at); bus_.fetch(at+1); value=shift_right(r,value,0); bus_.advance(8);
    }
    void rotate_right_through_carry(std::uint16_t at, std::uint8_t& value) { // rr r
        bus_.fetch(at); bus_.fetch(at+1); value=shift_right(r,value,r.f&carry_flag); bus_.advance(8);
    }
    void add_to_hl(std::uint16_t at, std::uint16_t operand) { one_byte(at,11); add_hl(r,operand); }
    // The carry is complemented and the half carry takes the old carry. The
    // undocumented bits are not rendered: the add-with-carry that follows
    // rewrites every flag before anything can observe them.
    void complement_carry(std::uint16_t at) {
        one_byte(at,4);
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|((r.f&carry_flag) ? half_flag : 0)|
            ((r.f&carry_flag) ? 0 : carry_flag));
    }
    // Conditional jumps taken inside the segment; the operand is read either
    // way. They report whether the jump was taken.
    bool jump_if_no_carry(std::uint16_t at) { // jp nc,nn
        bus_.fetch(at); (void)operand_word(at+1); bus_.advance(10); return !(r.f&carry_flag);
    }
    bool relative_if_carry(std::uint16_t at) { return relative(at,r.f&carry_flag); }
    bool relative_if_zero(std::uint16_t at) { return relative(at,r.f&zero_flag); }
    // A conditional return that is not taken (the taken one ends the segment
    // and is executed by the core).
    void return_not_taken(std::uint16_t at) { one_byte(at,5); }

private:
    std::uint8_t operand(unsigned at) { return bus_.read(static_cast<std::uint16_t>(at)); }
    std::uint16_t operand_word(unsigned at) {
        const auto low=operand(at), high=operand(at+1);
        return pair(high,low);
    }
    void load_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {
        bus_.fetch(at); const auto value=operand_word(at+1);
        high=static_cast<std::uint8_t>(value>>8); low=static_cast<std::uint8_t>(value); bus_.advance(10);
    }
    bool relative(std::uint16_t at, bool taken) { bus_.fetch(at); (void)operand(at+1); bus_.advance(taken ? 12 : 7); return taken; }
    Bus& bus_;
};

// ---- Map check -------------------------------------------------------------

// 01:5FF9-5FFD, the failure ending: no battle (A = 0) and the pending flag
// cleared.
template<class Bus> Exit map_failure(Instructions<Bus>& z) {
    z.xor_a(0x5FF9);
    z.store_a_direct(0x5FFA);
    return returns(0x5FFD,return_cycles);
}

// 01:5F63-5FAD: select the region record from the map selector and the
// position, reduce the threshold when the vehicle modifier is set, then call
// the random-number routine.
template<class Bus> Exit map_check_start(Instructions<Bus>& z) {
    auto& r=z.r;
    z.load_hl(0x5F63);
    z.store_to_hl(0x5F66);                 // slot 2 bank register
    z.load_a_direct(0x5F68);               // selector
    z.compare_immediate(0x5F6B);
    if (z.jump_if_no_carry(0x5F6D)) return map_failure(z);
    z.copy(0x5F70,r.h,r.a);
    z.load_immediate(0x5F71,r.l);
    z.shift_right_logical(0x5F73,r.h);
    z.rotate_right_through_carry(0x5F75,r.l);
    z.load_de(0x5F77);
    z.add_to_hl(0x5F7A,z.de());
    z.load_de_direct(0x5F7B);
    z.copy(0x5F7F,r.a,r.e);
    z.add_immediate(0x5F80);
    if (!z.relative_if_carry(0x5F82)) {
        z.compare_immediate(0x5F84);
        z.complement_carry(0x5F86);
    }
    z.load_immediate(0x5F87,r.a);
    z.add_with_carry(0x5F89,r.d);
    z.and_immediate(0x5F8A);
    z.add_register(0x5F8C,r.a); z.add_register(0x5F8D,r.a); z.add_register(0x5F8E,r.a);
    z.copy(0x5F8F,r.c,r.a);
    z.load_de_direct(0x5F90);
    z.copy(0x5F94,r.a,r.e);
    z.add_immediate(0x5F95);
    z.load_immediate(0x5F97,r.a);
    z.add_with_carry(0x5F99,r.d);
    z.and_immediate(0x5F9A);
    z.add_register(0x5F9C,r.c);
    z.add_register(0x5F9D,r.a);
    z.load_immediate(0x5F9E,r.d);
    z.copy(0x5FA0,r.e,r.a);
    z.add_to_hl(0x5FA1,z.de());
    z.load_from_hl(0x5FA2,r.b);            // the region's threshold
    z.load_a_direct(0x5FA3);               // vehicle modifier
    z.or_a(0x5FA6);
    if (!z.relative_if_zero(0x5FA7)) {
        z.shift_right_logical(0x5FA9,r.b);
        z.shift_right_logical(0x5FAB,r.b);
    }
    return continues(0x5FAD,call_cycles);  // call the random-number routine
}

// 01:5FB1-5FD7 after the comparison, then the group choice below.
template<class Bus> Exit group_choice(Instructions<Bus>& z);
template<class Bus> Exit map_pick_pool(Instructions<Bus>& z) {
    auto& r=z.r;
    if (z.jump_if_no_carry(0x5FB1)) return map_failure(z);
    z.increment_hl(0x5FB4);
    z.load_immediate(0x5FB5,r.b);
    z.load_from_hl(0x5FB7,r.c);
    z.load_a_direct(0x5FB8);
    z.copy(0x5FBB,r.l,r.a);
    z.load_immediate(0x5FBC,r.h);
    z.load_de(0x5FBE);
    z.add_to_hl(0x5FC1,z.de());
    z.load_a_direct(0x5FC2);
    z.or_a(0x5FC5);
    if (!z.relative_if_zero(0x5FC6)) z.load_immediate(0x5FC8,r.a);
    z.add_from_hl(0x5FCA);
    z.copy(0x5FCB,r.l,r.a);
    z.load_immediate(0x5FCC,r.h);
    for (std::uint16_t at=0x5FCE;at<=0x5FD1;++at) z.add_to_hl(at,z.hl());
    z.load_de(0x5FD2);
    z.add_to_hl(0x5FD5,z.de());
    z.add_to_hl(0x5FD6,z.bc());
    z.load_from_hl(0x5FD7,r.a);
    return group_choice(z);
}

// ---- Monster-group choice 01:5FD8-5FE9 (also called by the dungeon) --------

template<class Bus> Exit group_choice(Instructions<Bus>& z) {
    auto& r=z.r;
    z.or_a(0x5FD8);
    if (r.f&zero_flag) return returns(0x5FD9,taken_conditional_return_cycles); // no pool
    z.return_not_taken(0x5FD9);
    z.load_hl(0x5FDA);
    z.store_to_hl(0x5FDD);                 // slot 2 bank register
    z.copy(0x5FDF,r.l,r.a);
    z.load_immediate(0x5FE0,r.h);
    for (std::uint16_t at=0x5FE2;at<=0x5FE4;++at) z.add_to_hl(at,z.hl());
    z.load_de(0x5FE5);
    z.add_to_hl(0x5FE8,z.de());
    return continues(0x5FE9,call_cycles);  // call the random-number routine
}
// 01:5FEC-5FF8: the draw picks one of eight pool entries.
template<class Bus> Exit group_pick(Instructions<Bus>& z) {
    auto& r=z.r;
    z.and_immediate(0x5FEC);
    z.copy(0x5FEE,r.e,r.a);
    z.load_immediate(0x5FEF,r.d);
    z.add_to_hl(0x5FF1,z.de());
    z.load_from_hl(0x5FF2,r.a);
    z.store_a_direct(0x5FF3);
    z.load_immediate(0x5FF6,r.a);
    return returns(0x5FF8,return_cycles);
}

// ---- Dungeon game-mode handler ---------------------------------------------

template<class Bus> Exit dungeon_pause(Instructions<Bus>& z) {   // 00:0F3C
    z.load_a_direct(0x0F3C);
    z.or_a(0x0F3F);
    return continues(0x0F40,(z.r.f&zero_flag) ? untaken_call_cycles : call_cycles);
}
template<class Bus> Exit dungeon_wait(Instructions<Bus>& z) {    // 00:0F43
    z.load_immediate(0x0F43,z.r.a);
    return continues(0x0F45,call_cycles);
}
template<class Bus> Exit dungeon_draw(Instructions<Bus>& z) {    // 00:0F4B
    auto& r=z.r;
    z.load_a_direct(0x0F4B);
    z.or_a(0x0F4E);
    if (r.f&zero_flag) return returns(0x0F4F,taken_conditional_return_cycles);
    z.return_not_taken(0x0F4F);
    z.xor_a(0x0F50);
    z.store_a_direct(0x0F51);
    z.load_a_direct(0x0F54);
    z.copy(0x0F57,r.b,r.a);
    return continues(0x0F58,call_cycles);
}
template<class Bus> Exit dungeon_after_compare(Instructions<Bus>& z) { // 00:0F5C
    if (!(z.r.f&carry_flag)) return returns(0x0F5C,taken_conditional_return_cycles);
    z.return_not_taken(0x0F5C);
    z.load_immediate(0x0F5D,z.r.b);
    return continues(0x0F5F,call_cycles);
}
template<class Bus> Exit dungeon_after_cell_test(Instructions<Bus>& z) { // 00:0F62
    auto& r=z.r;
    if (!(r.f&zero_flag)) return returns(0x0F62,taken_conditional_return_cycles);
    z.return_not_taken(0x0F62);
    z.xor_a(0x0F63);
    z.store_a_direct(0x0F64);
    z.load_a_direct(0x0F67);
    return continues(0x0F6A,call_cycles);
}
template<class Bus> Exit dungeon_after_group(Instructions<Bus>& z) { // 00:0F6D
    auto& r=z.r;
    z.or_a(0x0F6D);
    if (r.f&zero_flag) return returns(0x0F6E,taken_conditional_return_cycles);
    z.return_not_taken(0x0F6E);
    return continues(0x0F6F,call_cycles);
}
template<class Bus> Exit dungeon_after_setup(Instructions<Bus>& z) { // 00:0F75
    z.load_a_direct(0x0F75);
    z.or_a(0x0F78);
    return continues(0x0F79,(z.r.f&zero_flag) ? untaken_call_cycles : call_cycles);
}

// ---- Registration ----------------------------------------------------------

// The exact cost of the path this machine will take: the segment run on a copy
// of memory. When it read an unmapped address the path is not known, and the
// longest path of the segment is used instead.
template<class Logic> std::uint64_t cost(const Machine& machine, const Logic& logic, std::uint64_t maximum) {
    Estimate bus(machine);
    auto registers=load(machine.cpu());
    Instructions<Estimate> instructions(bus,registers);
    const auto exit=logic(instructions);
    return bus.uncertain ? maximum : bus.cycles()+exit.cycles;
}
template<class Logic> void run(RoutineContext& context, const Logic& logic) {
    auto& cpu=context.cpu();
    Live bus(context);
    auto registers=load(cpu);
    Instructions<Live> instructions(bus,registers);
    const auto exit=logic(instructions);
    store(cpu,registers);
    if (exit.segment.kind==RoutineExit::returned) context.finish_return(exit.segment.address);
    else context.finish_core(exit.segment.address);
}
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit,
                                       std::vector<SegmentExit> exits, bool slot2_mapper, std::uint64_t maximum, Logic logic) {
    registry.add(entry,SegmentSpec{unit,std::move(exits),slot2_mapper},
        [logic,maximum](const Machine& machine) { return cost(machine,logic,maximum); },
        [logic](RoutineContext& context) { run(context,logic); });
}

// The comparison of the draw with the threshold, in the register B. With the
// option at its default the segment is the original instruction, executed by
// the core. Otherwise the register holds the scaled threshold for that one
// instruction and its original value afterwards; admission is forced, as the
// segment adds no time of its own.
void add_compare(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, unsigned percent) {
    const bool scaled=percent!=encounter_percent_choices.original;
    SegmentSpec spec{unit,{{RoutineExit::core_final,entry.address}},false,0,true,scaled};
    registry.add(entry,std::move(spec),[](const Machine&) { return 4; },
        [entry,percent,scaled](RoutineContext& context) {
            if (!scaled) { context.finish_core(entry.address); return; }
            auto& b=context.cpu().bc.uint8_values.at_1;
            const auto original=b;
            b=static_cast<std::uint8_t>(std::min<unsigned long>(255,static_cast<unsigned long>(original)*percent/100));
            context.finish_core(entry.address);
            context.cpu().bc.uint8_values.at_1=original;
        });
}
}

void register_encounters(RoutineRegistry& registry, const Options& options) {
    using E=SegmentExit;
    const auto ret=RoutineExit::returned, fin=RoutineExit::core_final;
    // Map check unit. The maxima apply only if the path cannot be known (an
    // unmapped read) and are the cost of the longest path of each segment.
    add_segment(registry,map_check_unit,map_check_unit,{E{ret,0x5FFD},E{fin,0x5FAD}},true,324,
        [](auto& z) { return map_check_start(z); });
    add_compare(registry,map_compare_entry,map_check_unit,options.encounter_percent);
    add_segment(registry,map_after_compare_entry,map_check_unit,{E{ret,0x5FFD},E{ret,0x5FD9},E{fin,0x5FE9}},true,318,
        [](auto& z) { return map_pick_pool(z); });
    add_segment(registry,group_choice_unit,group_choice_unit,{E{ret,0x5FD9},E{fin,0x5FE9}},true,111,
        [](auto& z) { return group_choice(z); });
    add_segment(registry,group_choice_after_draw_entry,group_choice_unit,{E{ret,0x5FF8}},false,66,
        [](auto& z) { return group_pick(z); });
    // Dungeon unit. The three bare instructions (00:0F48, 0F72, 0F7C) stay native.
    add_segment(registry,dungeon_check_unit,dungeon_check_unit,{E{fin,0x0F40}},false,34,
        [](auto& z) { return dungeon_pause(z); });
    add_segment(registry,{0,0x0F43},dungeon_check_unit,{E{fin,0x0F45}},false,24,
        [](auto& z) { return dungeon_wait(z); });
    add_segment(registry,{0,0x0F4B},dungeon_check_unit,{E{ret,0x0F4F},E{fin,0x0F58}},false,73,
        [](auto& z) { return dungeon_draw(z); });
    add_compare(registry,dungeon_compare_entry,dungeon_check_unit,options.encounter_percent);
    add_segment(registry,{0,0x0F5C},dungeon_check_unit,{E{ret,0x0F5C},E{fin,0x0F5F}},false,29,
        [](auto& z) { return dungeon_after_compare(z); });
    add_segment(registry,{0,0x0F62},dungeon_check_unit,{E{ret,0x0F62},E{fin,0x0F6A}},false,52,
        [](auto& z) { return dungeon_after_cell_test(z); });
    add_segment(registry,{0,0x0F6D},dungeon_check_unit,{E{ret,0x0F6E},E{fin,0x0F6F}},false,26,
        [](auto& z) { return dungeon_after_group(z); });
    add_segment(registry,{0,0x0F75},dungeon_check_unit,{E{fin,0x0F79}},false,34,
        [](auto& z) { return dungeon_after_setup(z); });
}
}
