// SPDX-License-Identifier: GPL-3.0-or-later
// Shared rendering kit of the milestone 7 dungeon segments (dungeon_cells.cpp,
// dungeon_movement.cpp, dungeon_objects.cpp). It follows the pattern each
// milestone 4-6 file keeps privately (encounters.cpp): a segment is written
// once as a sequence of original instructions over a `Bus`; the live bus
// performs the accesses through the routine context, the estimating bus runs
// the same sequence without touching the machine to learn the exact cost of
// the path this machine state will take, which is the segment's bound.
//
// Every rendering follows the pinned core (third_party/redcode-z80): bus order
// of opcode fetches, operand reads and data accesses, cycles, flags including
// the undocumented bits, and MEMPTR. Operands (addresses, constants, the bank
// number) are read from the player's image, never written here. Q needs no
// rendering: the core's final instruction always sets it.
#pragma once
#include "game/dungeon.h"
#include "machine/machine.h"
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace psmodern::game::kit {
inline constexpr std::uint8_t carry_flag=0x01, subtract_flag=0x02, parity_flag=0x04, x_flag=0x08, half_flag=0x10,
    y_flag=0x20, zero_flag=0x40, sign_flag=0x80;

// The registers a segment works on. SP is the body's view of the stack
// pointer: pushes and pops rendered here move it and go through the bus, so a
// segment may pop words an earlier segment pushed (inherited_stack) and end
// with words it pushed still held (a negative exit offset); run() stores SP
// back before the final instruction. wz is MEMPTR.
struct Registers { std::uint8_t a, f, b, c, d, e, h, l; std::uint16_t sp, wz; };
inline Registers load(const Z80& cpu) {
    return {cpu.af.uint8_values.at_1,cpu.af.uint8_values.at_0,cpu.bc.uint8_values.at_1,cpu.bc.uint8_values.at_0,
        cpu.de.uint8_values.at_1,cpu.de.uint8_values.at_0,cpu.hl.uint8_values.at_1,cpu.hl.uint8_values.at_0,
        cpu.sp.uint16_value,cpu.memptr.uint16_value};
}
inline void store(Z80& cpu, const Registers& r) {
    cpu.af.uint8_values.at_1=r.a; cpu.af.uint8_values.at_0=r.f;
    cpu.bc.uint8_values.at_1=r.b; cpu.bc.uint8_values.at_0=r.c;
    cpu.de.uint8_values.at_1=r.d; cpu.de.uint8_values.at_0=r.e;
    cpu.hl.uint8_values.at_1=r.h; cpu.hl.uint8_values.at_0=r.l;
    cpu.sp.uint16_value=r.sp; cpu.memptr.uint16_value=r.wz;
}
constexpr std::uint16_t pair(std::uint8_t high, std::uint8_t low) { return static_cast<std::uint16_t>((high<<8)|low); }
constexpr std::uint8_t high_byte(std::uint16_t value) { return static_cast<std::uint8_t>(value>>8); }
constexpr std::uint8_t low_byte(std::uint16_t value) { return static_cast<std::uint8_t>(value); }

// ---- Flags ------------------------------------------------------------------
inline std::uint8_t sign_zero_xy(std::uint8_t value) {
    return static_cast<std::uint8_t>((value&(sign_flag|y_flag|x_flag))|(value ? 0 : zero_flag));
}
inline std::uint8_t parity(std::uint8_t value) { return std::popcount(value)%2 ? 0 : parity_flag; }
// add/adc a: carry is 0 or 1.
inline void add8(Registers& r, std::uint8_t operand, unsigned carry) {
    const unsigned sum=r.a+operand+carry;
    const auto result=static_cast<std::uint8_t>(sum);
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|((r.a^operand^result)&half_flag)|
        ((~(r.a^operand)&(r.a^result)&0x80) ? parity_flag : 0)|(sum>0xff ? carry_flag : 0));
    r.a=result;
}
// The flags of a - operand - borrow; the result is returned (sub, sbc, neg).
inline std::uint8_t subtract_flags(Registers& r, std::uint8_t minuend, std::uint8_t operand, unsigned borrow) {
    const unsigned difference=static_cast<unsigned>(minuend)-operand-borrow;
    const auto result=static_cast<std::uint8_t>(difference);
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|((minuend^operand^result)&half_flag)|
        (((minuend^operand)&(minuend^result)&0x80) ? parity_flag : 0)|subtract_flag|(difference>0xff ? carry_flag : 0));
    return result;
}
// cp: as sub, but the undocumented bits come from the operand.
inline void compare8(Registers& r, std::uint8_t operand) {
    (void)subtract_flags(r,r.a,operand,0);
    r.f=static_cast<std::uint8_t>((r.f&~(y_flag|x_flag))|(operand&(y_flag|x_flag)));
}
inline void logic_flags(Registers& r, bool and_operation) {
    r.f=static_cast<std::uint8_t>(sign_zero_xy(r.a)|parity(r.a)|(and_operation ? half_flag : 0));
}
inline std::uint8_t increment8(Registers& r, std::uint8_t value) {
    const auto result=static_cast<std::uint8_t>(value+1);
    r.f=static_cast<std::uint8_t>((r.f&carry_flag)|sign_zero_xy(result)|((result&0x0f)==0 ? half_flag : 0)|
        (result==0x80 ? parity_flag : 0));
    return result;
}
inline std::uint8_t decrement8(Registers& r, std::uint8_t value) {
    const auto result=static_cast<std::uint8_t>(value-1);
    r.f=static_cast<std::uint8_t>((r.f&carry_flag)|sign_zero_xy(result)|((result&0x0f)==0x0f ? half_flag : 0)|
        (result==0x7f ? parity_flag : 0)|subtract_flag);
    return result;
}
// Shift or rotate right of one byte (CB group); the bit shifted out goes to
// carry and the bit shifted in is given.
inline std::uint8_t shift_right(Registers& r, std::uint8_t value, unsigned entering) {
    const auto result=static_cast<std::uint8_t>((value>>1)|(entering<<7));
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|parity(result)|(value&1));
    return result;
}
// 16-bit addition into HL: sign, zero and parity stay, the rest comes from the
// sum (undocumented bits and half carry from the high byte); MEMPTR = HL + 1.
inline void add_hl(Registers& r, std::uint16_t operand) {
    const unsigned hl=pair(r.h,r.l), sum=hl+operand;
    r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|((sum>>8)&(y_flag|x_flag))|
        (((hl^operand^sum)>>8)&half_flag)|((sum>>16)&1));
    r.wz=static_cast<std::uint16_t>(hl+1);
    r.h=high_byte(static_cast<std::uint16_t>(sum)); r.l=low_byte(static_cast<std::uint16_t>(sum));
}
// bit n: S only for a set bit 7, Z and P/V for a clear bit, H set, C kept; the
// undocumented bits come from `xy` (the register, or MEMPTR's high byte).
inline void bit_flags(Registers& r, unsigned bit, std::uint8_t value, std::uint8_t xy) {
    const auto tested=static_cast<std::uint8_t>(value&(1u<<bit));
    r.f=static_cast<std::uint8_t>((tested ? (tested&sign_flag) : (zero_flag|parity_flag))|(xy&(y_flag|x_flag))|
        half_flag|(r.f&carry_flag));
}

