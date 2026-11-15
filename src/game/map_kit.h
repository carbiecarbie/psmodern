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
//   - RST 08 rendered inline: the restart, the native VDP address helper at
//     00:0008 and its return, through the stack exactly as natively;
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
using kit::Exit; using kit::returns; using kit::continues;
using kit::returned_at; using kit::core_final_at; using kit::conditional_at; using kit::loop_at;
using kit::Permissions; using kit::segment;
using kit::call_cycles; using kit::untaken_call_cycles; using kit::jump_cycles; using kit::return_cycles;
using kit::taken_conditional_return_cycles;
using kit::jump_if_exit; using kit::relative_if_exit; using kit::decrement_jump_exit;

// RST 08 as a final instruction executed by the core (the segment then needs
// two scratch bytes for the pushed return address, see SegmentSpec).
inline constexpr unsigned restart_cycles=11;
// The native VDP address helper the game calls with RST 08: ld a,e; out
// (n),a; ld a,d; out (n),a; ret. Its port operand is read from the image.
inline constexpr std::uint16_t vdp_address_helper=0x0008, vdp_address_helper_return=0x000E;

// A loop exit on a conditional transfer (JP cc, JR cc or DJNZ): taken at the
// first arrival with `after` cycles elapsed, the core deciding the branch.
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
    void nop(std::uint16_t at) { this->one_byte(at,4); }                                              // nop
    void xor_immediate(std::uint16_t at) {                                                            // xor n
        bus().fetch(at); r.a^=operand(at+1); kit::logic_flags(r,false); bus().advance(7);
    }
    void rotate_left_circular_a(std::uint16_t at) {                                                   // rlca
        this->one_byte(at,4);
        r.a=static_cast<std::uint8_t>((r.a<<1)|(r.a>>7));
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(r.a&(y_flag|x_flag))|(r.a&1));
    }
    void rotate_left_through_carry(std::uint16_t at, std::uint8_t& value) {                          // rl r
        bus().fetch(at); bus().fetch(at+1);
        const auto result=static_cast<std::uint8_t>((value<<1)|(r.f&carry_flag));
        r.f=static_cast<std::uint8_t>(kit::sign_zero_xy(result)|kit::parity(result)|(value>>7));
        value=result; bus().advance(8);
    }
    void increment_at_hl(std::uint16_t at) {                                                          // inc (hl)
        bus().fetch(at); const auto v=bus().read(this->hl()); bus().write(this->hl(),kit::increment8(r,v)); bus().advance(11);
    }
    void decrement_at_hl(std::uint16_t at) {                                                          // dec (hl)
        bus().fetch(at); const auto v=bus().read(this->hl()); bus().write(this->hl(),kit::decrement8(r,v)); bus().advance(11);
    }
    // sbc hl,rr: flags from the 16-bit difference (undocumented bits and half
    // borrow from the high byte); MEMPTR = HL + 1.
    void subtract_hl_with_carry(std::uint16_t at, std::uint16_t operand) {
        bus().fetch(at); bus().fetch(at+1);
        const unsigned lhs=this->hl(), borrow=r.f&carry_flag, difference=lhs-operand-borrow;
        const auto result=static_cast<std::uint16_t>(difference);
        r.f=static_cast<std::uint8_t>(subtract_flag|((result>>8)&(sign_flag|y_flag|x_flag))|(result==0 ? zero_flag : 0)|
            (((lhs^operand^result)&0x1000) ? half_flag : 0)|(((lhs^operand)&(lhs^result)&0x8000) ? parity_flag : 0)|
            (difference>0xffff ? carry_flag : 0));
        r.wz=static_cast<std::uint16_t>(lhs+1); this->set_hl(result); bus().advance(15);
    }
    // ldi: one byte (HL) -> (DE), both incremented, BC decremented; P/V = BC != 0,
    // H and N cleared, the undocumented bits from A + the byte; MEMPTR unchanged.
    void load_increment(std::uint16_t at) {
        bus().fetch(at); bus().fetch(at+1);
        const auto value=bus().read(this->hl());
        bus().write(this->de(),value);
        this->set_hl(static_cast<std::uint16_t>(this->hl()+1)); this->set_de(static_cast<std::uint16_t>(this->de()+1));
        this->set_bc(static_cast<std::uint16_t>(this->bc()-1));
        const auto t=static_cast<std::uint8_t>(value+r.a);
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|carry_flag))|((t&2)<<4)|(t&x_flag)|(this->bc() ? parity_flag : 0));
        bus().advance(16);
    }

    // -- Port output
    void output(std::uint16_t at) {                                                                   // out (n),a
        bus().fetch(at); const auto port=operand(at+1);
        r.wz=pair(r.a,static_cast<std::uint8_t>(port+1));
        bus().out(pair(r.a,port),r.a,10); bus().advance(1);
    }
    // outi: (HL) to port C, HL incremented, B decremented first (the port's
    // high byte is the new B); MEMPTR = BC + 1; flags from the new B and the
    // byte plus the new L.
    void output_increment(std::uint16_t at) {
        bus().fetch(at); bus().fetch(at+1);
        const auto value=bus().read(this->hl());
        this->set_hl(static_cast<std::uint16_t>(this->hl()+1));
        r.b=static_cast<std::uint8_t>(r.b-1);
        r.wz=static_cast<std::uint16_t>(this->bc()+1);
        bus().out(this->bc(),value,15); bus().advance(1);
        const unsigned t=value+r.l;
        r.f=static_cast<std::uint8_t>((r.b&(sign_flag|y_flag|x_flag))|(r.b ? 0 : zero_flag)|
            kit::parity(static_cast<std::uint8_t>((t&7)^r.b))|(t>255 ? (half_flag|carry_flag) : 0)|((value>>6)&subtract_flag));
    }
    // `count` OUTI instructions in a row from `at` (an unrolled upload).
    void output_increment_run(std::uint16_t at, unsigned count) {
        for (unsigned i=0;i<count;++i) output_increment(static_cast<std::uint16_t>(at+2*i));
    }

    // -- RST 08 and inline calls
    // RST 08 at `at` with the native helper rendered: the restart pushes
    // at + 1, the helper writes E then D to its port (the VDP control port)
    // and returns. Returns whether the helper came back to at + 1; where the
    // stack does not give back what was pushed (a stack in ROM), the original
    // returns elsewhere: the estimate marks itself uncertain (the segment's
    // maximum, above one frame, makes dispatch fall back) and the live side
    // never meets such a state, so reaching one there is a logic error.
    bool restart_vdp_address(std::uint16_t at) {
        bus().fetch(at); r.wz=vdp_address_helper; bus().advance(1);
        push_word(static_cast<std::uint16_t>(at+1)); bus().advance(10);
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
        bus().fetch(at); bus().fetch(at+1); index(which)=operand_word(at+2); bus().advance(14);
    }
    void add_to_index(std::uint16_t at, Index which, std::uint16_t operand) {                        // add ix,rr
        bus().fetch(at); bus().fetch(at+1);
        auto& x=index(which); const unsigned sum=x+operand;
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|((sum>>8)&(y_flag|x_flag))|
            (((x^operand^sum)>>8)&half_flag)|((sum>>16)&1));
        r.wz=static_cast<std::uint16_t>(x+1); x=static_cast<std::uint16_t>(sum); bus().advance(15);
    }
    void push_index(std::uint16_t at, Index which) {                                                 // push ix
        bus().fetch(at); bus().fetch(at+1); push_word(index(which)); bus().advance(15);
    }
    void pop_index(std::uint16_t at, Index which) {                                                  // pop ix
        bus().fetch(at); bus().fetch(at+1); index(which)=pop_word(); bus().advance(14);
    }
    void load_from_indexed(std::uint16_t at, Index which, std::uint8_t& to) {                        // ld r,(ix+d)
        const auto address=indexed_address(at,which); to=bus().read(address); bus().advance(19);
    }
    void store_to_indexed(std::uint16_t at, Index which, std::uint8_t value) {                       // ld (ix+d),r
        const auto address=indexed_address(at,which); bus().write(address,value); bus().advance(19);
    }
    void store_immediate_indexed(std::uint16_t at, Index which) {                                    // ld (ix+d),n
        const auto address=indexed_address(at,which); const auto value=operand(at+3);
        bus().write(address,value); bus().advance(19);
    }
    void add_from_indexed(std::uint16_t at, Index which) {                                           // add a,(ix+d)
        const auto address=indexed_address(at,which); kit::add8(r,bus().read(address),0); bus().advance(19);
    }
    void compare_indexed(std::uint16_t at, Index which) {                                            // cp (ix+d)
        const auto address=indexed_address(at,which); kit::compare8(r,bus().read(address)); bus().advance(19);
    }
    void increment_indexed(std::uint16_t at, Index which) {                                          // inc (ix+d)
        const auto address=indexed_address(at,which); const auto v=bus().read(address);
        bus().write(address,kit::increment8(r,v)); bus().advance(23);
    }
    void decrement_indexed(std::uint16_t at, Index which) {                                          // dec (ix+d)
        const auto address=indexed_address(at,which); const auto v=bus().read(address);
        bus().write(address,kit::decrement8(r,v)); bus().advance(23);
    }
    // DD CB d op: only the two prefix bytes are opcode fetches; d and the
    // operation byte are plain reads. The undocumented bits of BIT come from
    // the high byte of the address (MEMPTR).
    void test_bit_indexed(std::uint16_t at, Index which, unsigned bit) {                             // bit n,(ix+d)
        const auto address=indexed_bit_address(at,which); const auto v=bus().read(address);
        kit::bit_flags(r,bit,v,high_byte(address)); bus().advance(20);
    }
    void set_bit_indexed(std::uint16_t at, Index which, unsigned bit) {                              // set n,(ix+d)
        const auto address=indexed_bit_address(at,which); const auto v=bus().read(address);
        bus().write(address,static_cast<std::uint8_t>(v|(1u<<bit))); bus().advance(23);
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
        (void)operand(at+3);
        return r.wz;
    }
    bool came_back(std::uint16_t target, std::uint16_t expected) {
        if (target==expected) return true;
        if constexpr (std::is_base_of_v<kit::Estimate,Bus>) bus().uncertain=true;
        else throw std::logic_error("a registered map segment returned from an inline call to another address");
        return false;
    }
};

// ---- Bounds and registration -----------------------------------------------------
// As in the milestone 7 kit, with the index registers loaded and stored and
// the port-capable buses. `maximum` is used when the estimate is uncertain;
// choose it above one frame (2 * Machine::frame_cycles) so that such a state
// falls back.
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
    const auto exit=logic(instructions);
    store(cpu,registers);
    if (exit.segment.kind==RoutineExit::returned) context.finish_return(exit.segment.address);
    else context.finish_core(exit.segment.address);
}
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       std::uint64_t maximum, Logic logic) {
    registry.add(entry,std::move(spec),
        [logic,maximum](const Machine& machine) { return cost(machine,logic,maximum); },
        [logic](RoutineContext& context) { run(context,logic); });
}
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec, Logic logic) {
    add_segment(registry,entry,std::move(spec),uncertain_maximum,std::move(logic));
}
}
