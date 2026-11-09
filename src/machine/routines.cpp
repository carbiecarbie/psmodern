// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/routines.h"
#include "machine/machine.h"
#include <stdexcept>
#include <utility>

namespace psmodern {
void RoutineRegistry::add(RoutineEntry entry, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body) {
    if (entry.bank>=32 || entry.address>=0xc000 || !bound || !body)
        throw std::invalid_argument("Invalid registered routine");
    if (!routines_.emplace(entry,RegisteredRoutine{std::move(bound),std::move(body),{}}).second)
        throw std::invalid_argument("Duplicate registered routine");
}
RegisteredRoutine* RoutineRegistry::find(RoutineEntry entry) {
    auto it=routines_.find(entry); return it==routines_.end() ? nullptr : &it->second;
}
const RegisteredRoutine* RoutineRegistry::find(RoutineEntry entry) const {
    auto it=routines_.find(entry); return it==routines_.end() ? nullptr : &it->second;
}
RoutineContext::RoutineContext(Machine& m, std::uint64_t bound)
    : machine_(m), entry_(m.cycles()), bound_(bound), stack_(m.cpu().sp.uint16_value) {}
void RoutineContext::active() const {
    if (finished_) throw std::logic_error("Registered routine already returned");
}
void RoutineContext::check_bound() const {
    if (elapsed()>bound_) throw std::runtime_error("Registered routine exceeded its declared cycle bound");
}
Z80& RoutineContext::cpu() { active(); return machine_.cpu(); }
std::uint64_t RoutineContext::elapsed() const { return machine_.cycles()-entry_; }
void RoutineContext::advance(std::uint64_t cycles) {
    active();
    if (elapsed()>bound_ || cycles>bound_-elapsed())
        throw std::runtime_error("Registered routine exceeded its declared cycle bound");
    machine_.advance(cycles);
}
std::uint8_t RoutineContext::read(std::uint16_t address) { active(); return machine_.read(address); }
void RoutineContext::write(std::uint16_t address, std::uint8_t value) {
    active();
    if (address>=0xfffc) throw std::runtime_error("Registered routine cannot write the mapper");
    machine_.write(address,value);
}
void RoutineContext::account_fetches(std::uint64_t count) {
    active();
    auto& r=machine_.cpu().r;
    r=static_cast<std::uint8_t>((r&0x80)|((r+count)&0x7f));
    machine_.opcode_fetches_+=count;
}
std::uint8_t RoutineContext::fetch_opcode(std::uint16_t address) {
    const auto value=read(address); account_fetches(1); return value;
}
void RoutineContext::at_offset(std::uint64_t offset) {
    active();
    if (offset<elapsed()) throw std::invalid_argument("Registered routine port offset is in the past");
    advance(offset-elapsed());
}
std::uint8_t RoutineContext::read_port(std::uint16_t port, std::uint64_t offset) {
    at_offset(offset); return machine_.read_port(port,machine_.cycles());
}
void RoutineContext::write_port(std::uint16_t port, std::uint8_t value, std::uint64_t offset) {
    active();
    const auto low=static_cast<std::uint8_t>(port);
    if (low<0x40 && !(low&1)) throw std::runtime_error("Registered routine cannot change memory mapping");
    if (low>=0x80 && low<0xc0 && (low&1)) {
        auto reg=machine_.vdp().control_write_register(value);
        if (reg && (*reg==0 || *reg==1 || *reg==10))
            throw std::runtime_error("Registered routine cannot write an interrupt control register");
    }
    at_offset(offset); machine_.write_port(port,value,machine_.cycles());
}
void RoutineContext::finish_return(std::uint16_t address) {
    active();
    // Validate the instruction without bus side effects; the core performs all
    // real fetches and stack reads below. Ignored index prefixes are supported.
    auto opcode_address=address;
    auto peek=[&](std::uint16_t pc) {
        if (machine_.memory().mapping(pc).kind!=Memory::Kind::rom)
            throw std::runtime_error("Registered routine return must be in ROM");
        return machine_.memory().read(pc);
    };
    auto code=peek(opcode_address);
    std::uint64_t prefixes=0;
    while ((code&0xdf)==0xdd) {
        if (++prefixes>bound_/4) throw std::runtime_error("Registered routine return exceeds bound");
        code=peek(++opcode_address);
    }
    bool returning=code==0xc9;
    if ((code&0xc7)==0xc0) {
        constexpr unsigned bits[]{6,0,2,7};
        const auto condition=(code>>3)&7;
        const auto flag=(machine_.cpu().af.uint16_value>>bits[condition/2])&1;
        returning=flag==(condition&1);
    }
    if (!returning || machine_.cpu().sp.uint16_value!=stack_)
        throw std::runtime_error("Registered routine must execute its returning instruction with the entry stack");
    machine_.cpu().pc.uint16_value=address;
    do { machine_.step(); check_bound(); } while (machine_.cpu().resume==Z80_RESUME_XY);
    // The validated taken return performs the real bus reads, including open
    // bus if the stack is unmapped. Peeking Memory here would predict a
    // different target in that case; the core alone determines the caller PC.
    if (machine_.cpu().sp.uint16_value!=static_cast<std::uint16_t>(stack_+2))
        throw std::runtime_error("Registered routine did not return to its caller");
    finished_=true;
}
}