// ---- Buses ------------------------------------------------------------------
// One bus per way of running a segment: fetch (opcode read and refresh), read,
// write, time, and the cycles elapsed since the segment's entry.
class Live {
public:
    explicit Live(RoutineContext& context) : context_(context) {}
    void fetch(std::uint16_t address) { (void)context_.fetch_opcode(address); }
    std::uint8_t read(std::uint16_t address) { return context_.read(address); }
    void write(std::uint16_t address, std::uint8_t value) { context_.write(address,value); }
    void advance(std::uint64_t cycles) { context_.advance(cycles); }
    std::uint64_t elapsed() const { return context_.elapsed(); }
private:
    RoutineContext& context_;
};
// The same accesses without touching the machine, counting cycles. Reads come
// from the machine's memory until the first write, which makes a private copy
// of the memory (so a bank switch is followed too); a read-only path such as a
// table scan copies nothing. A read of an unmapped address returns the bus
// value the real read would see only by accident, so the estimate is then not
// exact (the caller uses its maximum).
class Estimate {
public:
    explicit Estimate(const Machine& machine) : machine_(machine) {}
    void fetch(std::uint16_t) {}
    std::uint8_t read(std::uint16_t address) {
        const Memory& memory=copy_ ? *copy_ : machine_.memory();
        if (memory.mapping(address).kind==Memory::Kind::unmapped) { uncertain=true; return machine_.peek(address); }
        return memory.read(address);
    }
    void write(std::uint16_t address, std::uint8_t value) {
        if (!copy_) copy_.emplace(machine_.memory());
        copy_->write(address,value);
    }
    void advance(std::uint64_t cycles) { cycles_+=cycles; }
    std::uint64_t elapsed() const { return cycles_; }
    std::uint64_t cycles() const { return cycles_; }
    bool uncertain=false;
private:
    const Machine& machine_;
    std::optional<Memory> copy_;
    std::uint64_t cycles_=0;
};

