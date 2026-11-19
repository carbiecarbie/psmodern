// SPDX-License-Identifier: GPL-3.0-or-later
// Rendering kit of the milestone 12 menu segments (menu_engine.cpp,
// menu_windows.cpp, menu_select.cpp, menu_field.cpp, menu_places.cpp,
// menu_fades.cpp). It extends the milestone 11 map kit (map_kit.h), which
// extends the milestone 7 kit (dungeon_kit.h), without changing either: the
// same registers, instructions, flags, buses and exit declarations, plus
// what the menu code needs on top:
//   - port input: INI (the window engine reads video memory through the VDP
//     data port), at the original T-state of the read, on the live bus
//     through RoutineContext::read_port and on the estimating bus as time
//     only (the loop is counted by B; the bytes read are not known there);
//   - CPL, RES b,r, SET b,r and SUB (IX+d)/(IY+d);
//   - an inline subroutine whose callee renders its own (possibly
//     conditional) return: the digit helper inside the number printers and
//     the per-colour fade steps inside the fade step have several returns.
// Every rendering follows the pinned core (third_party/redcode-z80) and is
// proved instruction by instruction against it in tests/menu_kit_tests.cpp.
#pragma once
#include "game/map_kit.h"
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace psmodern::game::menu_kit {
using kit::carry_flag; using kit::subtract_flag; using kit::parity_flag; using kit::x_flag;
using kit::half_flag; using kit::y_flag; using kit::zero_flag; using kit::sign_flag;
using kit::pair; using kit::high_byte; using kit::low_byte;
using kit::Exit; using kit::fall_through; using kit::refused; using kit::Condition; using kit::Guard;
using kit::returned_at; using kit::core_final_at; using kit::conditional_at; using kit::loop_at;
using kit::Permissions; using kit::segment;
using kit::call_cycles; using kit::untaken_call_cycles; using kit::jump_cycles; using kit::return_cycles;
using kit::taken_conditional_return_cycles;
using map_kit::conditional_loop_at; using map_kit::restart_cycles;
using map_kit::vdp_address_helper; using map_kit::vdp_address_helper_return;
using map_kit::Registers; using map_kit::Index; using map_kit::load; using map_kit::store;
using map_kit::uncertain_maximum;

// ---- Buses ---------------------------------------------------------------------
// in(port, delay): the port read happens `delay` cycles from now (the T-state
// of the read inside its instruction) and time advances by `delay`. Only the
// live bus knows the value; the estimating bus returns 0 (the menu code that
// reads a port stores the byte and counts the loop with B, so its path and
// cost do not depend on the byte).
class Live : public map_kit::Live {
public:
    explicit Live(RoutineContext& context) : map_kit::Live(context), context_(context) {}
    std::uint8_t in(std::uint16_t port, std::uint64_t delay) { return context_.read_port(port,elapsed()+delay); }
    std::uint8_t input_increment(std::uint16_t port,std::uint16_t destination) {
        return context_.input_increment(port,destination);
    }
private:
    RoutineContext& context_;
};
class Estimate : public map_kit::Estimate {
public:
    explicit Estimate(const Machine& machine) : map_kit::Estimate(machine) {}
    std::uint8_t in(std::uint16_t, std::uint64_t delay) { advance(delay); return 0; }
    std::uint8_t input_increment(std::uint16_t,std::uint16_t destination) {
        write(destination,0); advance(16); return 0;
    }
};

// ---- Instructions --------------------------------------------------------------
template<class Bus> class Instructions : public map_kit::Instructions<Bus> {
    using Base=map_kit::Instructions<Bus>;
public:
    Instructions(Bus& bus, Registers& registers) : Base(bus,registers) {}
    using Base::r;
    using Base::bus;

    // ini: port BC (B before the decrement) read at T12 into (HL), HL
    // incremented, B decremented; MEMPTR = BC + 1 with the old B; flags from
    // the new B and from the byte plus C + 1 (as the core computes them).
    void input_increment(std::uint16_t at) {
        bus().expect(at,OpcodePattern::prefixed(0xED,0xA2)); bus().fetch(at); bus().fetch(at+1);
        const auto port=this->bc();
        const auto value=bus().input_increment(port,this->hl());
        this->set_hl(static_cast<std::uint16_t>(this->hl()+1));
        r.wz=static_cast<std::uint16_t>(port+1);
        r.b=static_cast<std::uint8_t>(r.b-1);
        const unsigned t=value+static_cast<std::uint8_t>(r.c+1);
        r.f=static_cast<std::uint8_t>((r.b&(sign_flag|y_flag|x_flag))|(r.b ? 0 : zero_flag)|
            kit::parity(static_cast<std::uint8_t>((t&7)^r.b))|(t>255 ? (half_flag|carry_flag) : 0)|((value>>6)&subtract_flag));
        r.q=r.f;
    }
    // cpl: A inverted; H and N set, S, Z, P/V and C kept, the undocumented
    // bits from the result.
    void complement_a(std::uint16_t at) {
        bus().expect(at,this->op(0x2F)); this->one_byte(at,4);
        r.a=static_cast<std::uint8_t>(~r.a);
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag|carry_flag))|(r.a&(y_flag|x_flag))|half_flag|subtract_flag);
        r.q=r.f;
    }
    // res b,r and set b,r: two opcode fetches, flags unchanged.
    void reset_bit(std::uint16_t at, unsigned bit, std::uint8_t& value) {
        bus().expect(at,this->with_field(0xCB,static_cast<std::uint8_t>(0x80|((bit&7)<<3)),0,value)); bus().fetch(at); bus().fetch(at+1); value=static_cast<std::uint8_t>(value&~(1u<<bit)); bus().advance(8);
    }
    void set_bit(std::uint16_t at, unsigned bit, std::uint8_t& value) {
        bus().expect(at,this->with_field(0xCB,static_cast<std::uint8_t>(0xC0|((bit&7)<<3)),0,value)); bus().fetch(at); bus().fetch(at+1); value=static_cast<std::uint8_t>(value|(1u<<bit)); bus().advance(8);
    }
    // sub (ix+d) / sub (iy+d): MEMPTR = IX + d.
    void subtract_indexed(std::uint16_t at, Index which) {
        bus().expect(at,this->indexed(which,0x96)); bus().fetch(at); bus().fetch(at+1);
        const auto offset=static_cast<std::int8_t>(bus().read(static_cast<std::uint16_t>(at+2)));
        r.wz=static_cast<std::uint16_t>(this->index(which)+offset);
        r.a=kit::subtract_flags(r,r.a,bus().read(r.wz),0);
        bus().advance(19);
    }
    // A bare `call nn` at `at` whose callee the segment renders completely,
    // its returning instruction included: `callee(z)` renders the callee's
    // path and ends with return_from (ret) or a taken return_if (ret cc), and
    // returns the address it popped (r.wz). Same explicit in-body guard as
    // map_kit::Instructions::inline_call for a return elsewhere (a stack not
    // in RAM).
    template<class Callee> bool inline_subroutine(std::uint16_t at, Callee callee) {
        (void)this->call(at);
        const std::uint16_t target=callee(*this);
        return this->guard(target==static_cast<std::uint16_t>(at+3),
                           "an inline subroutine returned to another address (stack not in RAM)");
    }
};

// ---- Bounds and registration -----------------------------------------------------
// As in the map kit, with the port-input buses. `maximum` is used when the
// estimate is uncertain; choose it above one frame (uncertain_maximum) so that
// the segment refuses such a state at its entry (the run stops).
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
}
