// SPDX-License-Identifier: GPL-3.0-or-later
// Rendering kit of the milestone 11 map segments (map_video.cpp,
// map_scroll.cpp, map_objects.cpp, map_terrain.cpp, map_entrances.cpp,
// map_modes.cpp). It extends the milestone 7 kit (dungeon_kit.h) without
// changing it: the same instructions, flags, buses and exit declarations,
// plus what the map code needs on top:
//   - the index registers IX and IY (kept in Registers beside the kit's own),
//     with their indexed loads, stores, arithmetic, bit tests, pushes and pops;
//   - port output (OUT (n),A and OUTI) at the original T-state of the write,
//     on the live bus through RoutineContext::write_port and on the estimating
//     bus as time only (no routine in scope reads a port);
//   - RST 08 rendered inline: the restart, the VDP address helper at
//     00:0008 and its return, through the stack exactly as in the original;
//   - a generic inline call for the unrolled upload tails (a bare CALL whose
//     callee the segment renders, with the same return guard);
//   - NOP, LDI, XOR n, SBC HL,rr, RL r, RLCA, INC/DEC (HL).
// Every rendering follows the pinned core (third_party/redcode-z80) and is
// proved instruction by instruction against it in tests/map_kit_tests.cpp.
#pragma once
#include "game/dungeon_kit.h"
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace psmodern::game::map_kit {
using kit::carry_flag; using kit::subtract_flag; using kit::parity_flag; using kit::x_flag;
using kit::half_flag; using kit::y_flag; using kit::zero_flag; using kit::sign_flag;
using kit::pair; using kit::high_byte; using kit::low_byte;
using kit::Exit; using kit::fall_through; using kit::refused; using kit::Condition; using kit::Guard;
using kit::returned_at; using kit::core_final_at; using kit::conditional_at; using kit::loop_at;
using kit::Permissions; using kit::segment;
using kit::call_cycles; using kit::untaken_call_cycles; using kit::jump_cycles; using kit::return_cycles;
using kit::taken_conditional_return_cycles;

// RST 08 as a final instruction left to the next segment (the segment then needs
// two scratch bytes for the pushed return address, see SegmentSpec).
inline constexpr unsigned restart_cycles=11;
// The VDP address helper the game calls with RST 08: ld a,e; out
// (n),a; ld a,d; out (n),a; ret. Its port operand is read from the image.
inline constexpr std::uint16_t vdp_address_helper=0x0008, vdp_address_helper_return=0x000E;

// A loop exit on a conditional transfer (JP cc, JR cc or DJNZ): taken at the
// first arrival with `after` cycles elapsed, the next segment deciding the branch.
//   conditional_loop_at(0x02D2,1024)     XL 02D2 (after 1024)
inline SegmentExit conditional_loop_at(std::uint16_t address, std::uint16_t after, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::core_final,.address=address,.stack=stack,.conditional=true,.after=after};
}

// ---- Registers -----------------------------------------------------------------
// The kit's registers plus the index registers. Instructions<Bus>::r is this
// type, so a body writes z.r.ix as naturally as z.r.a.
struct Registers : kit::Registers { std::uint16_t ix, iy; };
inline Registers load(const Z80& cpu) {
    Registers r{};
    static_cast<kit::Registers&>(r)=kit::load(cpu);
    r.ix=cpu.ix_iy[0].uint16_value; r.iy=cpu.ix_iy[1].uint16_value;
    return r;
}
inline void store(Z80& cpu, const Registers& r) {
    kit::store(cpu,r);
    cpu.ix_iy[0].uint16_value=r.ix; cpu.ix_iy[1].uint16_value=r.iy;
}
enum class Index { ix, iy };

// ---- Buses ---------------------------------------------------------------------
// out(port, value, delay): the port write happens `delay` cycles from now (the
// T-state of the write inside its instruction); time advances by `delay`.
class Live : public kit::Live {
public:
    explicit Live(RoutineContext& context) : kit::Live(context), context_(context) {}
    void out(std::uint16_t port, std::uint8_t value, std::uint64_t delay) {
        context_.write_port(port,value,elapsed()+delay);
    }
private:
    RoutineContext& context_;
};
class Estimate : public kit::Estimate {
public:
    explicit Estimate(const Machine& machine) : kit::Estimate(machine) {}
    void out(std::uint16_t, std::uint8_t, std::uint64_t delay) { advance(delay); }
};