// ---- Instructions -------------------------------------------------------------
// The original instructions of the dungeon code, each given the address it has
// in the ROM. Register operands are passed by reference to fields of `r`.
template<class Bus> class Instructions {
public:
    Instructions(Bus& bus, Registers& registers) : r(registers), bus_(bus) {}
    Registers& r;

    std::uint16_t hl() const { return pair(r.h,r.l); }
    std::uint16_t de() const { return pair(r.d,r.e); }
    std::uint16_t bc() const { return pair(r.b,r.c); }
    void set_hl(std::uint16_t v) { r.h=high_byte(v); r.l=low_byte(v); }
    void set_de(std::uint16_t v) { r.d=high_byte(v); r.e=low_byte(v); }
    void set_bc(std::uint16_t v) { r.b=high_byte(v); r.c=low_byte(v); }
    bool zero() const { return r.f&zero_flag; }
    bool carry() const { return r.f&carry_flag; }
    bool sign() const { return r.f&sign_flag; }
    bool parity_even() const { return r.f&parity_flag; }
    // Cycles since the segment's entry, and the loop-exit rule of a split
    // scan: at an arrival at a loop exit declared with `after`, the segment
    // ends there exactly when this is true (the mechanism refuses an earlier
    // exit, and a later one diverges from the original).
    std::uint64_t elapsed() const { return bus_.elapsed(); }
    bool loop_exit(std::uint16_t after) const { return bus_.elapsed()>=after; }
    // Direct bus access for state the instructions do not cover (tests).
    Bus& bus() { return bus_; }

    // -- 8-bit loads
    void one_byte(std::uint16_t at, unsigned cycles) { bus_.fetch(at); bus_.advance(cycles); }
    void copy(std::uint16_t at, std::uint8_t& to, std::uint8_t from) { one_byte(at,4); to=from; }             // ld r,r'
    void load_immediate(std::uint16_t at, std::uint8_t& to) { bus_.fetch(at); to=operand(at+1); bus_.advance(7); } // ld r,n
    void load_from_hl(std::uint16_t at, std::uint8_t& to) { bus_.fetch(at); to=bus_.read(hl()); bus_.advance(7); } // ld r,(hl)
    void store_register_to_hl(std::uint16_t at, std::uint8_t value) { bus_.fetch(at); bus_.write(hl(),value); bus_.advance(7); } // ld (hl),r
    void store_to_hl(std::uint16_t at) {                                                                     // ld (hl),n
        bus_.fetch(at); const auto value=operand(at+1); bus_.write(hl(),value); bus_.advance(10);
    }
    void load_a_direct(std::uint16_t at) {                                                                   // ld a,(nn)
        bus_.fetch(at); const auto address=operand_word(at+1); r.a=bus_.read(address);
        r.wz=static_cast<std::uint16_t>(address+1); bus_.advance(13);
    }
    void store_a_direct(std::uint16_t at) {                                                                  // ld (nn),a
        bus_.fetch(at); const auto address=operand_word(at+1);
        r.wz=pair(r.a,low_byte(static_cast<std::uint16_t>(address+1)));
        bus_.write(address,r.a); bus_.advance(13);
    }
    void load_a_from_pair(std::uint16_t at, std::uint16_t address) {                                         // ld a,(bc) / ld a,(de)
        bus_.fetch(at); r.wz=static_cast<std::uint16_t>(address+1); r.a=bus_.read(address); bus_.advance(7);
    }
    void store_a_to_pair(std::uint16_t at, std::uint16_t address) {                                          // ld (bc),a / ld (de),a
        bus_.fetch(at); r.wz=pair(r.a,low_byte(static_cast<std::uint16_t>(address+1)));
        bus_.write(address,r.a); bus_.advance(7);
    }
    // -- 16-bit loads and stack
    void load_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                               // ld rr,nn
        bus_.fetch(at); const auto value=operand_word(at+1); high=high_byte(value); low=low_byte(value); bus_.advance(10);
    }
    void load_hl(std::uint16_t at) { load_pair(at,r.h,r.l); }
    void load_de(std::uint16_t at) { load_pair(at,r.d,r.e); }
    void load_bc(std::uint16_t at) { load_pair(at,r.b,r.c); }
    void load_hl_direct(std::uint16_t at) {                                                                  // ld hl,(nn)
        bus_.fetch(at); const auto address=operand_word(at+1); r.wz=static_cast<std::uint16_t>(address+1);
        r.l=bus_.read(address); r.h=bus_.read(r.wz); bus_.advance(16);
    }
    void store_hl_direct(std::uint16_t at) {                                                                 // ld (nn),hl
        bus_.fetch(at); const auto address=operand_word(at+1); r.wz=static_cast<std::uint16_t>(address+1);
        bus_.write(address,r.l); bus_.write(r.wz,r.h); bus_.advance(16);
    }
    void load_pair_direct(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                        // ld bc/de,(nn) (ED)
        bus_.fetch(at); bus_.fetch(at+1); const auto address=operand_word(at+2); r.wz=static_cast<std::uint16_t>(address+1);
        low=bus_.read(address); high=bus_.read(r.wz); bus_.advance(20);
    }
    void store_pair_direct(std::uint16_t at, std::uint8_t high, std::uint8_t low) {                         // ld (nn),bc/de (ED)
        bus_.fetch(at); bus_.fetch(at+1); const auto address=operand_word(at+2); r.wz=static_cast<std::uint16_t>(address+1);
        bus_.write(address,low); bus_.write(r.wz,high); bus_.advance(20);
    }
    void increment_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                          // inc rr
        one_byte(at,6); const auto v=static_cast<std::uint16_t>(pair(high,low)+1); high=high_byte(v); low=low_byte(v);
    }
    void decrement_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                          // dec rr
        one_byte(at,6); const auto v=static_cast<std::uint16_t>(pair(high,low)-1); high=high_byte(v); low=low_byte(v);
    }
    void increment_hl(std::uint16_t at) { increment_pair(at,r.h,r.l); }
    void decrement_hl(std::uint16_t at) { decrement_pair(at,r.h,r.l); }
    void exchange_de_hl(std::uint16_t at) { one_byte(at,4); std::swap(r.d,r.h); std::swap(r.e,r.l); }      // ex de,hl
    void push(std::uint16_t at, std::uint8_t high, std::uint8_t low) {                                      // push rr (high byte first)
        bus_.fetch(at); push_word(pair(high,low)); bus_.advance(11);
    }
    void pop(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                                     // pop rr (low byte first)
        bus_.fetch(at); const auto v=pop_word(); high=high_byte(v); low=low_byte(v); bus_.advance(10);
    }
    void add_to_hl(std::uint16_t at, std::uint16_t operand) { one_byte(at,11); add_hl(r,operand); }       // add hl,rr

    // -- 8-bit arithmetic and logic
    void add_immediate(std::uint16_t at) { bus_.fetch(at); add8(r,operand(at+1),0); bus_.advance(7); }
    void add_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); add8(r,value,0); }
    void add_from_hl(std::uint16_t at) { bus_.fetch(at); const auto v=bus_.read(hl()); add8(r,v,0); bus_.advance(7); }
    void add_with_carry(std::uint16_t at, std::uint8_t value) { one_byte(at,4); add8(r,value,r.f&carry_flag); }
    void add_with_carry_immediate(std::uint16_t at) { bus_.fetch(at); add8(r,operand(at+1),r.f&carry_flag); bus_.advance(7); }
    void subtract_immediate(std::uint16_t at) { bus_.fetch(at); r.a=subtract_flags(r,r.a,operand(at+1),0); bus_.advance(7); }
    void subtract_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); r.a=subtract_flags(r,r.a,value,0); }
    void subtract_with_carry(std::uint16_t at, std::uint8_t value) { one_byte(at,4); r.a=subtract_flags(r,r.a,value,r.f&carry_flag); }
    void compare_immediate(std::uint16_t at) { bus_.fetch(at); compare8(r,operand(at+1)); bus_.advance(7); }
    void compare_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); compare8(r,value); }
    void compare_from_hl(std::uint16_t at) { bus_.fetch(at); const auto v=bus_.read(hl()); compare8(r,v); bus_.advance(7); }
    void and_immediate(std::uint16_t at) { bus_.fetch(at); r.a&=operand(at+1); logic_flags(r,true); bus_.advance(7); }
    void and_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); r.a&=value; logic_flags(r,true); }
    void or_immediate(std::uint16_t at) { bus_.fetch(at); r.a|=operand(at+1); logic_flags(r,false); bus_.advance(7); }
    void or_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); r.a|=value; logic_flags(r,false); }
    void xor_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); r.a^=value; logic_flags(r,false); }
    void or_a(std::uint16_t at) { or_register(at,r.a); }
    void xor_a(std::uint16_t at) { xor_register(at,r.a); }
    void negate(std::uint16_t at) { bus_.fetch(at); bus_.fetch(at+1); r.a=subtract_flags(r,0,r.a,0); bus_.advance(8); } // neg
    void increment(std::uint16_t at, std::uint8_t& value) { one_byte(at,4); value=increment8(r,value); }   // inc r
    void decrement(std::uint16_t at, std::uint8_t& value) { one_byte(at,4); value=decrement8(r,value); }   // dec r
    void rotate_right_circular_a(std::uint16_t at) {                                                       // rrca
        one_byte(at,4);
        r.a=static_cast<std::uint8_t>((r.a>>1)|(r.a<<7));
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(r.a&(y_flag|x_flag))|(r.a>>7));
    }
    void shift_right_logical(std::uint16_t at, std::uint8_t& value) {                                      // srl r
        bus_.fetch(at); bus_.fetch(at+1); value=shift_right(r,value,0); bus_.advance(8);
    }
    void rotate_right_through_carry(std::uint16_t at, std::uint8_t& value) {                               // rr r
        bus_.fetch(at); bus_.fetch(at+1); value=shift_right(r,value,r.f&carry_flag); bus_.advance(8);
    }
    void test_bit(std::uint16_t at, unsigned bit, std::uint8_t value) {                                    // bit n,r
        bus_.fetch(at); bus_.fetch(at+1); bit_flags(r,bit,value,value); bus_.advance(8);
    }
    void test_bit_at_hl(std::uint16_t at, unsigned bit) {                                                  // bit n,(hl)
        bus_.fetch(at); bus_.fetch(at+1); const auto v=bus_.read(hl()); bit_flags(r,bit,v,high_byte(r.wz)); bus_.advance(12);
    }
    void set_bit_at_hl(std::uint16_t at, unsigned bit) {                                                   // set n,(hl)
        bus_.fetch(at); bus_.fetch(at+1); const auto v=bus_.read(hl());
        bus_.write(hl(),static_cast<std::uint8_t>(v|(1u<<bit))); bus_.advance(15);
    }
    // ldir: every iteration fetches both opcode bytes again; a repeated
    // iteration takes its undocumented bits from the instruction's address and
    // leaves MEMPTR at address + 1, the last one takes them from A + the byte.
    void block_copy(std::uint16_t at) {
        for (;;) {
            bus_.fetch(at); bus_.fetch(at+1);
            const auto value=bus_.read(hl());
            bus_.write(de(),value);
            set_hl(static_cast<std::uint16_t>(hl()+1)); set_de(static_cast<std::uint16_t>(de()+1));
            set_bc(static_cast<std::uint16_t>(bc()-1));
            const auto keep=static_cast<std::uint8_t>(r.f&(sign_flag|zero_flag|carry_flag));
            if (bc()) {
                r.f=static_cast<std::uint8_t>(keep|(high_byte(at)&(y_flag|x_flag))|parity_flag);
                r.wz=static_cast<std::uint16_t>(at+1); bus_.advance(21);
                continue;
            }
            const auto t=static_cast<std::uint8_t>(value+r.a);
            r.f=static_cast<std::uint8_t>(keep|((t&2)<<4)|(t&x_flag));
            bus_.advance(16);
            return;
        }
    }

    // -- Control transfers inside a segment (the operand is read either way)
    std::uint16_t jump(std::uint16_t at) { bus_.fetch(at); r.wz=operand_word(at+1); bus_.advance(10); return r.wz; } // jp nn
    bool jump_if(std::uint16_t at, bool taken) { bus_.fetch(at); r.wz=operand_word(at+1); bus_.advance(10); return taken; } // jp cc,nn
    void relative(std::uint16_t at) { (void)relative_if(at,true); }                                        // jr e
    bool relative_if(std::uint16_t at, bool taken) {                                                        // jr cc,e
        bus_.fetch(at); const auto offset=static_cast<std::int8_t>(operand(at+1));
        if (taken) r.wz=static_cast<std::uint16_t>(at+2+offset);
        bus_.advance(taken ? 12 : 7); return taken;
    }
    bool decrement_jump(std::uint16_t at) {                                                                // djnz e
        bus_.fetch(at); bus_.advance(1);
        r.b=static_cast<std::uint8_t>(r.b-1);
        const auto offset=static_cast<std::int8_t>(operand(at+1));
        const bool taken=r.b!=0;
        if (taken) r.wz=static_cast<std::uint16_t>(at+2+offset);
        bus_.advance(taken ? 12 : 7); return taken;
    }
    // A bare call or return rendered inline (the callee is part of the
    // segment, for example the cell readers): the return address goes through
    // the stack exactly as natively.
    std::uint16_t call(std::uint16_t at) {                                                                 // call nn
        bus_.fetch(at); r.wz=operand_word(at+1); bus_.advance(1);
        push_word(static_cast<std::uint16_t>(at+3)); bus_.advance(16); return r.wz;
    }
    bool call_if(std::uint16_t at, bool taken) {                                                           // call cc,nn
        bus_.fetch(at); r.wz=operand_word(at+1);
        if (!taken) { bus_.advance(10); return false; }
        bus_.advance(1); push_word(static_cast<std::uint16_t>(at+3)); bus_.advance(16); return true;
    }
    std::uint16_t return_from(std::uint16_t at) { bus_.fetch(at); r.wz=pop_word(); bus_.advance(10); return r.wz; } // ret
    bool return_if(std::uint16_t at, bool taken) {                                                         // ret cc
        bus_.fetch(at);
        if (!taken) { bus_.advance(5); return false; }
        r.wz=pop_word(); bus_.advance(11); return true;
    }
    // A conditional return that is not taken (the taken one ends the segment
    // and is executed by the core).
    void return_not_taken(std::uint16_t at) { one_byte(at,5); }

