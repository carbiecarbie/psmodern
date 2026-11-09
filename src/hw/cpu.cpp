// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/cpu.h"
#include "hw/initial_state.h"
namespace psmodern {
Cpu::Cpu(CpuBus& bus) : bus_(&bus) {
    bind(bus); state_.options=Z80_MODEL_ZILOG_NMOS; initial_state::cpu(state_);
}
Cpu::Cpu(const Cpu& other) : state_(other.state_), bus_(other.bus_), start_cycle_(other.start_cycle_) { bind(*bus_); }
Cpu& Cpu::operator=(const Cpu& other) {
    if (this != &other) { state_=other.state_; start_cycle_=other.start_cycle_; bind(*other.bus_); }
    return *this;
}
void Cpu::bind(CpuBus& bus) {
    bus_=&bus; state_.context=this; state_.fetch_opcode=opcode;
    state_.fetch=read; state_.read=read; state_.write=write;
    state_.in=input; state_.out=output;
}
std::uint64_t Cpu::step(std::uint64_t start_cycle) { start_cycle_=start_cycle; return z80_run(&state_,1); }
bool Cpu::equivalent(const Cpu& other) const {
    DifferenceList out; differences(other,out); return out.fields.empty();
}
void Cpu::differences(const Cpu& other, DifferenceList& out) const {
    const auto& a=state_; const auto& b=other.state_;
    // Z80.c:2962 replaces data[0] before dispatch; 2073/2080/2489 replace
    // data[1] before prefix use, 2113 replaces data[3], and 1142 writes
    // data[2] before its interrupt-delay use (2614-2616). After an unprefixed
    // instruction these three bytes are dead. Keep all bytes at prefixes
    // and resumptions. See routine_tests.cpp scratch_overwrite.
    auto prefix=[](std::uint8_t op) { return op==0xcb || op==0xed || (op&0xdf)==0xdd; };
    const bool data_live=a.resume || b.resume || prefix(a.data.uint8_array[0]) || prefix(b.data.uint8_array[0]);
    // xy is a temporary: Z80.c:2094,2488,2834 load the selected index before
    // use; 2144-2148 only save/restore its old, otherwise unused value.
    out.add("cpu.data[0]",a.data.uint8_array[0],b.data.uint8_array[0],true);
    if (data_live) for (unsigned i=1;i<4;++i)
        out.add("cpu.data["+std::to_string(i)+"]",a.data.uint8_array[i],b.data.uint8_array[i],true);
    out.add("cpu.cycles",a.cycles,b.cycles);
    out.add("cpu.cycle_limit",a.cycle_limit,b.cycle_limit);
    out.add("cpu.ix_iy[0]",a.ix_iy[0].uint16_value,b.ix_iy[0].uint16_value,true);
    out.add("cpu.ix_iy[1]",a.ix_iy[1].uint16_value,b.ix_iy[1].uint16_value,true);
    out.add("cpu.pc",a.pc.uint16_value,b.pc.uint16_value);
    out.add("cpu.sp",a.sp.uint16_value,b.sp.uint16_value);
    out.add("cpu.memptr",a.memptr.uint16_value,b.memptr.uint16_value);
    out.add("cpu.a",a.af.uint8_values.at_1,b.af.uint8_values.at_1,true);
    out.add("cpu.f",a.af.uint8_values.at_0,b.af.uint8_values.at_0);
    out.add("cpu.a_",a.af_.uint8_values.at_1,b.af_.uint8_values.at_1,true);
    out.add("cpu.f_",a.af_.uint8_values.at_0,b.af_.uint8_values.at_0);
    out.add("cpu.bc",a.bc.uint16_value,b.bc.uint16_value,true);
    out.add("cpu.bc_",a.bc_.uint16_value,b.bc_.uint16_value,true);
    out.add("cpu.de",a.de.uint16_value,b.de.uint16_value,true);
    out.add("cpu.de_",a.de_.uint16_value,b.de_.uint16_value,true);
    out.add("cpu.hl",a.hl.uint16_value,b.hl.uint16_value,true);
    out.add("cpu.hl_",a.hl_.uint16_value,b.hl_.uint16_value,true);
    out.add("cpu.r",a.r,b.r,true);
    out.add("cpu.r7",a.r7,b.r7,true);
    out.add("cpu.i",a.i,b.i,true);
    out.add("cpu.im",a.im,b.im);
    out.add("cpu.request",a.request,b.request);
    out.add("cpu.resume",a.resume,b.resume);
    out.add("cpu.iff1",a.iff1,b.iff1);
    out.add("cpu.iff2",a.iff2,b.iff2);
    out.add("cpu.q",a.q,b.q);
    out.add("cpu.options",a.options,b.options);
    out.add("cpu.int_line",a.int_line,b.int_line);
    out.add("cpu.halt_line",a.halt_line,b.halt_line);
    out.add("cpu.start_cycle",start_cycle_,other.start_cycle_);
}

zuint8 Cpu::opcode(void* p, zuint16 a) { return static_cast<Cpu*>(p)->bus_->opcode(a); }
zuint8 Cpu::read(void* p, zuint16 a) { return static_cast<Cpu*>(p)->bus_->read(a); }
void Cpu::write(void* p, zuint16 a, zuint8 v) { static_cast<Cpu*>(p)->bus_->write(a,v); }
// Hardware documentation: Zilog Z80 CPU User Manual UM008011-0816,
// Figure 7: the I/O M-cycle is T1, T2, TW, T3 and read data is taken in T3,
// before /RD goes inactive. Port accesses take effect at the start of T3,
// three T-states after M-cycle start; INT is sampled at the same point.
zuint8 Cpu::input(void* p, zuint16 a) {
    auto& c=*static_cast<Cpu*>(p);
    return c.bus_->input(a,c.start_cycle_ + c.state_.cycles + z80_in_cycle(&c.state_) + 3);
}
void Cpu::output(void* p, zuint16 a, zuint8 v) {
    auto& c=*static_cast<Cpu*>(p);
    c.bus_->output(a,v,c.start_cycle_ + c.state_.cycles + z80_out_cycle(&c.state_) + 3);
}
}
