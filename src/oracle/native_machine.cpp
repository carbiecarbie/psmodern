// SPDX-License-Identifier: GPL-3.0-or-later
#include "oracle/native_machine.h"
#include <Z80.h>
#include <iostream>
#include <tuple>

namespace psmodern::oracle {
// The pinned core's bus for one step of a NativeMachine. Port accesses take
// effect at the start of T3 of the I/O M-cycle, three T-states after the
// M-cycle starts (Zilog Z80 CPU User Manual UM008011-0816, Figure 7); INT is
// sampled at the same point.
struct CoreBus {
    Console& machine;
    std::uint64_t start;
    ::Z80 core{};
    static zuint8 fetch_opcode(void* p, zuint16 a) { return static_cast<CoreBus*>(p)->machine.opcode(a); }
    static zuint8 read(void* p, zuint16 a) { return static_cast<CoreBus*>(p)->machine.read(a); }
    static void write(void* p, zuint16 a, zuint8 v) { static_cast<CoreBus*>(p)->machine.write(a,v); }
    // A port access brings the hardware to its cycle, which can raise an NMI
    // (a Pause sample or a scheduled one) on the register file while the
    // core runs on its copy: the new request is handed to the core at once,
    // as the core saw it when it kept the register file itself.
    static zuint8 in(void* p, zuint16 port) {
        auto& bus=*static_cast<CoreBus*>(p);
        const auto before=bus.machine.cpu().request;
        const auto value=bus.machine.read_port(port,bus.start+bus.core.cycles+z80_in_cycle(&bus.core)+3);
        bus.core.request|=static_cast<zuint8>(bus.machine.cpu().request&~before);
        return value;
    }
    static void out(void* p, zuint16 port, zuint8 value) {
        auto& bus=*static_cast<CoreBus*>(p);
        const auto before=bus.machine.cpu().request;
        bus.machine.write_port(port,value,bus.start+bus.core.cycles+z80_out_cycle(&bus.core)+3);
        bus.core.request|=static_cast<zuint8>(bus.machine.cpu().request&~before);
    }
    // One z80_run(1) on the register file: every field the core keeps
    // between steps is a field of the register file, copied in and back.
    std::uint64_t run(Z80& r) {
        core.context=this; core.fetch_opcode=fetch_opcode; core.fetch=read; core.read=read; core.write=write;
        core.in=in; core.out=out; core.options=Z80_MODEL_ZILOG_NMOS;
        core.data.uint32_value=r.data.uint32_value;
        core.ix_iy[0].uint16_value=r.ix_iy[0].uint16_value; core.ix_iy[1].uint16_value=r.ix_iy[1].uint16_value;
        core.pc.uint16_value=r.pc.uint16_value; core.sp.uint16_value=r.sp.uint16_value;
        core.xy.uint16_value=r.xy.uint16_value; core.memptr.uint16_value=r.memptr.uint16_value;
        core.af.uint16_value=r.af.uint16_value; core.af_.uint16_value=r.af_.uint16_value;
        core.bc.uint16_value=r.bc.uint16_value; core.bc_.uint16_value=r.bc_.uint16_value;
        core.de.uint16_value=r.de.uint16_value; core.de_.uint16_value=r.de_.uint16_value;
        core.hl.uint16_value=r.hl.uint16_value; core.hl_.uint16_value=r.hl_.uint16_value;
        core.r=r.r; core.i=r.i; core.im=r.im; core.request=r.request; core.resume=r.resume;
        core.iff1=r.iff1; core.iff2=r.iff2; core.q=r.q; core.int_line=r.int_line; core.halt_line=r.halt_line;
        const auto cycles=z80_run(&core,1);
        r.data.uint32_value=core.data.uint32_value;
        r.ix_iy[0].uint16_value=core.ix_iy[0].uint16_value; r.ix_iy[1].uint16_value=core.ix_iy[1].uint16_value;
        r.pc.uint16_value=core.pc.uint16_value; r.sp.uint16_value=core.sp.uint16_value;
        r.xy.uint16_value=core.xy.uint16_value; r.memptr.uint16_value=core.memptr.uint16_value;
        r.af.uint16_value=core.af.uint16_value; r.af_.uint16_value=core.af_.uint16_value;
        r.bc.uint16_value=core.bc.uint16_value; r.bc_.uint16_value=core.bc_.uint16_value;
        r.de.uint16_value=core.de.uint16_value; r.de_.uint16_value=core.de_.uint16_value;
        r.hl.uint16_value=core.hl.uint16_value; r.hl_.uint16_value=core.hl_.uint16_value;
        r.r=core.r; r.i=core.i; r.im=core.im; r.request=core.request; r.resume=core.resume;
        r.iff1=core.iff1; r.iff2=core.iff2; r.q=core.q; r.int_line=core.int_line; r.halt_line=core.halt_line;
        return cycles;
    }
};
namespace {
// A late NMI edge must not be visible to the core until after this
// instruction (it missed the previous instruction's E-1 sample).
struct HideLateNmi {
    Z80& cpu;
    bool late;
    HideLateNmi(Z80& cpu,bool late) : cpu(cpu),late(late) { if (late) cpu.request &= static_cast<std::uint8_t>(~Z80::request_nmi); }
    ~HideLateNmi() { if (late) cpu.request |= Z80::request_nmi; }
};
}
std::uint64_t NativeMachine::step() {
    sync(cycle_);
    auto& cpu=cpu_;
    const bool unsampled_nmi=(cpu.request & Z80::request_nmi) && !sampled_nmi_;
    cpu.set_int_line(sampled_int_);
    std::uint64_t elapsed;
    {
        HideLateNmi mask(cpu,unsampled_nmi);
        CoreBus bus{*this,cycle_};
        elapsed=bus.run(cpu);
    }
    finish_interval(elapsed);
    return elapsed;
}
std::uint64_t native_step(Machine& engine) {
    CoreBus bus{engine,engine.cycles()};
    return bus.run(engine.cpu());
}
std::uint64_t NativeMachine::run_frame(const FrameInput& next_input) {
    pending_pads_=next_input.pads; pending_reset_=next_input.reset; pending_pause_=next_input.pause;
    const auto target=(cycle_/frame_cycles+1)*frame_cycles;
    while (cycle_<target || cpu_.resume==Z80::resume_xy) step();
    return cycle_-target;
}

namespace {
// The arbitration state of a boundary as the pinned core sees it once the
// boundary's INT sample is applied (z80_int at Z80.c 2380-2383).
struct Arbitration {
    std::uint8_t request;
    bool sampled_int, sampled_nmi, ei, retn, ld_a_ir;
    bool operator==(const Arbitration&) const = default;
};
Arbitration arbitration(const Console& m) {
    const auto& c=m.cpu();
    auto request=c.request;
    if (!m.sampled_int()) request&=static_cast<std::uint8_t>(~Z80::request_int);
    else if (c.iff1) request|=Z80::request_int;
    const auto* d=c.data.uint8_array;
    return {request,m.sampled_int(),m.sampled_nmi(),d[0]==0xfb,
        d[0]==0xed && (d[1]&0xc7)==0x45 && !(d[2]&1), d[0]==0xed && (d[1]&0xf7)==0x57};
}
auto registers(const Z80& c) {
    return std::tuple{c.pc.uint16_value,c.sp.uint16_value,c.af.uint16_value,c.bc.uint16_value,c.de.uint16_value,
        c.hl.uint16_value,c.af_.uint16_value,c.bc_.uint16_value,c.de_.uint16_value,c.hl_.uint16_value,
        c.ix_iy[0].uint16_value,c.ix_iy[1].uint16_value,c.memptr.uint16_value,c.i,c.r,c.iff1,c.iff2,c.im,
        c.q,c.halt_line,c.resume};
}
auto console(const Console& m) {
    return std::tuple{m.cycles(),m.opcode_fetches(),m.open_bus(),m.io_control(),m.pause_nmi_level()};
}

bool excluded(const std::string& field, Scope scope) {
    if (scope==Scope::boundary && !field.starts_with("cpu.") && !field.starts_with("machine.")) return true;
    for (const char* prefix:{"cpu.data","cpu.int_line","cpu.request","machine.sampled_int","machine.sampled_nmi",
                             "psg.writes","psg.samples","memory.cartridge_ram_dirty"})
        if (field.starts_with(prefix)) return true;
    return false;
}
}
bool equal(const Console& engine, const Console& oracle, Scope scope) {
    if (registers(engine.cpu())!=registers(oracle.cpu()) ||
        arbitration(engine)!=arbitration(oracle) || console(engine)!=console(oracle)) return false;
    if (scope==Scope::boundary) return true;
    // The remaining console fields (input latches, scheduled NMI) and the
    // chips: the per-field list is slow, so it runs on a mismatch only.
    if (!(Console::same_memory(engine.memory(),oracle.memory()) && engine.vdp()==oracle.vdp() &&
          Console::same_sound_state(engine.psg(),oracle.psg()))) return false;
    return engine.same_inputs(oracle);
}
std::vector<std::string> differences(const Console& engine, const Console& oracle, Scope scope) {
    std::vector<std::string> out;
    for (const auto& d:oracle.differences(engine)) {
        if (excluded(d.field,scope)) continue;
        out.push_back(d.field+": "+(d.can_carry_rom ? std::string("different (values redacted)") :
            "oracle="+d.original+", engine="+d.replacement));
    }
    const auto a=arbitration(oracle), b=arbitration(engine);
    auto add=[&](const char* name, unsigned x, unsigned y) {
        if (x!=y) out.push_back(std::string("arbitration.")+name+": oracle="+std::to_string(x)+", engine="+std::to_string(y));
    };
    add("request",a.request,b.request); add("sampled_int",a.sampled_int,b.sampled_int);
    add("sampled_nmi",a.sampled_nmi,b.sampled_nmi); add("after_ei",a.ei,b.ei);
    add("after_retn",a.retn,b.retn); add("after_ld_a_ir",a.ld_a_ir,b.ld_a_ir);
    return out;
}
std::vector<std::string> queue_differences(const Console& engine, const Console& oracle) {
    std::vector<std::string> out;
    if (engine.memory().cartridge_ram_dirty()!=oracle.memory().cartridge_ram_dirty())
        out.push_back("memory.cartridge_ram_dirty: oracle="+std::to_string(oracle.memory().cartridge_ram_dirty())+
                      ", engine="+std::to_string(engine.memory().cartridge_ram_dirty()));
    const auto& a=oracle.psg().writes(); const auto& b=engine.psg().writes();
    if (a!=b) out.push_back("psg.writes: oracle "+std::to_string(a.size())+" waiting, engine "+std::to_string(b.size())+
                            " waiting, different");
    return out;
}
bool same_machine(const Console& engine, const Console& oracle) {
    const bool state=equal(engine,oracle,Scope::whole);
    auto lines=queue_differences(engine,oracle);
    if (state && lines.empty()) return true;
    if (!state) for (auto& line:differences(engine,oracle,Scope::whole)) lines.push_back(std::move(line));
    for (const auto& line:lines) std::cerr<<line<<'\n';
    return false;
}
}