private:
    std::uint8_t operand(unsigned at) { return bus_.read(static_cast<std::uint16_t>(at)); }
    std::uint16_t operand_word(unsigned at) { const auto low=operand(at), high=operand(at+1); return pair(high,low); }
    void push_word(std::uint16_t value) {
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus_.write(r.sp,high_byte(value));
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus_.write(r.sp,low_byte(value));
    }
    std::uint16_t pop_word() {
        const auto low=bus_.read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        const auto high=bus_.read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        return pair(high,low);
    }
    Bus& bus_;
};

// ---- Exit declarations (SegmentSpec) -----------------------------------------------
// Named factories instead of positional initializers. `stack` is SP before the
// final instruction minus the entry SP, in even bytes: negative for words the
// segment pushed and still holds (it needs that much scratch_stack), positive
// for inherited words it popped (it needs that much inherited_stack).
//   returned_at(0x6807)             R 6807
//   returned_at(0x6849,+4)          R 6849 [+4]
//   core_final_at(0x6640,-2)        F 6640 [-2]
//   conditional_at(0x68F4)          X 68F4 (JP cc, JR cc or DJNZ, decided by the core)
//   loop_at(0x6988,128,-2)          L 6988 [-2] (after 128)
inline SegmentExit returned_at(std::uint16_t address, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::returned,.address=address,.stack=stack};
}
inline SegmentExit core_final_at(std::uint16_t address, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::core_final,.address=address,.stack=stack};
}
inline SegmentExit conditional_at(std::uint16_t address, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::core_final,.address=address,.stack=stack,.conditional=true};
}
inline SegmentExit loop_at(std::uint16_t address, std::uint16_t after, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::core_final,.address=address,.stack=stack,.after=after};
}
// The permissions of a segment; every field defaults to none.
struct Permissions {
    bool slot2_mapper=false;      // writes the slot-2 bank register $FFFF
    unsigned scratch_stack=0;     // bytes below the entry SP the body pushes (held words included)
    unsigned inherited_stack=0;   // bytes above the entry SP pushed by an earlier segment of the unit
};
//   segment(dungeon::handler_unit,{returned_at(0x6807),loop_at(0x6618,128)},{.slot2_mapper=true,.scratch_stack=4})
inline SegmentSpec segment(RoutineEntry unit, std::vector<SegmentExit> exits, Permissions permissions={}) {
    SegmentSpec spec{unit,std::move(exits),permissions.slot2_mapper,permissions.scratch_stack};
    spec.inherited_stack=permissions.inherited_stack;
    return spec;
}

