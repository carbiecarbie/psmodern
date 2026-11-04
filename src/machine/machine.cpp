// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include <stdexcept>

namespace psmodern {
Machine::Machine(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram)
    : memory_(std::move(rom), ram), cpu_(*this) {}
Machine::Machine(const Machine& o)
    : memory_(o.memory_), cpu_(o.cpu_), cycle_(o.cycle_), opcode_fetches_(o.opcode_fetches_), scheduled_nmi_(o.scheduled_nmi_),
      pads_(o.pads_), io_control_(o.io_control_), open_bus_(o.open_bus_), reset_(o.reset_), pause_(o.pause_),
      int_(o.int_), accesses_(o.accesses_) { bind(); }
Machine& Machine::operator=(const Machine& o) {
    if (this != &o) {
        memory_=o.memory_; cpu_=o.cpu_; cycle_=o.cycle_; opcode_fetches_=o.opcode_fetches_; scheduled_nmi_=o.scheduled_nmi_;
        pads_=o.pads_; io_control_=o.io_control_; open_bus_=o.open_bus_; reset_=o.reset_; pause_=o.pause_;
        int_=o.int_; accesses_=o.accesses_; bind();
    }
    return *this;
}
void Machine::bind() { cpu_.bind(*this); }
bool Machine::equivalent(const Machine& o) const {
    return memory_==o.memory_ && cpu_.equivalent(o.cpu_) && cycle_==o.cycle_ &&
        opcode_fetches_==o.opcode_fetches_ && scheduled_nmi_==o.scheduled_nmi_ &&
        pads_==o.pads_ && io_control_==o.io_control_ && open_bus_==o.open_bus_ && reset_==o.reset_ &&
        pause_==o.pause_ && int_==o.int_ && accesses_==o.accesses_;
}
void Machine::set_int(bool level) { int_=level; z80_int(&cpu_.raw(), level); }
void Machine::set_pause(bool level) { if (level && !pause_) z80_nmi(&cpu_.raw()); pause_=level; }
void Machine::schedule_nmi(std::uint64_t cycle) {
    if (cycle < cycle_) throw std::invalid_argument("NMI is in the past");
    scheduled_nmi_=cycle;
}
std::optional<std::uint64_t> Machine::next_nmi() const {
    if (cpu_.raw().request & Z80_REQUEST_NMI) return cycle_;
    return scheduled_nmi_;
}
void Machine::deliver_nmi() {
    if (scheduled_nmi_ && *scheduled_nmi_ <= cycle_) { z80_nmi(&cpu_.raw()); scheduled_nmi_.reset(); }
}
std::uint64_t Machine::step(const Hook& hook) {
    deliver_nmi();
    z80_int(&cpu_.raw(), int_);
    const auto request = cpu_.raw().request;
    const bool reject_nmi = (request & Z80_REQUEST_REJECT_NMI) != 0;
    const bool nmi = !reject_nmi && (request & Z80_REQUEST_NMI);
    const bool reti_delay = cpu_.raw().data.uint8_array[0] == 0xed &&
        (cpu_.raw().data.uint8_array[1] & 0xc7) == 0x45 && (cpu_.raw().data.uint8_array[2] & 1);
    const bool irq = !reject_nmi && !nmi && (request & Z80_REQUEST_INT) &&
        cpu_.raw().data.uint8_array[0] != 0xfb && !reti_delay;
    if (cpu_.raw().resume == 0 && !nmi && !irq && hook) {
        const auto pc=cpu_.raw().pc.uint16_value;
        if (auto target=hook(pc, memory_.mapping(pc))) cpu_.raw().pc.uint16_value=*target;
    }
    const auto elapsed=cpu_.step(cycle_);
    cycle_ += elapsed;
    deliver_nmi();
    return elapsed;
}
std::uint8_t Machine::opcode(std::uint16_t addr) { ++opcode_fetches_; return read(addr); }
std::uint8_t Machine::read(std::uint16_t addr) {
    if (memory_.mapping(addr).kind != Memory::Kind::unmapped) open_bus_=memory_.read(addr);
    return open_bus_;
}
void Machine::write(std::uint16_t addr, std::uint8_t value) { memory_.write(addr,value); open_bus_=value; }
std::uint8_t Machine::input(std::uint16_t addr, std::uint64_t cycle) { return read_port(addr,cycle); }
void Machine::output(std::uint16_t addr, std::uint8_t value, std::uint64_t cycle) { write_port(addr,value,cycle); }
std::uint8_t Machine::read_port(std::uint16_t raw, std::uint64_t cycle) {
    const auto port=static_cast<std::uint8_t>(raw);
    if (port < 0x40) return open_bus_;
    if (port < 0xc0) { accesses_.push_back({cycle,port,0xff,false}); return 0xff; }
    if (!memory_.io_enabled()) return open_bus_;
    auto low=[](bool pressed) -> unsigned { return pressed ? 0 : 1; };
    auto pin=[&](unsigned direction, unsigned output, bool input) {
        return (io_control_ & (1u << direction)) ? low(input) : ((io_control_ >> output) & 1u);
    };
    auto& a=pads_[0]; auto& b=pads_[1];
    if (!(port & 1)) return low(a.up) | (low(a.down)<<1) | (low(a.left)<<2) |
        (low(a.right)<<3) | (low(a.one)<<4) | (pin(0,4,a.two)<<5) |
        (low(b.up)<<6) | (low(b.down)<<7);
    return low(b.left) | (low(b.right)<<1) | (low(b.one)<<2) | (pin(2,6,b.two)<<3) |
        (low(reset_)<<4) | 0x20 | (pin(1,5,false)<<6) | (pin(3,7,false)<<7);
}
void Machine::write_port(std::uint16_t raw, std::uint8_t value, std::uint64_t cycle) {
    const auto port=static_cast<std::uint8_t>(raw);
    if (port < 0x40) {
        if (port & 1) io_control_=value;
        else memory_.set_memory_control(value);
    } else if (port < 0xc0) accesses_.push_back({cycle,port,value,true});
}
}
