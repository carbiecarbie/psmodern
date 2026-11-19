// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/video_transfer.h"
#include "game/frame_updates.h"
#include "probe.h"
#include <iostream>

using namespace psmodern;
namespace {
// The documented equality of engine and original (milestone 14): the pinned
// core's per-step scratch is not machine state, and a segment whose final is
// rendered runs no core step. The host-side queues the equality leaves out
// are compared too: the PSG writes waiting to be taken and the cartridge RAM
// dirty flag.
bool same(const Console& a, const Console& b) {
    const bool equal=oracle::equal(a,b,oracle::Scope::whole), writes=a.psg().writes()==b.psg().writes(),
        dirty=a.memory().cartridge_ram_dirty()==b.memory().cartridge_ram_dirty();
    if (!equal) for (const auto& line:oracle::differences(a,b,oracle::Scope::whole)) std::cerr<<line<<'\n';
    if (!writes) std::cerr<<"PSG writes differ\n";
    if (!dirty) std::cerr<<"cartridge RAM dirty flag differs\n";
    return equal && writes && dirty;
}
void compare_transfers(probe::Harness& h) {
    for (auto s:game::video_transfer::segments) {
        for (unsigned count=0;count<256;++count) {
            auto m=h.prepare(s.entry,{.seed=count});
            m.cpu().bc.uint16_value=(count<<8)|(s.entry.address==0x03AA ? 0xBF : 0xBE);
            m.cpu().de.uint16_value=0x4100+count;
            m.cpu().hl.uint16_value=0xC600+count;
            m.memory().write(m.cpu().hl.uint16_value,count);
            h.run_cpp(m,s.entry);
        }
    }
    for (auto list:{std::span<const std::uint16_t>(game::video_transfer::direct_packet_entries),
                    std::span<const std::uint16_t>(game::video_transfer::masked_packet_entries)}) {
        for (auto pc:list) for (unsigned control=0;control<256;++control) {
            auto m=h.prepare({0,pc},{.seed=control});
            m.cpu().hl.uint16_value=0xC600;
            m.memory().write(0xC600,control);
            m.cpu().de.uint16_value=0x4400;
            h.run_cpp(m,{0,pc});
        }
    }
    // A partial control command reverses which of E and D acts as the
    // command byte. Only changes to interrupt registers need the core.
    for (bool latched:{false,true}) {
        for (unsigned cmd=0;cmd<256;++cmd) {
            auto m=h.prepare({0,8},{.seed=cmd});
            if (latched) m.write_port(0xBF,0x55,m.cycles());
            m.cpu().de.uint16_value=latched ? cmd : cmd<<8;
            const auto reg=cmd&15;
            if ((cmd>>6)==2 && (reg==0 || reg==1 || reg==10))
                h.run_fallback(m,{0,8},RoutineFallback::frame);
            else
                h.run_cpp(m,{0,8});
        }
    }
}
void video_registers(probe::Harness& h) {
    const RoutineEntry entry{0,0x03AA};
    for (bool latched:{false,true}) for (auto reg:{0u,1u,2u,10u,15u})
        for (unsigned count:{0,1,2,3,255}) {
            auto m=h.prepare(entry,{.seed=count});
            if (latched) m.write_port(0xBF,0xA5,m.cycles());
            m.cpu().bc.uint16_value=(count<<8)|0xBF;
            m.cpu().hl.uint16_value=0xC610;
            m.memory().write(0xC610,0x80|reg);
            m.memory().write(0xC611,0x81);
            auto original=m;
            // Milestone 14: a slice is up to sixteen OTIR iterations, each
            // its own instruction, all rendered.
            h.run_cpp(m,entry);
            while (original.cycles()<m.cycles()) original.step();
            probe::check(same(m,original),"VDP register output or iteration count diverged");
        }
    auto m=h.prepare({0,0x03A4},{.seed=9});
    auto original=m;
    for (unsigned i=0;m.cpu().pc.uint16_value!=0x0180;++i) {
        probe::check(i<25,"VDP setup did not return");
        h.run_cpp(m,{0,m.cpu().pc.uint16_value});
    }
    while (original.cycles()<m.cycles()) original.step();
    probe::check(same(m,original),"VDP setup differs after all twenty port writes");
}
void register_windows(probe::Harness& h) {
    const RoutineEntry entry{0,0x03AA};
    for (auto event:{0,1,2}) for (unsigned distance=1;distance<=43;++distance) {
        for (unsigned count:{0,1,2,3}) {
            auto m=h.prepare(entry,{.iff1=event==0,.iff2=event==0,.seed=distance+count});
            m.cpu().bc.uint16_value=(count<<8)|0xBF;
            m.cpu().hl.uint16_value=0xFFFF;
            std::uint64_t until=Machine::frame_cycles;
            if (event==0) until=*m.next_irq();
            if (event==1) { m.set_pause(true); until=Machine::pause_sample_offset; }
            m.advance(until-distance-m.cycles());
            auto other=m;
            m.step(h.registry());
            do { other.step(); } while (other.cycles()<m.cycles());
            probe::check(same(m,other) && m.last_access()==other.last_access(),
                         "register write differs across IRQ, NMI or frame edge");
            // The previous instruction's opcode bytes, which the next
            // boundary's arbitration reads (data[0] and data[1]); the core's
            // other scratch bytes are not machine state (milestone 14: the
            // last OTIR iteration is rendered, not run on the core).
            probe::check(m.cpu().data.uint8_array[0]==other.cpu().data.uint8_array[0] &&
                         m.cpu().data.uint8_array[1]==other.cpu().data.uint8_array[1],
                         "register transfer lost the previous opcode bytes");
        }
    }
    auto m=h.prepare(entry,{.seed=1});
    m.cpu().bc.uint16_value=0x027E;
    h.run_fallback(m,entry,RoutineFallback::frame);
}
void vblank_uploads(probe::Harness& h) {
    namespace f=game::frame_updates;
    for (auto list:{std::span<const f::Transfer>(f::entries),std::span<const f::Transfer>(f::calls),
                    std::span<const f::Transfer>(f::jumps)}) for (auto t:list) {
        auto m=h.prepare({0,t.pc},{.seed=t.pc});
        m.cpu().bc.uint16_value=0x04BE;
        m.cpu().hl.uint16_value=0xC600;
        h.run_cpp(m,{0,t.pc});
    }
    // Each of the two page writers uses B==0 for 256 bytes on subsequent pages.
    for (std::uint16_t pc:{0x0226,0x0290}) for (unsigned pages:{1u,2u,3u}) {
        auto m=h.prepare({0,pc},{.seed=pages});
        m.cpu().af.uint8_values.at_1=pages;
        m.cpu().bc.uint16_value=0x03BE;
        m.cpu().hl.uint16_value=0xC600;
        m.write_port(0xBF,0,m.cycles());
        m.write_port(0xBF,0x40,m.cycles());
        const unsigned size=3+256*(pages-1);
        for (unsigned i=0;i<size;++i) m.memory().write(0xC600+i,i*39+pages);
        auto native=m;
        for (unsigned steps=0;m.cpu().pc.uint16_value!=pc+9;++steps) {
            probe::check(steps<size*2+4,"frame upload did not finish its pages");
            h.run_cpp(m,{0,m.cpu().pc.uint16_value});
        }
        while (native.cycles()<m.cycles()) native.step();
        probe::check(same(m,native) && m.cpu().hl.uint16_value==0xC600+size,"frame upload pointer differs");
        m.advance(24);
        for (unsigned i=0;i<size;++i)
            probe::check(m.vdp().vram()[i]==static_cast<std::uint8_t>(i*39+pages),"frame upload has the wrong tile byte");
    }
    for (unsigned high:{0x80u,0x81u,0x8Au}) {
        auto m=h.prepare({0,0x01EF},{});
        m.write_port(0xBF,0,m.cycles());
        m.memory().write(0xC300,high);
        h.run_fallback(m,{0,0x01EF},RoutineFallback::frame);
    }
}
void sprite_tail(probe::Harness& h) {
    for (std::uint16_t pc=0x58AE;pc<0x599E;pc+=16) {
        if (pc==0x595E) continue;
        const RoutineEntry entry{1,pc};
        for (unsigned count:{0u,1u,7u,8u,9u,255u}) for (std::uint16_t source:{0x3FFC,0xC6FC,0xFFFC}) {
            auto m=h.prepare(entry,{.seed=count});
            m.cpu().bc.uint16_value=(count<<8)|0xBE;
            m.cpu().hl.uint16_value=source;
            auto native=m;
            const auto cost=h.run_cpp(m,entry);
            for (unsigned i=0;i<8;++i) native.step();
            probe::check(cost.cycles==128 && same(m,native) && m.last_access()==native.last_access() &&
                         m.cpu().data.uint32_value==native.cpu().data.uint32_value,"unrolled sprite transfer differs");
        }
    }
    const RoutineEntry entry{1,0x591E};
    for (unsigned event=0;event<3;++event) for (unsigned distance=1;distance<=129;++distance) {
        auto m=h.prepare(entry,{.iff1=event==1,.iff2=event==1,.seed=distance});
        auto end=event==1 ? m.next_irq().value() : Machine::frame_cycles;
        if (event==2) {end-=256; m.schedule_nmi(end);}
        m.advance(end-distance-m.cycles());
        m.cpu().bc.uint16_value=0x00BE;
        m.cpu().hl.uint16_value=0xC600;
        oracle::NativeMachine native(m);
        m.step(h.registry());
        do {native.step();} while (native.cycles()<m.cycles());
        // The documented equality (milestone 14) with the host queues: the
        // core's per-step scratch is not machine state (review R3 M4).
        probe::check(same(m,native),"sprite tail crossed a hardware boundary");
    }
    auto m=h.prepare(entry,{});
    m.cpu().bc.uint16_value=0x23BF;
    h.run_fallback(m,entry,RoutineFallback::frame);
}
void ice_rows(probe::Harness& h) {
    auto m=h.prepare({1,0x75CC},{.iff1=true,.iff2=true,.seed=12});
    m.cpu().hl.uint16_value=0xC600;
    m.cpu().de.uint16_value=0x4620;
    for (unsigned i=0;i<8;++i) m.memory().write(0xC600+i,0x31+i);
    auto other=m;
    for (unsigned i=0;m.cpu().pc.uint16_value!=0x75E5;++i) {
        probe::check(i<60,"ice animation did not finish two rows");
        m.step(h.registry());
    }
    while (other.cycles()<m.cycles()) other.step();
    probe::check(same(m,other),"ice rows differ from original");
    m.advance(24);
    for (unsigned i=0;i<8;++i)
        probe::check(m.vdp().vram()[0x620+64*(i/4)+i%4]==0x31+i,"wrong VRAM byte in the ice animation");
}
void dialogue_frame_edge(probe::Harness& h) {
    const RoutineEntry entry{0,0x3451};
    for (unsigned event=0;event<3;++event) for (unsigned distance=1;distance<=21;++distance) {
        auto m=h.prepare(entry,{.iff1=event==1,.iff2=event==1,.seed=distance});
        std::uint64_t deadline=Machine::frame_cycles;
        if (event==1) deadline=*m.next_irq();
        if (event==2) {m.set_pause(true); deadline=m.next_nmi().value();}
        m.advance(deadline-distance-m.cycles());
        auto original=m;
        const bool fits=event==0 ? distance>10 : distance>=20;
        if (fits) h.run_cpp(m,entry);
        else h.run_fallback(m,entry,event==0 ? RoutineFallback::frame : event==1 ? RoutineFallback::irq : RoutineFallback::nmi);
        do {original.step();} while(original.cycles()<m.cycles());
        probe::check(same(m,original),"dialogue load differs at frame or interrupt");
        FrameInput pads{};
        pads.pads[0].one=true;
        for (unsigned i=0;i<2;++i) {
            m.run_frame(pads);
            original.run_frame(pads);
            probe::check(same(m,original),"dialogue load changed input or timing on the next frame");
        }
    }
}
// Interrupt sweeps (milestone 14, recipe step 5): an IRQ and an NMI at every
// cycle (or every `stride` cycles) of each segment of more than one
// instruction of video_transfer.cpp and frame_updates.cpp, each run compared
// with the oracle at every instruction boundary, nested handlers included.
void sweeps(probe::Harness& h) {
    auto sweep=[&](std::uint16_t at, const std::function<void(Machine&)>& adjust={}, unsigned stride=1) {
        probe::sweep(h,{0,at},{.iff1=true,.iff2=true,.seed=at},adjust,stride);
    };
    auto registers=[](std::uint16_t bc, std::uint16_t de, std::uint16_t hl, std::uint8_t a, std::vector<std::uint8_t> stack={}) {
        return [=](Machine& m) {
            m.cpu().bc.uint16_value=bc; m.cpu().de.uint16_value=de; m.cpu().hl.uint16_value=hl; m.cpu().af.uint8_values.at_1=a;
            for (unsigned i=0;i<64;++i) m.memory().write(static_cast<std::uint16_t>(0xC600+i),static_cast<std::uint8_t>(0x83+5*i));
            std::uint16_t at=m.cpu().sp.uint16_value;
            for (auto b:stack) m.memory().write(at++,b);
        };
    };
    sweep(0x0008,registers(0,0x4123,0,0));
    for (std::uint16_t at:{0x0347,0x034C}) sweep(at,registers(0x0403,0x4100,0xC600,1));
    sweep(0x0350,registers(0x04BE,0x4100,0xC600,1));
    sweep(0x0355,registers(0x00BE,0x4100,0xC600,2));
    sweep(0x035A);
    for (std::uint16_t at:{0x0364,0x0369}) sweep(at,registers(0x0203,0x4100,0x93C7,0));
    sweep(0x0374,registers(0x0201,0x4100,0x93C7,0));
    for (std::uint16_t unit:{0x0377,0x0390}) {
        const std::uint16_t next_row=unit==0x0377 ? 0x0386 : 0x039A;
        sweep(unit,registers(0x0203,0x4100,0xC600,0));
        sweep(unit+2,registers(0x0203,0x4100,0xC600,0));
        sweep(unit+5,registers(0x03BE,0x4100,0xC600,0));
        sweep(next_row,registers(0x00BE,0x4100,0xC600,0,{0x03,0x02,0x80,0x01}));
    }
    sweep(0x03A4);
    // A RAM table of four register writes (registers 2 to 5), so that the
    // frame interrupt stays enabled during the sweep.
    sweep(0x03AA,[](Machine& m) {
        constexpr std::uint8_t table[]{0xFF,0x82,0xFF,0x83,0x07,0x84,0xFF,0x85};
        for (unsigned i=0;i<8;++i) m.memory().write(static_cast<std::uint16_t>(0xC610+i),table[i]);
        m.cpu().bc.uint16_value=0x08BF; m.cpu().hl.uint16_value=0xC610;
    });
    sweep(0x03AC);
    for (std::uint16_t unit:{0x03CD,0x03FA}) {
        const bool masked=unit==0x03FA;
        const std::uint16_t header=unit+13, data=masked ? 0x0414 : 0x03E6, stride=masked ? 0x041C : 0x03ED;
        for (std::uint16_t at:{unit,static_cast<std::uint16_t>(unit+2)}) sweep(at,registers(0x0304,0x4400,0xC600,0));
        sweep(unit+7,registers(0,0,0xC600,0,{0x00,0x44,0x04,0x03,0x80,0x01}));
        sweep(header,registers(0x0304,0x4400,0xC600,0));
        sweep(data,registers(0x0304,0x4400,0xC600,0));
        for (std::uint16_t at:{static_cast<std::uint16_t>(stride-1),stride}) sweep(at,registers(0x0304,0x4400,0xC600,0));
        sweep(stride+9,registers(0x0004,0x4400,0xC600,0));
    }
    // frame_updates.cpp: the two vertical-blank uploads.
    for (std::uint16_t at:{0x01EF,0x0204,0x0222,0x025F,0x0274,0x027B,0x0280})
        sweep(at,registers(0x04BE,0x4100,0xC600,2),2);
    for (std::uint16_t at:{0x0226,0x0290}) sweep(at,registers(0x10BE,0x4100,0xC100,2),2);
    for (std::uint16_t at:{0x022B,0x0295}) sweep(at,registers(0x00BE,0x4100,0xC100,2));
}
void packets(probe::Harness& h,std::uint16_t pc,unsigned length) {
    auto m=h.prepare({0,pc},{.seed=length});
    m.cpu().de.uint16_value=0x4400;
    m.cpu().hl.uint16_value=0xC600;
    std::uint16_t source=0xC600;
    for (unsigned plane=0;plane<4;++plane) {
        if (length<=127) {
            m.memory().write(source++,length);
            m.memory().write(source++,0xA0+plane);
        }
        m.memory().write(source++,0x80|(length&127));
        for (unsigned i=0;i<length;++i) m.memory().write(source++,i+plane);
        m.memory().write(source++,0);
    }
    auto original=m;
    for (unsigned step=0;m.cpu().pc.uint16_value!=0x0180;++step) {
        probe::check(step<30000,"four-plane graphics transfer did not return");
        m.step(h.registry());
    }
    while (original.cycles()<m.cycles()) original.step();
    probe::check(same(m,original) && m.cpu().hl.uint16_value==source,"graphics packet decoder differs");
    m.advance(24);
    for (unsigned plane=0;plane<4;++plane) for (unsigned i=0;i<length;++i) {
        if (length<=127)
            probe::check(m.vdp().vram()[0x400+4*i+plane]==0xA0+plane,"wrong repeated graphics byte");
        auto literal_start=length<=127 ? length : 0;
        probe::check(m.vdp().vram()[0x400+4*(i+literal_start)+plane]==static_cast<std::uint8_t>(i+plane),
                     "wrong literal graphics byte");
    }
}
void complete(probe::Harness& h,std::uint16_t pc,unsigned count) {
    auto m=h.prepare({0,pc},{.seed=count});
    const bool fill=pc==0x0363, rows=pc==0x0377 || pc==0x0390;
    m.cpu().bc.uint16_value=rows ? 0x0200|count : count;
    m.cpu().de.uint16_value=0x4100;
    m.cpu().hl.uint16_value=fill ? 0x93C7 : 0xC600;
    m.memory().write(0xC210,0x82);
    for (unsigned i=0;i<2*count;++i) m.memory().write(0xC600+i,3*i+7);
    auto original=m;
    for (unsigned step=0;m.cpu().pc.uint16_value!=0x0180;++step) {
        probe::check(step<2000,"video transfer did not return");
        m.step(h.registry());
    }
    while (original.cycles()<m.cycles()) original.step();
    probe::check(same(m,original),"whole video transfer differs");
    // Give the last pending data write its video slot.
    m.advance(24);
    for (unsigned row=0;row<(rows ? 2u : 1u);++row) {
        for (unsigned i=0;i<count;++i) {
            const bool words=fill || pc==0x0377;
            const unsigned addr=0x100+row*64+i*(words ? 2 : 1);
            const auto expected=static_cast<std::uint8_t>(fill ? 0xC7 : 3*(row*count+i)+7);
            probe::check(m.vdp().vram()[addr]==expected,"wrong byte sent to video memory");
            if (words) probe::check(m.vdp().vram()[addr+1]==(fill ? 0x93 : 0x82),"wrong tile attribute");
        }
    }
}
}
int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        if (argc!=3) return 1;
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probe::Harness h(*rom);
        compare_transfers(h);
        video_registers(h);
        register_windows(h);
        ice_rows(h);
        vblank_uploads(h);
        sprite_tail(h);
        dialogue_frame_edge(h);
        for (auto pc:{0x03CD,0x03FA}) for (auto n:{1,3,31,127,256}) packets(h,pc,n);
        for (auto pc:{0x0346,0x0363}) for (auto n:{1,7,255,256,257}) complete(h,pc,n);
        for (auto pc:{0x0377,0x0390}) for (auto n:{1,7,31}) complete(h,pc,n);
        sweeps(h);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