// ---- The exit a body takes -------------------------------------------------------
// The last instruction of a segment, executed by the core, and the cycles it
// takes on the path the segment found. A body names the exit by kind and
// address only; the mechanism applies the declaration's stack offset,
// conditional flag and loop threshold. Before returning, the body must leave
// SP (Registers::sp) at the entry SP plus the declared offset: pushes and pops
// rendered with Instructions move it.
struct Exit { SegmentExit segment; unsigned cycles; };
inline Exit returns(std::uint16_t address, unsigned cycles) { return {{RoutineExit::returned,address},cycles}; }
inline Exit continues(std::uint16_t address, unsigned cycles) { return {{RoutineExit::core_final,address},cycles}; }
inline constexpr unsigned call_cycles=17, untaken_call_cycles=10, jump_cycles=10, return_cycles=10,
    taken_conditional_return_cycles=11;
// Conditional final transfers: the core decides from the flags and B the body
// left; only the cycles depend on the outcome, which the body knows.
//   return kit::jump_if_exit(0x68F4);                      // JP cc,nn: 10 either way
//   return kit::relative_if_exit(0x6712,z.zero());          // JR cc,e: 12 taken, 7 not
//   return kit::decrement_jump_exit(z,0x6660);              // DJNZ: B is decremented by the core
inline Exit jump_if_exit(std::uint16_t address) { return continues(address,10); }
inline Exit relative_if_exit(std::uint16_t address, bool taken) { return continues(address,taken ? 12 : 7); }
template<class Z> Exit decrement_jump_exit(const Z& z, std::uint16_t address) { return continues(address,z.r.b!=1 ? 13 : 8); }

