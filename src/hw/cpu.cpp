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
    const auto& a=state_; const auto& b=other.state_;
    return a.cycles==b.cycles && a.cycle_limit==b.cycle_limit &&
        a.data.uint32_value==b.data.uint32_value &&
        a.ix_iy[0].uint16_value==b.ix_iy[0].uint16_value && a.ix_iy[1].uint16_value==b.ix_iy[1].uint16_value &&
        a.pc.uint16_value==b.pc.uint16_value && a.sp.uint16_value==b.sp.uint16_value &&
        a.xy.uint16_value==b.xy.uint16_value && a.memptr.uint16_value==b.memptr.uint16_value &&
        a.af.uint16_value==b.af.uint16_value && a.af_.uint16_value==b.af_.uint16_value &&
        a.bc.uint16_value==b.bc.uint16_value && a.bc_.uint16_value==b.bc_.uint16_value &&
        a.de.uint16_value==b.de.uint16_value && a.de_.uint16_value==b.de_.uint16_value &&
        a.hl.uint16_value==b.hl.uint16_value && a.hl_.uint16_value==b.hl_.uint16_value &&
        a.r==b.r && a.r7==b.r7 && a.i==b.i && a.im==b.im && a.request==b.request &&
        a.resume==b.resume && a.iff1==b.iff1 && a.iff2==b.iff2 && a.q==b.q &&
        a.options==b.options && a.int_line==b.int_line && a.halt_line==b.halt_line && start_cycle_==other.start_cycle_;
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
