// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace psmodern;
namespace {
void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
std::shared_ptr<const Memory::Rom> program(std::initializer_list<std::uint8_t> code) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    std::copy(code.begin(),code.end(),rom->begin());
    return rom;
}
void startup_state() {
    Machine m(program({0})); const auto& c=m.cpu();
    check(c.pc.uint16_value==0 && c.sp.uint16_value==0xdff0 && c.af.uint16_value==0xab42 &&
          c.bc.uint16_value==2 && c.de.uint16_value==0xa51b && c.hl.uint16_value==0,"initial CPU primary registers");
    check(c.af_.uint16_value==0 && c.bc_.uint16_value==0x0200 && c.de_.uint16_value==0xc700 &&
          c.hl_.uint16_value==0xc75f && c.ix_iy[0].uint16_value==0 && c.ix_iy[1].uint16_value==0,
          "initial CPU alternate and index registers");
    check(c.i==0 && c.r==0x1d && c.iff1==0 && c.iff2==0 && c.im==1 && c.memptr.uint16_value==0,
          "initial interrupt and refresh state");
    check(m.cycles()==0 && m.vdp().dot()==1 && m.vdp().irq() && c.int_line,
          "initial VDP phase and IRQ");
    check(m.memory().memory_control()==0xab && m.memory().read(0xc000)==0xab &&
          m.memory().read(0xc001)==0 && m.vdp().cram()==initial_state::cram,"initial memory and palette");
}
void timing() {
    Machine m(program({0x3e,0x42,0xd3,0x7f,0xdb,0xbe,0xdd,0xd3,0x7f}));
    check(m.step()==7,"LD duration");
    check(m.step()==11,"OUT duration");
    // Port accesses happen at T3 of the I/O M-cycle: M-cycle start plus three.
    check(m.last_access()==PortAccess{17,0x7f,0x42,true},"OUT at T3");
    check(m.step()==11,"IN duration");
    check(m.last_access()==PortAccess{28,0xbe,0,false},"IN at T3");
    check(m.step()==4 && m.cpu().resume==Z80_RESUME_XY,"prefix resume");
    check(m.step()==11 && m.cpu().resume==0,"prefixed OUT");
    check(m.last_access()==PortAccess{43,0x7f,0,true},"prefixed OUT at T3");
    Machine block(program({0xed,0x41,0xed,0xa3,0xed,0xa2}));
    block.cpu().bc.uint16_value=0x017f; block.cpu().hl.uint16_value=0xc100;
    block.memory().write(0xc100,0x5a);
    check(block.step()==12 && block.last_access()==PortAccess{11,0x7f,1,true},"OUT (C) at T3");
    check(block.step()==16 && block.last_access()==PortAccess{27,0x7f,0x5a,true},"OUTI at T3");
    check(block.step()==16 && block.last_access()==PortAccess{40,0x7f,0,false},"INI at T3");
}
void ports() {
    Machine m(program({0}));
    m.set_pad(0,{true,false,true,false,true,true});
    m.set_pad(1,{false,true,false,true,false,true}); m.set_reset(true);
    check(m.read_port(0xdc,0)==0x4a,"pad A and B");
    check(m.read_port(0xdd,0)==0xe5,"pad B and reset");
    m.write_port(0x3f,0,0);
    check((m.read_port(0xdc,0)&0x20)==0 && (m.read_port(0xdd,0)&0xc8)==0,"outputs low");
    m.write_port(0x01,0xf0,0);
    check((m.read_port(0xc0,0)&0x20)!=0 && (m.read_port(0xff,0)&0xc8)==0xc8,"output mirrors");
    m.write_port(0x3e,0x04,0);
    check(m.read_port(0xdc,0)==0 && m.read_port(0xdd,0)==0,"I/O disabled returns open bus");
    m.write_port(0x3f,0,0);
    m.write_port(0x00,0,0);
    check(m.read_port(0xc0,0)!=0xff,"I/O enabled by mirror");
    check((m.read_port(0xdd,0)&0xc8)==0,"I/O control remains writable while inputs disabled");
    for (int p=0x40;p<0x80;++p) {
        m.write_port(p,0x55,0);
        check(m.read_port(p,0)==m.read_port(0x7e + (p&1),0),"counter mirrors");
    }
    for (int p=0xc0;p<=0xff;++p) check(m.read_port(p,0)==m.read_port(0xdc+(p&1),0),"controller mirrors");
    for (int p=0;p<0x40;p+=2) { m.write_port(p,0,0); check(m.memory().memory_control()==0,"memory-control mirrors"); }
    for (int p=1;p<0x40;p+=2) { m.write_port(p,0xf0,0); check((m.read_port(0xdd,0)&0xc8)==0xc8,"I/O control mirrors"); }
    Machine open(program({0xdb,0x3e}));
    open.step(); check((open.cpu().af.uint16_value>>8)==0x3e,"SMS1 I/O open bus follows memory access");
}
void vdp_control() {
    Vdp v;
    check(v.irq(),"initial VBlank IRQ");
    check(v.read_status(0x1b)==0xdb && !v.irq(),"initial status and clear");
    check(v.read_status(0x05)==0x05,"status flags cleared");
    auto command=[&](unsigned address,unsigned code) {
        v.write_control(address&255); v.write_control((code<<6)|(address>>8));
    };
    command(0x3fff,1); v.write_data(0x6a); v.write_data(0xb1);
    command(0x3fff,0);
    check(v.read_data()==0x6a && v.read_data()==0xb1,"prefetch and address wrap");
    command(0x20,1); v.write_data(0x92); check(v.read_data()==0x92,"data write updates buffer");
    command(0x3fff,3); v.write_data(0xff); v.write_data(0xc3);
    check(v.cram()[31]==0x3f && v.cram()[0]==3,"CRAM masking and wrap");
    v.write_control(0x99); v.read_data(); v.write_control(0x55); v.write_control(0x80);
    check(v.registers()[0]==0x55,"data read resets control latch");
    v.write_control(0x99); v.write_data(0); v.write_control(0x66); v.write_control(0x81);
    check(v.registers()[1]==0x66,"data write resets control latch");
    v.write_control(0x99); v.read_status(0); v.write_control(0x77); v.write_control(0x82);
    check(v.registers()[2]==0x77,"status read resets control latch");
    auto regs=v.registers();
    for (unsigned reg=11;reg<16;++reg) {
        v.write_control(0x33); v.write_control(0x80|reg);
        check(v.registers()==regs,"unknown register ignored");
    }
    command(0x0100,0); v.write_data(0x4b);
    check(v.vram()[0x0101]==0x4b,"code zero writes VRAM after prefetch");
    command(0x0200,2); v.write_data(0x2c);
    check(v.vram()[0x0200]==0x2c,"code two data write targets VRAM");
}
void vdp_timing() {
    Vdp v; v.read_status(0);
    v.write_control(0); v.write_control(0x80); // line IRQ off
    auto next=v.next_irq(); check(next==43986,"frame IRQ query");
    v.sync(*next-1); check(!v.irq(),"frame IRQ not early");
    auto early=v;
    check(!(early.read_status(0)&0x80) && early.v_counter()==0xc0,
          "frame bit and V counter unchanged one cycle before the frame event");
    v.sync(*next); check(v.irq() && v.v_counter()==0xc1,"frame IRQ and V counter change together");
    auto both=v;
    check((both.read_status(0)&0x80) && !both.irq(),"frame bit readable with the pending IRQ");
    early.sync(*next); check(early.irq(),"status read before the event does not cancel it");
    check(v.read_status(0)&0x80,"frame status bit");
    check(!v.irq(),"status drops IRQ");
    // The 64 zero sprite entries all cover line 1; overflow uses the same point.
    Vdp overflow; overflow.read_status(0);
    overflow.sync(209); auto before_overflow=overflow;
    check(!(before_overflow.read_status(0)&0x40) && before_overflow.v_counter()==0,
          "overflow and V counter unchanged one cycle before the event");
    overflow.sync(210);
    check((overflow.read_status(0)&0x40) && overflow.v_counter()==1,
          "overflow and V counter change at the frame-event point");
    v.write_control(0); v.write_control(0x81); v.sync(2*Machine::frame_cycles+43986);
    check(!v.irq(),"disabled pending frame");
    v.write_control(0x20); v.write_control(0x81); check(v.irq(),"enable pending frame");
    Vdp disabled; disabled.read_status(0);
    disabled.write_control(0); disabled.write_control(0x80);
    disabled.write_control(0); disabled.write_control(0x81);
    check(!disabled.next_irq(),"no future IRQ with both enables off");
    disabled.sync(43986); check(!disabled.irq(),"disabled frame IRQ remains pending");
    disabled.write_control(0x20); disabled.write_control(0x81);
    check(disabled.irq() && disabled.next_irq()==43986,"enable asserts an already pending IRQ");
    check((disabled.read_status(0)&0x80) && !disabled.irq(),"status read returns and clears a pending frame bit");
    Vdp counters; counters.sync(218*228-16); check(counters.v_counter()==0xda,"V counter DA");
    counters.sync(219*228-16); check(counters.v_counter()==0xd5,"V counter jump");
    Vdp line; line.read_status(0); line.write_control(0); line.write_control(0x81);
    line.write_control(0); line.write_control(0x8a); // R10 updates on next reload
    line.sync(261*228+211); line.read_status(0);
    line.sync(Machine::frame_cycles+211); check(line.irq(),"line counter underflow");
    line.read_status(0); check(!line.irq(),"line status clear");
    Vdp reload; reload.read_status(0);
    reload.write_control(1); reload.write_control(0x8a);
    reload.sync(193*228+210); auto old=reload.line_counter();
    reload.write_control(5); reload.write_control(0x8a);
    check(reload.line_counter()==old,"R10 does not immediately alter counter");
    reload.sync(193*228+211); check(reload.line_counter()==5,"R10 written before reload event");
    reload.write_control(7); reload.write_control(0x8a);
    check(reload.line_counter()==5,"R10 written after reload waits");
    reload.sync(194*228+210); check(reload.line_counter()==5,"counter unchanged before next reload");
    reload.sync(194*228+211); check(reload.line_counter()==7,"counter reload uses new R10");
    // The line event follows the frame event by one CPU cycle within a line.
    Vdp order; order.read_status(0);
    order.write_control(0x10); order.write_control(0x80);
    order.write_control(0); order.write_control(0x81);
    order.write_control(0); order.write_control(0x8a);
    order.sync(200*228); check(order.next_irq()==261*228+211,"line IRQ query one cycle after frame point");
    order.sync(261*228+210); check(!order.irq(),"line IRQ not at the frame-event cycle");
    order.sync(261*228+211); check(order.irq(),"line IRQ one cycle after the frame-event cycle");
    // A latch at CPU cycle c reads dot floor(1+1.5c)+3 within the line.
    Vdp hc; check(hc.h_counter()==0,"H latch power-on"); hc.sync(209); check(hc.h_counter()==0,"H read remains latched");
    hc.latch_h_counter(); check(hc.h_counter()==0xf3,"H count F3 one cycle before the frame event");
    hc.sync(210); hc.latch_h_counter(); check(hc.h_counter()==0xf4,"H count F4 at the frame event");
    hc.sync(211); hc.latch_h_counter(); check(hc.h_counter()==0xf5,"H count F5 at the line event");

    Vdp sequence;
    auto map_v=[](unsigned line) { return static_cast<std::uint8_t>(line<=0xda ? line : line-6); };
    for (unsigned physical=0;physical<262;++physical) {
        sequence.sync(physical*228+209);
        check(sequence.v_counter()==map_v(physical),"V counter before transition");
        sequence.sync(physical*228+210);
        check(sequence.v_counter()==map_v((physical+1)%262),"V counter at transition");
    }
    Vdp all_lines; all_lines.read_status(0);
    all_lines.write_control(0x10); all_lines.write_control(0x80);
    all_lines.write_control(0); all_lines.write_control(0x81);
    all_lines.write_control(3); all_lines.write_control(0x8a);
    unsigned counter=0xfc;
    for (unsigned physical=0;physical<262;++physical) {
        all_lines.sync(physical*228+210);
        check(all_lines.line_counter()==counter,"line counter before event");
        bool pending=false;
        if (physical<192 || physical==261) {
            if (counter==0) { counter=3; pending=true; } else --counter;
        } else counter=3;
        all_lines.sync(physical*228+211);
        check(all_lines.line_counter()==counter && all_lines.irq()==pending,"line counter at event");
        all_lines.read_status(0);
    }
}
void irq_boundaries() {
    auto prepare=[](Machine& m) {
        m.read_port(0xbf,0); m.write_port(0xbf,0,0); m.write_port(0xbf,0x80,0);
        m.cpu().iff1=m.cpu().iff2=1; m.cpu().sp.uint16_value=0xdff0;
        m.advance(0);
    };
    Machine before(program({0})); prepare(before); auto when=before.next_irq(); check(when==43986,"query assertion time");
    before.advance(*when-3); int hooks=0;
    auto hook=[&](std::uint16_t,Memory::Mapping)->std::optional<std::uint16_t>{ ++hooks; return {}; };
    before.step(hook); check(hooks==1,"hook before pending IRQ");
    check(before.cycles()==*when+1,"next-IRQ query predicts the sampled assertion");
    before.step(hook); check(hooks==1 && before.cpu().pc.uint16_value==0x38,"IRQ at E-1 suppresses hook");
    Machine at(program({0})); prepare(at); at.advance(*when-4);
    at.step(); check(at.cycles()==*when,"IRQ at E boundary");
    at.step(); check(at.cpu().pc.uint16_value==2,"IRQ at E waits for next sample");
    at.step(); check(at.cpu().pc.uint16_value==0x38,"IRQ accepted after E sample");
    Machine clear(program({0xdb,0xbf,0})); prepare(clear); clear.advance(*when-8);
    clear.step(); check(!clear.vdp().irq(),"status read clears new IRQ");
    clear.step(); check(clear.cpu().pc.uint16_value==3,"status read suppresses acceptance");
    Machine loop(program({0})), advanced(program({0}));
    for (int i=0;i<30000;++i) loop.step();
    advanced.advance(120000);
    check(loop.vdp()==advanced.vdp(),"NOP loop and advance VDP state");
    Machine no_cpu(program({0})); no_cpu.read_port(0xbf,0); no_cpu.advance(0);
    check(!no_cpu.cpu().int_line,"advance updates deasserted INT");
    no_cpu.advance(43986); check(no_cpu.vdp().irq() && no_cpu.cpu().int_line,"advance updates asserted INT");
}
void input_frames() {
    Machine m(program({0})); m.set_pad(0,{true});
    check(!(m.read_port(0xdc,0)&1),"frame zero input");
    m.advance(1000); m.set_pad(0,{}); m.set_reset(true); m.set_pause(true);
    check(m.next_nmi()==Machine::frame_cycles+Machine::pause_sample_offset,
          "query predicts a mid-frame Pause after the next latch");
    check(!(m.read_port(0xdc,m.cycles())&1) && (m.read_port(0xdd,m.cycles())&0x10),
          "mid-frame input is not yet latched");
    m.advance(Machine::frame_cycles-m.cycles());
    check((m.read_port(0xdc,m.cycles())&1) && !(m.read_port(0xdd,m.cycles())&0x10),
          "pad and reset latch at frame start");
    check(m.next_nmi()==Machine::frame_cycles+Machine::pause_sample_offset,
          "Pause latches at the boundary and schedules its hardware sample");
    Machine f(program({0})); auto overshoot=f.run_frame({});
    check(overshoot<4 && f.cycles()==Machine::frame_cycles+overshoot,"frame run and overshoot");
    Machine framed(program({0}));
    FrameInput pressed; pressed.pads[0].one=true; pressed.reset=true;
    check(framed.read_port(0xdc,0)&0x10,"frame zero starts released");
    framed.run_frame(pressed);
    check(!(framed.read_port(0xdc,framed.cycles())&0x10) &&
          !(framed.read_port(0xdd,framed.cycles())&0x10),
          "run_frame supplies input for its ending boundary");
    framed.run_frame({});
    check((framed.read_port(0xdc,framed.cycles())&0x10) &&
          (framed.read_port(0xdd,framed.cycles())&0x10),
          "following frame input replaces previous state");
    Machine prefix(program({0xdd,0})); prefix.advance(Machine::frame_cycles-2);
    check(prefix.run_frame({})==6 && prefix.cpu().resume==0 && prefix.cpu().pc.uint16_value==2,
          "frame run completes an instruction spanning its boundary");
    Machine h(program({0}));
    h.write_port(0x3f,0,0); h.advance(210);
    check(h.read_port(0x7f,h.cycles())==0,"H latch retains old value");
    h.write_port(0x3f,0x20,h.cycles());
    check(h.read_port(0x41,h.cycles())==0xf4,"TH rising edge latches H counter");
    h.advance(2); check(h.read_port(0x7f,h.cycles())==0xf4,"H latch remains stable");
    h.write_port(0x3f,0,h.cycles()); h.write_port(0x3f,0x20,h.cycles());
    check(h.read_port(0x7f,h.cycles())==0xf6,"next TH rising edge updates H counter");
    // The latch reads three dots after the TH write: cycle 34 is dot 52 and
    // cycle 35 is dot 53, so a delay of two or four dots would fail one check.
    Machine edge(program({0}));
    edge.write_port(0x3f,0,0); edge.write_port(0x3f,0x20,34);
    check(edge.read_port(0x7f,34)==0x1b,"TH at dot 52 latches dot 55");
    edge.write_port(0x3f,0,35); edge.write_port(0x3f,0x20,35);
    check(edge.read_port(0x7f,35)==0x1c,"TH at dot 53 latches dot 56");
    // LD A,0; OUT (3F),A; LD A,20; OUT (3F),A; IN A,(7F): the second OUT's
    // T3 is cycle 35 (dot 53), so the latch holds dot 56.
    Machine out(program({0x3e,0x00,0xd3,0x3f,0x3e,0x20,0xd3,0x3f,0xdb,0x7f}));
    out.step(); out.step(); out.step(); out.step();
    check(out.last_access()==PortAccess{35,0x3f,0x20,true},"TH OUT at T3");
    out.step(); check((out.cpu().af.uint16_value>>8)==0x1c,"H latch three dots after a T3 OUT");
}
void cpu() {
    Machine m(program({0xdd,0x21,0x34,0x12,0x76}));
    m.cpu().r=0xfe;
    int hooks=0;
    auto hook=[&](std::uint16_t pc, Memory::Mapping loc)->std::optional<std::uint16_t> {
        ++hooks; check(loc.kind==Memory::Kind::rom && loc.bank==0,"hook mapping");
        return pc==4 ? std::optional<std::uint16_t>(0x20) : std::nullopt;
    };
    check(m.step(hook)==4 && hooks==1,"prefix first half");
    Machine snap=m;
    check(m.step(hook)==10 && hooks==1,"prefix resume excludes hook");
    check(snap.step()==10 && snap.cpu().ix_iy[0].uint16_value==m.cpu().ix_iy[0].uint16_value,"prefix snapshot");
    check(m.cpu().r==0x80,"prefix R bit seven");
    auto before=m.cycles();
    auto fetches=m.opcode_fetches();
    check(m.step(hook)==4 && m.cpu().pc.uint16_value==0x21 && m.cpu().r==0x81 && m.cycles()==before+4,"redirect");
    check(m.opcode_fetches()==fetches+1,"redirect does not fetch twice");
    Machine indexed(program({0xdd,0xcb,0x00,0x46})); indexed.cpu().ix_iy[0].uint16_value=0xc000;
    check(indexed.step()==4 && indexed.step()==16 && indexed.cpu().r==0x1f,"indexed CB counts only M1 fetches");
    Machine halt(program({0x76}));
    check(halt.step()==4 && halt.cpu().resume==Z80_RESUME_HALT,"HALT");
    check(halt.step()==4 && halt.cpu().r==0x1f,"HALT increments R");
    halt.cpu().sp.uint16_value=0xdff0; halt.set_pause(true);
    halt.advance(Machine::frame_cycles-halt.cycles()+Machine::pause_sample_offset);
    check(halt.step()==11 && halt.cpu().pc.uint16_value==0x66 && halt.cpu().r==0x20,"NMI exits HALT");
    z80_power(&halt.cpu(),1); check(halt.cpu().r==0,"reset R");
    Machine halt_irq(program({0x76}));
    halt_irq.read_port(0xbf,0);
    halt_irq.advance(0);
    halt_irq.cpu().iff1=halt_irq.cpu().iff2=1;
    check(halt_irq.step()==4 && halt_irq.cpu().resume==Z80_RESUME_HALT,"IRQ HALT setup");
    halt_irq.advance(43986-halt_irq.cycles());
    check(halt_irq.step()==13 && halt_irq.cpu().pc.uint16_value==0x38,
          "IRQ exits HALT without an extra idle M1");
}
void interrupts() {
    Machine m(program({0xfb,0x00,0x00}));
    m.cpu().im=1; m.cpu().sp.uint16_value=0xdff0;
    check(m.step()==4 && m.cpu().iff1,"EI");
    check(m.step()==4 && m.cpu().pc.uint16_value==2,"EI delay");
    check(m.step()==13 && m.cpu().pc.uint16_value==0x38 && m.cpu().r==0x20,"IM1 acceptance");
    int calls=0;
    auto redirect=[&](std::uint16_t pc, Memory::Mapping) -> std::optional<std::uint16_t> {
        ++calls; check(pc==0x38,"hook targets ISR after IRQ"); return 0x42;
    };
    auto fetches=m.opcode_fetches(); auto old_r=m.cpu().r; auto old_cycle=m.cycles();
    check(m.step(redirect)==4 && calls==1 && m.cpu().pc.uint16_value==0x43,"interrupt redirect");
    check(m.opcode_fetches()==fetches+1 && m.cpu().r==old_r+1 && m.cycles()==old_cycle+4,"interrupt redirect costs only instruction");
    Machine q(program({0})); q.cpu().sp.uint16_value=0xdff0; q.cpu().im=1; q.cpu().iff1=1;
    int rejected_hook=0;
    auto pending=[&](std::uint16_t, Memory::Mapping)->std::optional<std::uint16_t>{ ++rejected_hook; return {}; };
    check(q.step(pending)==13 && rejected_hook==0 && q.cpu().pc.uint16_value==0x38,"no hook for interrupted fetch");
    check(q.step(redirect)==4 && calls==2 && q.cpu().pc.uint16_value==0x43,"redirect after pending interrupt");
    Machine pref(program({0xdd,0x21,0,0,0})); pref.cpu().im=1; pref.cpu().iff1=0; pref.cpu().sp.uint16_value=0xdff0;
    check(pref.step()==4 && pref.cpu().resume==Z80_RESUME_XY,"prefix before interrupt");
    pref.cpu().iff1=1; rejected_hook=0;
    check(pref.step(pending)==10 && rejected_hook==0,"interrupt deferred until prefix completes");
    check(pref.step(pending)==13 && rejected_hook==0 && pref.cpu().pc.uint16_value==0x38,"IRQ after prefix");
    Machine n(program({0})); n.cpu().sp.uint16_value=0xdff0;
    n.set_pause(true);
    check(n.next_nmi()==Machine::pause_sample_offset,"query includes pending frame-zero Pause");
    n.advance(0);
    check(n.next_nmi()==Machine::pause_sample_offset,"future Pause NMI query");
    n.advance(Machine::pause_sample_offset);
    check(n.next_nmi()==Machine::pause_sample_offset,"pending NMI query");
    check(n.step()==11 && n.cpu().pc.uint16_value==0x66,"NMI acceptance");
    n.set_pause(true); check(!n.next_nmi(),"pause is edge triggered");
    n.set_pause(false); n.advance(Machine::frame_cycles-n.cycles()+Machine::pause_sample_offset);
    n.set_pause(true); n.advance(Machine::frame_cycles); check(n.next_nmi().has_value(),"new pause edge");
    // Z80.c:1141-1146 records IFF1 before RETN; 2608-2612 reject INT
    // when that saved bit is zero. The B1a mirror used the opposite condition.
    Machine ret(program({0xed,0x45,0})); ret.cpu().iff1=0; ret.cpu().iff2=1;
    ret.cpu().sp.uint16_value=0xc100; ret.memory().write(0xc100,2); ret.memory().write(0xc101,0);
    ret.step(); int ret_hooks=0;
    auto ret_hook=[&](std::uint16_t,Memory::Mapping)->std::optional<std::uint16_t>{ ++ret_hooks; return {}; };
    ret.step(ret_hook); check(ret_hooks==1 && ret.cpu().pc.uint16_value==3,"RETN delay permits hook");
    ret.step(ret_hook); check(ret_hooks==1 && ret.cpu().pc.uint16_value==0x38,"RETN delay ends before IRQ");
    Machine future(program({0})); future.schedule_nmi(5); future.advance(5);
    check(future.next_nmi()==5 && future.cycles()==5,"advance delivers NMI");
    Machine nmi_before(program({0}));
    nmi_before.schedule_nmi(3);
    check(nmi_before.step()==4 && nmi_before.cpu().pc.uint16_value==1,
          "NMI at E-1 finishes current instruction");
    check(nmi_before.step()==11 && nmi_before.cpu().pc.uint16_value==0x66,
          "NMI at E-1 enters at next boundary");
    Machine nmi_at(program({0}));
    nmi_at.schedule_nmi(4);
    check(nmi_at.step()==4 && nmi_at.cpu().pc.uint16_value==1,
          "NMI at E finishes current instruction");
    check(nmi_at.step()==4 && nmi_at.cpu().pc.uint16_value==2,
          "NMI at E misses the previous sample");
    check(nmi_at.step()==11 && nmi_at.cpu().pc.uint16_value==0x66,
          "NMI at E enters after the next instruction");
    Machine pause_before(program({0})); pause_before.set_pause(true);
    pause_before.advance(Machine::pause_sample_offset-3);
    check(pause_before.step()==4 && pause_before.step()==11,
          "Pause at E-1 enters after current instruction");
    Machine pause_at(program({0})); pause_at.set_pause(true);
    pause_at.advance(Machine::pause_sample_offset-4);
    check(pause_at.step()==4 && pause_at.step()==4 && pause_at.step()==11,
          "Pause at E enters after the next instruction");
}
void copy_run() {
    Machine a(program({0xdd,0x21,0x34,0x12,0x3e,0x66,0xd3,0x7f,0x00}));
    check(a.step()==4 && a.cpu().resume==Z80_RESUME_XY,"copy resume point");
    Machine b=a; Machine c(program({0})); c=a;
    for (int i=0;i<4;++i) { check(a.step()==b.step(),"copy timing"); c.step(); }
    check(a.equivalent(b) && a.equivalent(c),"complete copy state");
    b.memory().write(0xc050,0x33); check(a.memory().read(0xc050)!=0x33,"copy RAM independent");
}
void hook_mappings() {
    Machine m(program({0}));
    auto expect=[&](std::uint16_t address, Memory::Kind kind, unsigned bank) {
        m.cpu().pc.uint16_value=address;
        int called=0;
        auto hook=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
            ++called; check(pc==address && map==Memory::Mapping{kind,bank},"hook mapped location"); return {};
        };
        m.step(hook); check(called==1,"hook called once");
    };
    m.memory().write(0xfffd,9);
    expect(0x03ff,Memory::Kind::rom,0);
    expect(0x0400,Memory::Kind::rom,9);
    m.memory().write(0xfffe,0xff); expect(0x4000,Memory::Kind::rom,31);
    m.memory().write(0xfffc,8); expect(0x8000,Memory::Kind::cartridge_ram,0);
    expect(0xc000,Memory::Kind::ram,0);
}
}
int main() { try { startup_state(); timing(); ports(); vdp_control(); vdp_timing(); irq_boundaries(); input_frames(); cpu(); interrupts(); copy_run(); hook_mappings(); std::cout << "Machine tests passed.\n"; return 0; }
catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
