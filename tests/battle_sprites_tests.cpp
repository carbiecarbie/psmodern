// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/battle_sprites.h"
#include "probe.h"
#include "c4_sweep.h"
#include <iostream>

using namespace psmodern;
namespace {
constexpr unsigned slot=0xC860;
namespace sprites=game::battle_sprites;
void state(probe::Harness& h,std::span<const std::uint16_t> entries) {
    for (auto pc:entries) for (unsigned byte=0;byte<256;++byte) {
        const RoutineEntry entry{1,pc};
        auto m=h.prepare(entry,{.seed=byte});
        m.cpu().ix_iy[1].uint16_value=slot;
        m.cpu().hl.uint16_value=0xC600;
        m.memory().write(0xC600,byte);
        m.memory().write(0xC88A,byte);
        m.memory().write(0xC2EF,byte);
        m.memory().write(0xC29F,byte);
        m.memory().write(0xC2EE,byte);
        m.memory().write(0xC2E0,byte);
        m.memory().write(0xC309,byte);
        for (unsigned i:{0u,1u,10u,14u,15u,24u}) m.memory().write(slot+i,byte*(i+1));
        h.run_cpp(m,entry);
    }
}
void animation(probe::Harness& h,bool projectile) {
    const RoutineEntry update{1,static_cast<std::uint16_t>(projectile ? 0x5E72 : 0x5BCE)};
    for (unsigned timer:{0u,1u,2u,127u,128u,255u}) for (unsigned frame:{0u,7u,254u,255u}) {
        auto m=h.prepare(update,{.seed=frame+timer});
        m.cpu().ix_iy[1].uint16_value=slot;
        m.memory().write(slot,3);
        m.memory().write(slot+1,frame);
        m.memory().write(slot+14,timer);
        m.memory().write(slot+15,8);
        m.memory().write(slot+24,5);
        m.memory().write(0xC2EF,0x80);
        auto original=m;
        for (unsigned i=0;m.cpu().pc.uint16_value!=0x0180;++i) {
            probe::check(i<12,"sprite frame update did not return");
            h.run_cpp(m,{1,m.cpu().pc.uint16_value});
        }
        while (original.cycles()<m.cycles()) original.step();
        probe::check(oracle::equal(m,original,oracle::Scope::whole),"battle sprite update diverged");
        const auto left=static_cast<std::uint8_t>(timer-1);
        const auto next=static_cast<std::uint8_t>(frame+1);
        const bool tick=(left&0x80)!=0,finished=tick && next>=8;
        probe::check(m.peek(slot)==(finished ? 0 : 3) && m.peek(slot+14)==(tick ? 5 : left) &&
            m.peek(slot+1)==(tick && !finished ? next : frame),"sprite timer, frame or retirement differs");
        if (projectile && finished) probe::check(m.peek(0xC004)==0xBB,"projectile missed its sound request");
    }
}
void records(probe::Harness& h,bool projectile) {
    const auto entry=projectile ? sprites::projectile : sprites::attack;
    for (unsigned index=0;index<(projectile ? 2u : 19u);++index) {
        auto m=h.prepare(entry,{.seed=index});
        m.cpu().ix_iy[1].uint16_value=slot;
        m.memory().write(slot+10,index);
        m.memory().write(0xC88A,index<<6);
        m.memory().write(projectile ? 0xC896 : 0xC894,0x41);
        m.memory().write(projectile ? 0xC897 : 0xC895,0x37);
        auto original=m;
        for (unsigned step=0;m.cpu().pc.uint16_value!=0x03FA;++step) {
            probe::check(step<180,"sprite initialization did not reach graphics upload");
            m.step(h.registry());
        }
        while (original.cycles()<m.cycles()) original.step();
        const unsigned record=(projectile ? 0x5EA2 : 0x5BF1)+6*index;
        probe::check(oracle::equal(m,original,oracle::Scope::whole) && m.peek(0xC004)==m.peek(record) &&
            m.peek(slot+24)==m.peek(record+1) && m.peek(slot+1)==m.peek(record+2) &&
            m.peek(slot+15)==m.peek(record+3) && m.peek(slot+2)==0x41 && m.peek(slot+4)==0x37,
            "battle sprite record or graphics mapping differs");
    }
}
void chest_roll(probe::Harness& h) {
    for (unsigned frame:{0x42u,0x43u,0x44u,0xFFu}) {
        auto m=h.prepare({1,0x5D0D},{.seed=frame});
        m.cpu().ix_iy[1].uint16_value=slot;
        const auto sp=m.cpu().sp.uint16_value;
        m.memory().write(sp+1,frame);
        m.memory().write(slot,1); m.memory().write(slot+1,frame);
        h.run_cpp(m,{1,0x5D0D});
        probe::check(m.peek(slot)==(frame<0x43 ? 1 : 0) && m.peek(slot+1)==(frame<0x43 ? frame : 0x3D),
                     "treasure object retired at the wrong frame");
    }
    for (unsigned threshold=0;threshold<256;++threshold)
        for (unsigned draw=0;draw<256;++draw) {
            auto m=h.prepare({1,0x5C7C},{.seed=threshold+draw});
            m.cpu().ix_iy[1].uint16_value=slot;
            m.cpu().af.uint8_values.at_1=draw;
            m.memory().write(0xC2E0,threshold);
            h.run_cpp(m,{1,0x5C7C});
            const bool selected=threshold && (threshold>=0xF0 || threshold>=draw);
            const auto expected=selected ? (threshold&1 ? 0x43 : 0x3E) : 0x3D;
            probe::check(m.peek(slot+15)==expected,"chest selected the wrong reveal animation");
        }
}
void moving_object(probe::Harness& h) {
    for (unsigned location:{0u,0x17u}) {
        auto m=h.prepare(sprites::moving,{.seed=location});
        m.cpu().ix_iy[1].uint16_value=slot;
        m.memory().write(0xC309,location);
        auto native=m;
        auto finish=[&] {
            for (unsigned step=0;m.cpu().pc.uint16_value!=0x0180;++step) {
                probe::check(step<160,"moving object did not complete one update");
                m.step(h.registry());
            }
            while (native.cycles()<m.cycles()) native.step();
            probe::check(oracle::equal(m,native,oracle::Scope::whole),"moving object diverged during its trajectory");
        };
        finish();
        unsigned ticks=0;
        while (m.peek(slot)!=0) {
            probe::check(++ticks<750,"moving object did not reach its destination");
            for (auto* copy:{&m,&native}) {
                copy->cpu().sp.uint16_value=0xDF00;
                copy->cpu().pc.uint16_value=0x5D5B;
            }
            finish();
        }
        probe::check((location==0x17 ? m.peek(slot+2)==0x78 : m.peek(slot+4)==0x90),
                     "moving object retired at the wrong coordinate");
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
        {
            probe::Harness sweeping(*rom);
std::vector<std::uint16_t> all;
            auto add=[&](const auto& list) { all.insert(all.end(),list.begin(),list.end()); };
            add(sprites::attack_entries); add(sprites::projectile_entries); add(sprites::sequence_entries);
            add(sprites::chest_entries); add(sprites::moving_entries);
            c4::sweep_file(sweeping,"battle_sprites",all);
        }
        state(h,sprites::attack_entries);
        state(h,sprites::projectile_entries);
        state(h,sprites::sequence_entries);
        state(h,sprites::chest_entries);
        state(h,sprites::moving_entries);
        chest_roll(h);
        moving_object(h);
        for (bool projectile:{false,true}) {records(h,projectile); animation(h,projectile);}
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
