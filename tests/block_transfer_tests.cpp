// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include <algorithm>
#include <iostream>

using namespace psmodern;
namespace {
Machine prepare(probe::Harness& harness,RoutineEntry entry,unsigned count,
                std::uint16_t source,std::uint16_t dest) {
    auto m=harness.prepare(entry,{.cartridge_ram=true,.seed=count});
    for (unsigned i=0;i<32;++i)
        m.memory().write(static_cast<std::uint16_t>(0xc400+i),static_cast<std::uint8_t>(7*i+1));
    m.cpu().hl.uint16_value=source;
    m.cpu().de.uint16_value=dest;
    m.cpu().bc.uint16_value=static_cast<std::uint16_t>(count);
    return m;
}
void check_slices(probe::Harness& h,RoutineEntry entry) {
    constexpr std::pair<std::uint16_t,std::uint16_t> ranges[]{
        {0xc400,0xc401},{0xc401,0xc400},{0xc400,0xe401},{0xc400,0x8000},
        {0x8000,0xc401},{0x0100,0xdeff},{0xfffc,0xc400}
    };
    for (auto [source,dest]:ranges) {
        for (unsigned remaining:{0u,1u,2u,15u,16u,17u,31u,255u,1024u,65535u}) {
            auto machine=prepare(h,entry,remaining,source,dest);
            const unsigned copied=remaining==0 || remaining>16 ? 16 : remaining;
            const auto result=h.run_cpp(machine,entry);
            probe::check(result.cycles==21*copied-(remaining==copied ? 5 : 0),"wrong copy slice duration");
            probe::check(machine.cpu().bc.uint16_value==static_cast<std::uint16_t>(remaining-copied),
                         "wrong number of remaining bytes");
        }
    }
    for (unsigned count:{1u,2u,15u,16u,17u}) {
        const auto cost=count<=16 ? 21*count-5 : 336;
        for (unsigned offset=1;offset<=cost+1;++offset) {
            auto frame=prepare(h,entry,count,0xc400,0x8000);
            auto nmi=frame;
            auto irq=frame;
            frame.advance(Machine::frame_cycles-frame.cycles()%Machine::frame_cycles-offset);
            nmi.schedule_nmi(nmi.cycles()+offset);
            irq.cpu().iff1=irq.cpu().iff2=1;
            const auto next=irq.next_irq();
            probe::check(next && *next>irq.cycles()+cost+1,"expected an upcoming frame interrupt");
            irq.advance(*next-irq.cycles()-offset);
            unsigned admitted=0,elapsed=0;
            while (admitted<count && admitted<16) {
                const unsigned next=elapsed+(admitted+1==count ? 16 : 21);
                if (next>offset) break;
                elapsed=next;
                ++admitted;
            }
            if (!admitted) {
                h.run_fallback(frame,entry,RoutineFallback::frame);
                h.run_fallback(nmi,entry,RoutineFallback::nmi);
                h.run_fallback(irq,entry,RoutineFallback::irq);
            } else {
                for (auto* machine:{&frame,&nmi,&irq}) {
                    auto result=h.run_cpp(*machine,entry);
                    probe::check(result.cycles==elapsed && machine->cpu().bc.uint16_value==count-admitted,
                                 "copy must stop after the last transfer that fits");
                }
            }
        }
    }
    for (const std::uint16_t dest:{0xffed,0xfffb,0xfffc,0xffff}) {
        auto m=prepare(h,entry,16,0xc400,dest);
        h.run_fallback(m,entry,RoutineFallback::mapper_write);
    }
}
// The frame driver observes writes for other systems. Force a frame split
// inside a 1 KiB save and verify every delivered address, value and cycle.
void observed_save() {
    constexpr RoutineEntry save{0,0x1D1D};
    auto program=std::make_shared<Memory::Rom>(512*1024,0);
    (*program)[save.address]=0xed;
    (*program)[save.address+1]=0xb0;
    (*program)[save.address+2]=0x76;
    Machine cpp(program);
    cpp.memory().write(0xfffc,8);
    cpp.cpu().pc.uint16_value=save.address;
    cpp.cpu().sp.uint16_value=0xce40;
    cpp.cpu().bc.uint16_value=1024;
    cpp.cpu().hl.uint16_value=0xc300;
    cpp.cpu().de.uint16_value=0x8000;
    for (unsigned i=0;i<1024;++i) cpp.memory().write(0xc300+i,static_cast<std::uint8_t>(i*31));
    cpp.advance(Machine::frame_cycles-800);
    Machine native=cpp;
    RoutineRegistry registry;
    game::register_block_transfers(registry);
    replay::CallVerifier verifier("save copy");
    registry.verifier=&verifier;
    using Event=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
    std::vector<Event> expected,actual;
    for (unsigned frame=0;frame<2;++frame) {
        native.run_frame({},nullptr,{},[&](auto address,auto byte) {
            expected.emplace_back(address,byte,native.cycles());
        });
        cpp.run_frame({},registry,{},[&](auto address,auto byte) {
            actual.emplace_back(address,byte,cpp.cycles());
        });
        probe::check(actual==expected && cpp.equivalent(native),"save observer or machine frame differs");
        if (!frame) probe::check(!actual.empty() && actual.size()<1024,"frame must split save");
    }
    probe::check(actual.size()==1024 && registry.find(save)->counters.cpp_calls>0,"save transfer did not run");
}
void compare_slot_save(probe::Harness& h) {
    using Event=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
    RoutineRegistry registry;
    game::register_save_menu(registry);
    game::register_block_transfers(registry);
    replay::CallVerifier verifier("slot save",true);
    registry.verifier=&verifier;
    for (unsigned slot=1;slot<=5;++slot) {
        const std::uint16_t flag=0x8200+slot, payload=0x8000+slot*1024;
        auto cpp=h.prepare(game::save_menu_entries.front(),{.seed=slot});
        cpp.cpu().af.uint16_value=0;
        cpp.cpu().hl.uint16_value=flag;
        cpp.cpu().de.uint16_value=payload;
        cpp.memory().write(0xFFFC,8);
        cpp.memory().write(flag,1);
        cpp.memory().write(0xFFFC,0x80);
        cpp.memory().write(0xC100,0x76);
        for (unsigned i=0;i<1024;++i) cpp.memory().write(0xC300+i,i*17+slot);
        cpp.advance(Machine::frame_cycles-2000-cpp.cycles());
        auto native=cpp;
        std::vector<Event> expected,actual;
        auto frame=[&](Machine& m,RoutineRegistry* r,std::vector<Event>& events) {
            m.run_frame({},r,[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if (map!=Memory::Mapping{Memory::Kind::rom,0}) return {};
                if (pc==0x1D35) return 0xC100;
                if (pc==0x1D2D) {
                    probe::check(m.peek(flag)==1,"host saw unpublished save");
                    for (unsigned i=0;i<1024;++i)
                        probe::check(m.peek(payload+i)==static_cast<std::uint8_t>(i*17+slot),"host saw partial save");
                    events.emplace_back(pc,1,m.cycles());
                }
                return {};
            },[&](auto addr,auto value) { events.emplace_back(addr,value,m.cycles()); });
        };
        for (unsigned i=0;i<2;++i) {
            frame(native,nullptr,expected);
            frame(cpp,&registry,actual);
            probe::check(actual==expected && cpp.equivalent(native),"slot save write or observation differs");
            if (i==0) probe::check(cpp.peek(flag)==0,"interrupted save prematurely published");
        }
        auto count=[&](auto addr,auto byte) {
            return std::count_if(actual.begin(),actual.end(),[&](const auto& e) {
                return std::get<0>(e)==addr && std::get<1>(e)==byte;
            });
        };
        probe::check(count(flag,0)==1 && count(flag,1)==1 && count(0x1D2D,1)==1,
                     "slot requires one invalidation, one publication and one observation");
    }
    const RoutineEntry publish{0,0x1D2B};
    for (unsigned control:{0u,8u,12u,0x80u}) {
        for (std::uint16_t hl:{0x8200,0x8201,0x8205,0x8206,0xC100,0xFFFF}) {
            auto m=h.prepare(publish,{});
            m.memory().write(0xFFFC,control);
            m.cpu().hl.uint16_value=hl;
            if ((control&8) && hl>=0x8201 && hl<=0x8205) {
                m.memory().write(hl,1);
                h.run_cpp(m,publish);
            } else h.run_fallback(m,publish,RoutineFallback::frame);
        }
    }
}
void post_save_branch(probe::Harness& h) {
    for (bool unused:{false,true}) {
        auto m=h.prepare({0,0x1D32},{.seed=unused});
        const auto sp=m.cpu().sp.uint16_value;
        m.memory().write(sp+6,0x80);
        m.memory().write(sp+7,0x01);
        m.memory().write(0xC780,0xFF);
        m.cpu().af.uint8_values.at_0=unused ? 0x40 : 0;
        auto original=m;
        h.run_cpp(m,{0,0x1D32});
        h.run_cpp(m,{0,static_cast<std::uint16_t>(unused ? 0x1D41 : 0x1D35)});
        while (original.cycles()<m.cycles()) original.step();
        probe::check(m.equivalent(original),"save notification/return diverged");
        if (unused) probe::check(m.cpu().pc.uint16_value==0x0180 && m.peek(game::dungeon::game_mode)==0x10 &&
                                 !m.peek(0xC780),"unused-slot exit did not unwind both menus");
        else probe::check(m.peek(0xC780)==0xFF,"overwritten slot exited through the new-slot path");
    }
}

}
int main(int argc,char** argv) {
    try {
        if (argc==1) { observed_save(); return 0; }
        if (argc!=3) return 1;
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probe::Harness h(*rom);
        compare_slot_save(h);
        post_save_branch(h);
        for (auto entry:game::block_transfer_entries) check_slices(h,entry);
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
