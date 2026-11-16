// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/video_transfer.h"
#include "game/frame_updates.h"
#include "probe.h"
#include <iostream>

using namespace psmodern;
namespace {
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
            const auto duration=m.video_register_output_cycles();
            h.run_cpp(m,entry);
            original.step();
            if (duration>21) original.step();
            probe::check(m.equivalent(original),"VDP register output or iteration count diverged");
        }
    auto m=h.prepare({0,0x03A4},{.seed=9});
    auto original=m;
    for (unsigned i=0;m.cpu().pc.uint16_value!=0x0180;++i) {
        probe::check(i<25,"VDP setup did not return");
        h.run_cpp(m,{0,m.cpu().pc.uint16_value});
    }
    while (original.cycles()<m.cycles()) original.step();
    probe::check(m.equivalent(original),"VDP setup differs after all twenty port writes");
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
            probe::check(m.equivalent(other) && m.last_access()==other.last_access(),
                         "register write differs across IRQ, NMI or frame edge");
            probe::check(m.cpu().data.uint32_value==other.cpu().data.uint32_value,
                         "register transfer lost prefix scratch state");
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
        probe::check(m.equivalent(native) && m.cpu().hl.uint16_value==0xC600+size,"frame upload pointer differs");
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
            probe::check(cost.cycles==128 && m.equivalent(native) && m.last_access()==native.last_access() &&
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
        auto native=m;
        m.step(h.registry());
        do {native.step();} while (native.cycles()<m.cycles());
        probe::check(m.equivalent(native),"sprite tail crossed a hardware boundary");
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
    probe::check(m.equivalent(other),"ice rows differ from original");
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
        probe::check(m.equivalent(original),"dialogue load differs at frame or interrupt");
        FrameInput pads{};
        pads.pads[0].one=true;
        for (unsigned i=0;i<2;++i) {
            m.run_frame(pads);
            original.run_frame(pads);
            probe::check(m.equivalent(original),"dialogue load changed input or timing on the next frame");
        }
    }
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
    probe::check(m.equivalent(original) && m.cpu().hl.uint16_value==source,"graphics packet decoder differs");
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
    probe::check(m.equivalent(original),"whole video transfer differs");
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
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
