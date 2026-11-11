// SPDX-License-Identifier: GPL-3.0-or-later
// Shared party money addition. This routine also serves nonbattle callers;
// the optional battle multiplier therefore never belongs here.
#include "game/money.h"
#include "machine/machine.h"
#include <algorithm>
#include <cstdint>
#include <utility>

namespace psmodern::game {
namespace {
struct Registers {
    std::uint8_t f, d, e, h, l;
};
Registers load(const Z80& c) {
    return {c.af.uint8_values.at_0,c.de.uint8_values.at_1,c.de.uint8_values.at_0,
        c.hl.uint8_values.at_1,c.hl.uint8_values.at_0};
}
void store(Z80& c,const Registers& r) {
    c.af.uint8_values.at_0=r.f; c.de.uint8_values.at_1=r.d; c.de.uint8_values.at_0=r.e;
    c.hl.uint8_values.at_1=r.h; c.hl.uint8_values.at_0=r.l;
}
class Live {
public:
    explicit Live(RoutineContext& c):c_(c) {}
    void fetch(std::uint16_t at) { (void)c_.fetch_opcode(at); }
    std::uint8_t read(std::uint16_t at) { return c_.read(at); }
    void write(std::uint16_t at,std::uint8_t value) { c_.write(at,value); }
    void advance(unsigned cycles) { c_.advance(cycles); }
private: RoutineContext& c_;
};
class Estimate {
public:
    explicit Estimate(const Machine& m):m_(m),memory_(m.memory()) {}
    void fetch(std::uint16_t) {}
    std::uint8_t read(std::uint16_t at) {
        if(memory_.mapping(at).kind==Memory::Kind::unmapped) { uncertain=true; return m_.peek(at); }
        return memory_.read(at);
    }
    void write(std::uint16_t at,std::uint8_t value) { memory_.write(at,value); }
    void advance(unsigned cycles) { cycles_+=cycles; }
    bool uncertain=false;
    unsigned cycles_=0;
private: const Machine& m_; Memory memory_;
};
template<class Bus> void execute(Bus& bus,Registers& r) {
    auto word=[&](std::uint16_t at) { const auto l=bus.read(at), h=bus.read(at+1); return static_cast<std::uint16_t>(l|(h<<8)); };
    auto swap=[&](std::uint16_t at) { bus.fetch(at); std::swap(r.d,r.h); std::swap(r.e,r.l); bus.advance(4); };
    swap(0x297A);
    bus.fetch(0x297B); const auto source=word(0x297C); const auto low=bus.read(source), high=bus.read(source+1);
    r.l=low; r.h=high; bus.advance(16);
    bus.fetch(0x297E);
    const unsigned hl=(r.h<<8)|r.l, de=(r.d<<8)|r.e, sum=hl+de;
    r.f=static_cast<std::uint8_t>((r.f&0xc4)|((sum>>8)&0x28)|(((hl^de^sum)>>8)&0x10)|((sum>>16)&1));
    r.h=static_cast<std::uint8_t>(sum>>8); r.l=static_cast<std::uint8_t>(sum); bus.advance(11);
    bus.fetch(0x297F); (void)bus.read(0x2980);
    if(r.f&1) {
        bus.advance(7); bus.fetch(0x2981); const auto cap=word(0x2982);
        r.h=static_cast<std::uint8_t>(cap>>8); r.l=static_cast<std::uint8_t>(cap); bus.advance(10);
    } else bus.advance(12);
    bus.fetch(0x2984); const auto destination=word(0x2985);
    bus.write(destination,r.l); bus.write(destination+1,r.h); bus.advance(16);
    swap(0x2987);
}
}
void register_money_add(RoutineRegistry& registry) {
    registry.add(money_add_unit,SegmentSpec{money_add_unit,{{RoutineExit::returned,0x2988}}},
        [](const Machine& m) -> std::uint64_t {
            Estimate bus(m); auto r=load(m.cpu()); execute(bus,r);
            return bus.uncertain ? 78 : bus.cycles_+10;
        },
        [](RoutineContext& c) {
            Live bus(c); auto r=load(c.cpu()); execute(bus,r); store(c.cpu(),r); c.finish_return(0x2988);
        });
}
void register_battle_money_store(RoutineRegistry& registry,const Options& options) {
    const bool scaled=options.experience_percent!=experience_percent_choices.original;
    SegmentSpec spec{battle_setup_unit,{{RoutineExit::core_final,battle_money_store.address}},false,0,true,scaled};
    spec.absolute_wram_store=0xc2dd;
    registry.add(battle_money_store,spec,[](const Machine&) { return 16; },
        [percent=options.experience_percent,scaled](RoutineContext& c) {
            if (!scaled) { c.finish_core(battle_money_store.address); return; }
            auto& cpu=c.cpu(); const auto original=cpu.hl.uint16_value;
            const auto value=std::min<unsigned long long>(0xffff,
                static_cast<unsigned long long>(original)*percent/100);
            cpu.hl.uint16_value=static_cast<std::uint16_t>(value);
            c.finish_core(battle_money_store.address);
            cpu.hl.uint16_value=original;
        });
}
}