// ---- Bounds and registration -------------------------------------------------------
// The exact cost of the path this machine will take: the segment run on the
// estimating bus. When it read an unmapped address the path is not known, and
// `maximum` (the longest path of the segment) is used instead. A split scan
// whose body tests Instructions::loop_exit(after) gets the mechanism's loop
// rule here unchanged, since the estimating bus counts the same elapsed cycles.
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
// A segment whose bound is its estimated path.
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       std::uint64_t maximum, Logic logic) {
    registry.add(entry,std::move(spec),
        [logic,maximum](const Machine& machine) { return cost(machine,logic,maximum); },
        [logic](RoutineContext& context) { run(context,logic); });
}
// A segment with its own bound function (a fixed cost, for one).
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       RegisteredRoutine::Bound bound, Logic logic) {
    registry.add(entry,std::move(spec),std::move(bound),[logic](RoutineContext& context) { run(context,logic); });
}

// ---- The cell readers -----------------------------------------------------------
// 01:6BCA: the kind (low three bits) of the layout cell at view offset index B
// from the party's position and facing; HL is preserved through the stack.
// 01:6BE9: the same cell's value without bit 7 (the opened-door mark); HL is
// left pointing at the cell, DE holds B. Both rendered without their final
// return, so a caller can render them inline after a bare call.
inline constexpr std::uint16_t cell_type_return=0x6BE8, cell_value_return=0x6C05;
inline constexpr unsigned cell_type_cycles=159, cell_value_cycles=138; // with the final return
template<class Bus> void cell_offset_address(Instructions<Bus>& z, std::uint16_t at) {
    // A = facing & 3, times 16; DE = that; HL = offsets + DE + B; A = position + offset.
    auto& r=z.r;
    z.load_a_direct(at);
    z.and_immediate(at+3);
    for (std::uint16_t i=0;i<4;++i) z.add_register(static_cast<std::uint16_t>(at+5+i),r.a);
    z.copy(at+9,r.e,r.a);
    z.load_immediate(at+10,r.d);
    z.load_hl(at+12);
    z.add_to_hl(at+15,z.de());
    z.copy(at+16,r.e,r.b);
    z.add_to_hl(at+17,z.de());
    z.load_a_direct(at+18);
    z.add_from_hl(at+21);
    z.load_immediate(at+22,r.h);
    z.copy(at+24,r.l,r.a);
    z.load_from_hl(at+25,r.a);
}
template<class Bus> void cell_type(Instructions<Bus>& z) {        // 01:6BCA-6BE7
    auto& r=z.r;
    z.push(0x6BCA,r.h,r.l);
    cell_offset_address(z,0x6BCB);
    z.and_immediate(0x6BE5);
    z.pop(0x6BE7,r.h,r.l);
}
template<class Bus> void cell_value(Instructions<Bus>& z) {       // 01:6BE9-6C04
    cell_offset_address(z,0x6BE9);
    z.and_immediate(0x6C03);
}
// A bare `call 6BCA` (kind) or `call 6BE9` (value) at `at`, with the reader's
// body and return rendered in the segment: the one guard every segment that
// renders a reader inline uses. The call pushes its return address and the
// reader's return pops it again. Where the stack does not give back what was
// pushed (stack bytes in ROM), the original returns elsewhere and the
// rendering cannot follow it: the estimate marks itself uncertain, so the
// segment's maximum, chosen above one frame, makes dispatch fall back (reason
// frame) and the core runs the original. The live run never meets such a
// state, since it runs only after the estimate on the same memory; reaching
// one anyway is a logic error. Returns whether the reader came back to the
// instruction after the call (always true on the live side).
template<class Bus> bool inline_reader(Instructions<Bus>& z, bool kind, std::uint16_t at) {
    (void)z.call(at);
    if (kind) cell_type(z); else cell_value(z);
    const auto target=z.return_from(kind ? cell_type_return : cell_value_return);
    if (target==static_cast<std::uint16_t>(at+3)) return true;
    if constexpr (std::is_same_v<Bus,Estimate>) z.bus().uncertain=true;
    else throw std::logic_error("a registered dungeon segment returned from a cell reader to another address");
    return false;
}
}