// ---- Instructions --------------------------------------------------------------
template<class Bus> class Instructions : public kit::Instructions<Bus> {
    using Base=kit::Instructions<Bus>;
public:
    Instructions(Bus& bus, Registers& registers) : Base(bus,registers), r(registers) {}
    // Shadows the kit's view of the same object with the index registers added.
    Registers& r;
    using Base::bus;

    std::uint16_t& index(Index which) { return which==Index::ix ? r.ix : r.iy; }

    // -- Miscellaneous
    void nop(std::uint16_t at) { bus().expect(at,op(0x00)); this->one_byte(at,4); }                                              // nop
    void xor_immediate(std::uint16_t at) {                                                            // xor n
        bus().expect(at,op(0xEE)); bus().fetch(at); r.a^=operand(at+1); kit::logic_flags(r,false); bus().advance(7);
    }
    void rotate_left_circular_a(std::uint16_t at) {                                                   // rlca
        bus().expect(at,op(0x07)); this->one_byte(at,4);
        r.a=static_cast<std::uint8_t>((r.a<<1)|(r.a>>7));
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(r.a&(y_flag|x_flag))|(r.a&1));
        r.q=r.f;
    }
    void rotate_left_through_carry(std::uint16_t at, std::uint8_t& value) {                          // rl r
        bus().expect(at,this->with_field(0xCB,0x10,0,value)); bus().fetch(at); bus().fetch(at+1);
        const auto result=static_cast<std::uint8_t>((value<<1)|(r.f&carry_flag));
        r.f=static_cast<std::uint8_t>(kit::sign_zero_xy(result)|kit::parity(result)|(value>>7));
        r.q=r.f;
        value=result; bus().advance(8);
    }
    void increment_at_hl(std::uint16_t at) {                                                          // inc (hl)
        bus().expect(at,op(0x34)); bus().fetch(at); const auto v=bus().read(this->hl()); bus().write(this->hl(),kit::increment8(r,v)); bus().advance(11);
    }
    void decrement_at_hl(std::uint16_t at) {                                                          // dec (hl)
        bus().expect(at,op(0x35)); bus().fetch(at); const auto v=bus().read(this->hl()); bus().write(this->hl(),kit::decrement8(r,v)); bus().advance(11);
    }
    // sbc hl,rr: flags from the 16-bit difference (undocumented bits and half
    // borrow from the high byte); MEMPTR = HL + 1.
    void subtract_hl_with_carry(std::uint16_t at, std::uint16_t operand) {
        bus().expect(at,OpcodePattern{0xED,0x42,0xCF}); bus().fetch(at); bus().fetch(at+1);
        const unsigned lhs=this->hl(), borrow=r.f&carry_flag, difference=lhs-operand-borrow;
        const auto result=static_cast<std::uint16_t>(difference);
        r.f=static_cast<std::uint8_t>(subtract_flag|((result>>8)&(sign_flag|y_flag|x_flag))|(result==0 ? zero_flag : 0)|
            (((lhs^operand^result)&0x1000) ? half_flag : 0)|(((lhs^operand)&(lhs^result)&0x8000) ? parity_flag : 0)|
            (difference>0xffff ? carry_flag : 0));
        r.q=r.f;
        r.wz=static_cast<std::uint16_t>(lhs+1); this->set_hl(result); bus().advance(15);
    }
    // ldi: one byte (HL) -> (DE), both incremented, BC decremented; P/V = BC != 0,
    // H and N cleared, the undocumented bits from A + the byte; MEMPTR unchanged.
    void load_increment(std::uint16_t at) {
        bus().expect(at,OpcodePattern::prefixed(0xED,0xA0)); bus().fetch(at); bus().fetch(at+1);
        const auto value=bus().read(this->hl());
        bus().write(this->de(),value);
        this->set_hl(static_cast<std::uint16_t>(this->hl()+1)); this->set_de(static_cast<std::uint16_t>(this->de()+1));
        this->set_bc(static_cast<std::uint16_t>(this->bc()-1));
        const auto t=static_cast<std::uint8_t>(value+r.a);
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|carry_flag))|((t&2)<<4)|(t&x_flag)|(this->bc() ? parity_flag : 0));
        r.q=r.f;
        bus().advance(16);
    }

    // -- Port output
    void output(std::uint16_t at) {                                                                   // out (n),a
        bus().expect(at,op(0xD3)); bus().fetch(at); const auto port=operand(at+1);
        r.wz=pair(r.a,static_cast<std::uint8_t>(port+1));
        bus().out(pair(r.a,port),r.a,10); bus().advance(1);
    }
    // outi: (HL) to port C, HL incremented, B decremented first (the port's
    // high byte is the new B); MEMPTR = BC + 1; flags from the new B and the
    // byte plus the new L.
    void output_increment(std::uint16_t at) {
        bus().expect(at,OpcodePattern::prefixed(0xED,0xA3)); bus().fetch(at); bus().fetch(at+1);
        const auto value=bus().read(this->hl());
        this->set_hl(static_cast<std::uint16_t>(this->hl()+1));
        r.b=static_cast<std::uint8_t>(r.b-1);
        r.wz=static_cast<std::uint16_t>(this->bc()+1);
        bus().out(this->bc(),value,15); bus().advance(1);
        const unsigned t=value+r.l;
        r.f=static_cast<std::uint8_t>((r.b&(sign_flag|y_flag|x_flag))|(r.b ? 0 : zero_flag)|
            kit::parity(static_cast<std::uint8_t>((t&7)^r.b))|(t>255 ? (half_flag|carry_flag) : 0)|((value>>6)&subtract_flag));
        r.q=r.f;
    }
    // otir: one iteration, as output_increment; while B is not zero after the
    // decrement the iteration repeats with an extra M-cycle of 5 T-states, in
    // which the undocumented bits come from the instruction's address, H and
    // P/V change as the pinned core computes them (Banks 2018) and MEMPTR is
    // the address + 1. The exact flags matter at the boundary between two
    // iterations, where an interrupt can be accepted.
    void output_increment_repeat(std::uint16_t at) {
        bus().expect(at,OpcodePattern::prefixed(0xED,0xB3)); bus().fetch(at); bus().fetch(at+1);
        const auto value=bus().read(this->hl());
        this->set_hl(static_cast<std::uint16_t>(this->hl()+1));
        r.b=static_cast<std::uint8_t>(r.b-1);
        const unsigned t=value+r.l;
        const auto n=static_cast<std::uint8_t>((value>>6)&subtract_flag);
        const bool carry=t>255;
        const auto p=static_cast<std::uint8_t>((t&7)^r.b);
        if (r.b) {
            r.wz=static_cast<std::uint16_t>(at+1);
            bus().out(this->bc(),value,15); bus().advance(1);
            const auto b=r.b;
            const std::uint8_t extra=!carry ? kit::parity(static_cast<std::uint8_t>(p^(b&7))) :
                static_cast<std::uint8_t>(carry_flag|(n ?
                    ((b&0x0f)==0 ? half_flag : 0)|kit::parity(static_cast<std::uint8_t>(p^((b-1)&7))) :
                    ((b&0x0f)==0x0f ? half_flag : 0)|kit::parity(static_cast<std::uint8_t>(p^((b+1)&7)))));
            r.f=static_cast<std::uint8_t>((b&sign_flag)|(high_byte(at)&(y_flag|x_flag))|n|extra);
            r.q=r.f;
            bus().advance(5);
            return;
        }
        r.wz=static_cast<std::uint16_t>(this->bc()+1);
        bus().out(this->bc(),value,15); bus().advance(1);
        r.f=static_cast<std::uint8_t>(zero_flag|(carry ? half_flag|carry_flag : 0)|kit::parity(p)|n);
        r.q=r.f;
    }
    // otir as a slice of at most `limit` iterations, each an instruction with
    // its own boundary (milestone 14 final): the segment ends at the output
    // repeat exit, at `at` again while B is not zero, else at at + 2.
    Exit final_output_repeat(std::uint16_t at, unsigned limit=16) {                                  // otir
        bus().expect(at,OpcodePattern::prefixed(0xED,0xB3));
        for (unsigned i=0;i<limit;++i) { output_increment_repeat(at); if (!r.b) break; }
        return {{RoutineExit::output_repeat,at},0,r.b ? at : static_cast<std::uint16_t>(at+2)};
    }
    // `count` OUTI instructions in a row from `at` (an unrolled upload).
    void output_increment_run(std::uint16_t at, unsigned count) {
        for (unsigned i=0;i<count;++i) output_increment(static_cast<std::uint16_t>(at+2*i));
    }

    // -- RST 08 and inline calls
    // RST 08 at `at` with the helper rendered: the restart pushes
    // at + 1, the helper writes E then D to its port (the VDP control port)
    // and returns. Returns whether the helper came back to at + 1; where the
    // stack does not give back what was pushed (a stack in ROM), the original
    // returns elsewhere: an explicit in-body guard (the estimate is uncertain,
    // so the segment refuses the state at its entry and the run stops; the
    // in-body guard stops with GuardFailure).
    bool restart_vdp_address(std::uint16_t at) {
        bus().expect(at,op(0xCF)); bus().fetch(at); r.wz=vdp_address_helper;
        // Native memory callbacks occur at the RST instruction's entry cycle.
        push_word(static_cast<std::uint16_t>(at+1)); bus().advance(11);
        this->copy(vdp_address_helper,r.a,r.e);
        output(vdp_address_helper+1);
        this->copy(vdp_address_helper+3,r.a,r.d);
        output(vdp_address_helper+4);
        return came_back(this->return_from(vdp_address_helper_return),static_cast<std::uint16_t>(at+1));
    }
    // A bare `call nn` at `at` whose callee the segment renders: `callee(z)`
    // renders the callee's instructions except its final `ret`, which is at
    // `return_at`. Same guard as restart_vdp_address.
    template<class Callee> bool inline_call(std::uint16_t at, std::uint16_t return_at, Callee callee) {
        (void)this->call(at);
        callee(*this);
        return came_back(this->return_from(return_at),static_cast<std::uint16_t>(at+3));
    }

    // -- Index registers
    void load_index(std::uint16_t at, Index which) {                                                 // ld ix,nn
        bus().expect(at,indexed(which,0x21)); bus().fetch(at); bus().fetch(at+1); index(which)=operand_word(at+2); bus().advance(14);
    }
    void add_to_index(std::uint16_t at, Index which, std::uint16_t operand) {                        // add ix,rr
        bus().expect(at,indexed(which,0x09,0xCF)); bus().fetch(at); bus().fetch(at+1);
        auto& x=index(which); const unsigned sum=x+operand;
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|((sum>>8)&(y_flag|x_flag))|
            (((x^operand^sum)>>8)&half_flag)|((sum>>16)&1));
        r.q=r.f;
        r.wz=static_cast<std::uint16_t>(x+1); x=static_cast<std::uint16_t>(sum); bus().advance(15);
    }
    void push_index(std::uint16_t at, Index which) {                                                 // push ix
        bus().expect(at,indexed(which,0xE5)); bus().fetch(at); bus().fetch(at+1); bus().advance(4);
        push_word(index(which)); bus().advance(11);
    }
    void pop_index(std::uint16_t at, Index which) {                                                  // pop ix
        bus().expect(at,indexed(which,0xE1)); bus().fetch(at); bus().fetch(at+1); index(which)=pop_word(); bus().advance(14);
    }
    void load_from_indexed(std::uint16_t at, Index which, std::uint8_t& to) {                        // ld r,(ix+d)
        bus().expect(at,indexed(which,0x46,0xC7)); const auto address=indexed_address(at,which); to=bus().read(address); bus().advance(19);
    }
    void store_to_indexed(std::uint16_t at, Index which, std::uint8_t value) {                       // ld (ix+d),r
        bus().expect(at,indexed(which,0x70,0xF8)); const auto address=indexed_address(at,which); bus().advance(4);
        bus().write(address,value); bus().advance(15);
    }
    void store_immediate_indexed(std::uint16_t at, Index which) {                                    // ld (ix+d),n
        bus().expect(at,indexed(which,0x36)); const auto address=indexed_address(at,which); const auto value=operand(at+3);
        bus().advance(4); bus().write(address,value); bus().advance(15);
    }
    void add_from_indexed(std::uint16_t at, Index which) {                                           // add a,(ix+d)
        bus().expect(at,indexed(which,0x86)); const auto address=indexed_address(at,which); kit::add8(r,bus().read(address),0); bus().advance(19);
    }
    void compare_indexed(std::uint16_t at, Index which) {                                            // cp (ix+d)
        bus().expect(at,indexed(which,0xBE)); const auto address=indexed_address(at,which); kit::compare8(r,bus().read(address)); bus().advance(19);
    }
    void increment_indexed(std::uint16_t at, Index which) {                                          // inc (ix+d)
        bus().expect(at,indexed(which,0x34)); const auto address=indexed_address(at,which); bus().advance(4);
        const auto v=bus().read(address);
        bus().write(address,kit::increment8(r,v)); bus().advance(19);
    }
    void decrement_indexed(std::uint16_t at, Index which) {                                          // dec (ix+d)
        bus().expect(at,indexed(which,0x35)); const auto address=indexed_address(at,which); bus().advance(4);
        const auto v=bus().read(address);
        bus().write(address,kit::decrement8(r,v)); bus().advance(19);
    }
    // DD CB d op: only the two prefix bytes are opcode fetches; d and the
    // operation byte are plain reads. The undocumented bits of BIT come from
    // the high byte of the address (MEMPTR).
    void test_bit_indexed(std::uint16_t at, Index which, unsigned bit) {                             // bit n,(ix+d)
        bus().expect(at,indexed(which,0xCB)); const auto address=indexed_bit_address(at,which); const auto v=bus().read(address);
        kit::bit_flags(r,bit,v,high_byte(address)); bus().advance(20);
    }
    void set_bit_indexed(std::uint16_t at, Index which, unsigned bit) {                              // set n,(ix+d)
        bus().expect(at,indexed(which,0xCB)); const auto address=indexed_bit_address(at,which); bus().advance(4);
        const auto v=bus().read(address);
        bus().write(address,static_cast<std::uint8_t>(v|(1u<<bit))); bus().advance(19);
    }

