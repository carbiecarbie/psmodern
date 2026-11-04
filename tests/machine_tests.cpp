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
void timing() {
    Machine m(program({0x3e,0x42,0xd3,0x7f,0xdb,0xbe,0xdd,0xd3,0x7f}));
    check(m.step()==7,"LD duration");
    check(m.step()==11,"OUT duration");
    check(m.accesses().size()==1 && m.accesses()[0]==PortAccess{15,0x7f,0x42,true},"OUT strobe");
    check(m.step()==11,"IN duration");
    check(m.accesses()[1]==PortAccess{26,0xbe,0xff,false},"IN strobe");
    check(m.step()==4 && m.cpu().resume==Z80_RESUME_XY,"prefix resume");
    check(m.step()==11 && m.cpu().resume==0,"prefixed OUT");
    check(m.accesses()[2]==PortAccess{41,0x7f,0xff,true},"prefixed OUT strobe");
    Machine block(program({0xed,0x41,0xed,0xa3,0xed,0xa2}));
    block.cpu().bc.uint16_value=0x017f; block.cpu().hl.uint16_value=0xc100;
    block.memory().write(0xc100,0x5a);
    check(block.step()==12 && block.accesses()[0]==PortAccess{9,0x7f,1,true},"OUT (C) strobe");
    check(block.step()==16 && block.accesses()[1]==PortAccess{25,0x7f,0x5a,true},"OUTI strobe");
    check(block.step()==16 && block.accesses()[2]==PortAccess{38,0x7f,0xff,false},"INI strobe");
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
    for (int p=0x40;p<0xc0;++p) { m.write_port(p,0x55,123); check(m.accesses().back()==PortAccess{123,(std::uint8_t)p,0x55,true},"device mirror"); }
    for (int p=0x40;p<0xc0;++p) { check(m.read_port(p,456)==0xff && m.accesses().back()==PortAccess{456,(std::uint8_t)p,0xff,false},"device read mirror"); }
    for (int p=0xc0;p<=0xff;++p) check(m.read_port(p,0)==m.read_port(0xdc+(p&1),0),"controller mirrors");
    for (int p=0;p<0x40;p+=2) { m.write_port(p,0,0); check(m.memory().memory_control()==0,"memory-control mirrors"); }
    for (int p=1;p<0x40;p+=2) { m.write_port(p,0xf0,0); check((m.read_port(0xdd,0)&0xc8)==0xc8,"I/O control mirrors"); }
    Machine open(program({0xdb,0x3e}));
    open.step(); check((open.cpu().af.uint16_value>>8)==0x3e,"SMS1 I/O open bus follows memory access");
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
    check(indexed.step()==4 && indexed.step()==16 && indexed.cpu().r==2,"indexed CB counts only M1 fetches");
    Machine halt(program({0x76}));
    check(halt.step()==4 && halt.cpu().resume==Z80_RESUME_HALT,"HALT");
    check(halt.step()==4 && halt.cpu().r==2,"HALT increments R");
    halt.cpu().sp.uint16_value=0xdff0; halt.set_pause(true);
    check(halt.step()==11 && halt.cpu().pc.uint16_value==0x66 && halt.cpu().r==3,"NMI exits HALT");
    z80_power(&halt.cpu(),1); check(halt.cpu().r==0,"reset R");
}
void interrupts() {
    Machine m(program({0xfb,0x00,0x00}));
    m.cpu().im=1; m.cpu().sp.uint16_value=0xdff0; m.set_int(true);
    check(m.step()==4 && m.cpu().iff1,"EI");
    check(m.step()==4 && m.cpu().pc.uint16_value==2,"EI delay");
    check(m.step()==13 && m.cpu().pc.uint16_value==0x38 && m.cpu().r==3,"IM1 acceptance");
    int calls=0;
    auto redirect=[&](std::uint16_t pc, Memory::Mapping) -> std::optional<std::uint16_t> {
        ++calls; check(pc==0x38,"hook targets ISR after IRQ"); return 0x42;
    };
    auto fetches=m.opcode_fetches(); auto old_r=m.cpu().r; auto old_cycle=m.cycles();
    check(m.step(redirect)==4 && calls==1 && m.cpu().pc.uint16_value==0x43,"interrupt redirect");
    check(m.opcode_fetches()==fetches+1 && m.cpu().r==old_r+1 && m.cycles()==old_cycle+4,"interrupt redirect costs only instruction");
    Machine q(program({0})); q.cpu().sp.uint16_value=0xdff0; q.cpu().im=1; q.cpu().iff1=1; q.set_int(true);
    int rejected_hook=0;
    auto pending=[&](std::uint16_t, Memory::Mapping)->std::optional<std::uint16_t>{ ++rejected_hook; return {}; };
    check(q.step(pending)==13 && rejected_hook==0 && q.cpu().pc.uint16_value==0x38,"no hook for interrupted fetch");
    check(q.step(redirect)==4 && calls==2 && q.cpu().pc.uint16_value==0x43,"redirect after pending interrupt");
    Machine pref(program({0xdd,0x21,0,0,0})); pref.cpu().im=1; pref.cpu().iff1=1; pref.cpu().sp.uint16_value=0xdff0;
    check(pref.step()==4 && pref.cpu().resume==Z80_RESUME_XY,"prefix before interrupt");
    pref.set_int(true); rejected_hook=0;
    check(pref.step(pending)==10 && rejected_hook==0,"interrupt deferred until prefix completes");
    check(pref.step(pending)==13 && rejected_hook==0 && pref.cpu().pc.uint16_value==0x38,"IRQ after prefix");
    Machine n(program({0})); n.cpu().sp.uint16_value=0xdff0;
    n.set_pause(true); check(n.next_nmi()==0,"pending NMI query");
    check(n.step()==11 && n.cpu().pc.uint16_value==0x66,"NMI acceptance");
    n.set_pause(true); check(!n.next_nmi(),"pause is edge triggered");
    n.set_pause(false); n.set_pause(true); check(n.next_nmi().has_value(),"new pause edge");
    Machine future(program({0})); future.schedule_nmi(5); future.advance(5);
    check(future.next_nmi()==5 && future.cycles()==5,"advance delivers NMI");
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
int main() { try { timing(); ports(); cpu(); interrupts(); copy_run(); hook_mappings(); std::cout << "Machine tests passed.\n"; return 0; }
catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