protected:
    using Base::op;
    // DD or FD, then the operation (review R2 M1).
    static constexpr OpcodePattern indexed(Index which, std::uint8_t value, std::uint8_t mask=0xff) {
        return {static_cast<std::uint8_t>(which==Index::ix ? 0xDD : 0xFD),value,mask};
    }
private:
    std::uint8_t operand(unsigned at) { return bus().read(static_cast<std::uint16_t>(at)); }
    std::uint16_t operand_word(unsigned at) { const auto low=operand(at), high=operand(at+1); return pair(high,low); }
    void push_word(std::uint16_t value) {
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus().write(r.sp,high_byte(value));
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus().write(r.sp,low_byte(value));
    }
    std::uint16_t pop_word() {
        const auto low=bus().read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        const auto high=bus().read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        return pair(high,low);
    }
    // DD op d: prefix and operation are opcode fetches, d a read; MEMPTR = IX + d.
    std::uint16_t indexed_address(std::uint16_t at, Index which) {
        bus().fetch(at); bus().fetch(at+1);
        const auto offset=static_cast<std::int8_t>(operand(at+2));
        r.wz=static_cast<std::uint16_t>(index(which)+offset); return r.wz;
    }
    std::uint16_t indexed_bit_address(std::uint16_t at, Index which) {
        bus().fetch(at); bus().fetch(at+1);
        const auto offset=static_cast<std::int8_t>(operand(at+2));
        r.wz=static_cast<std::uint16_t>(index(which)+offset);
        (void)operand(at+3); // the operation byte, read as data
        return r.wz;
    }
    // The return-elsewhere state of an inline call (a stack not in RAM) is an
    // explicit in-body guard (kit::Instructions::guard).
    bool came_back(std::uint16_t target, std::uint16_t expected) {
        return this->guard(target==expected,"an inline call returned to another address (stack not in RAM)");
    }
};

// ---- Bounds and registration -----------------------------------------------------
// As in the milestone 7 kit, with the index registers loaded and stored and
// the port-capable buses. `maximum` is used when the estimate is uncertain;
// choose it above one frame (2 * Machine::frame_cycles) so that the segment
// refuses such a state at its entry (the run stops).
inline constexpr std::uint64_t uncertain_maximum=2*Machine::frame_cycles;
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
    Exit exit{};
    {
        kit::Sync<Registers,&map_kit::store,&map_kit::load> sync(registers);
        RoutineContext::SyncScope scope(context,sync);
        exit=logic(instructions);
    }
    store(cpu,registers);
    kit::finish(context,exit);
}
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       std::uint64_t maximum, Logic logic,
                                       std::source_location where=std::source_location::current()) {
    registry.add_exact(entry,std::move(spec),
        [logic,maximum](const Machine& machine) { return cost(machine,logic,maximum); },
        [logic](RoutineContext& context) { run(context,logic); },where);
}
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec, Logic logic,
                                       std::source_location where=std::source_location::current()) {
    add_segment(registry,entry,std::move(spec),uncertain_maximum,std::move(logic),where);
}
// A segment with its own bound function (a fixed cost, for one).
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       RegisteredRoutine::Bound bound, Logic logic,
                                       std::source_location where=std::source_location::current()) {
    registry.add_exact(entry,std::move(spec),std::move(bound),[logic](RoutineContext& context) { run(context,logic); },where);
}
// With an explicit entry guard (see kit::Guard).
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       kit::Guard guard, Logic logic,
                                       std::source_location where=std::source_location::current()) {
    registry.add_exact(entry,std::move(spec),std::move(guard),
        [logic](const Machine& machine) { return cost(machine,logic,uncertain_maximum); },
        [logic](RoutineContext& context) { run(context,logic); },where);
}
// Each scroll update writes the horizontal and vertical register pairs.
template<class Z> void write_scroll_registers(Z& z,std::uint16_t at) {
    for (unsigned offset:{0u,9u}) {
        z.load_a_direct(at+offset);
        z.output(at+offset+3);
        z.load_immediate(at+offset+5,z.r.a);
        z.output(at+offset+7);
    }
}

}
