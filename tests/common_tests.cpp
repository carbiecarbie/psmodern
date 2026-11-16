// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include <iostream>
using namespace psmodern;
int main(int argc,char** argv) {
 try {
    if (argc!=3) return 1;
    auto rom=probe::load(argv[1],argv[2]); if (!rom) return 77;
    const game::Options variants[]{{},{0,200},{200,400},{50,400}};
    for (const auto& options:variants) {
    probe::Harness h(*rom,options);
    {
        for (RoutineEntry reset: {RoutineEntry{0,0x0003},RoutineEntry{0,0x0085}}) {
        auto actual=h.prepare(reset,{}), native=actual;
        unsigned cpp_visits=0,native_visits=0;
        const auto before=h.routine(reset).counters;
        native.step([&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t>{
            probe::check(pc==reset.address,"native reset callback PC"); ++native_visits; return {};
        });
        actual.step(h.registry(),[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t>{
            probe::check(pc==reset.address,"replacement reset callback PC"); ++cpp_visits; return {};
        });
        probe::check(actual.equivalent(native) && actual.cpu().sp.uint16_value==0xcb00 && cpp_visits==1 && native_visits==1 &&
                     h.routine(reset).counters.cpp_calls==before.cpp_calls+1,"observed reset stack differs");
        probe::Setup disabled; disabled.work_ram_disabled=true;
        auto no_ram=h.prepare(reset,disabled);
        h.run_fallback(no_ram,reset,RoutineFallback::unmapped_stack);
        }
    }
    {
        probe::Setup setup; setup.sp=0xfff0;
        auto crossing=h.prepare({0,0x014F},setup);
        h.run_fallback(crossing,{0,0x014F},RoutineFallback::unmapped_stack);
        setup.sp=0xc100; setup.work_ram_disabled=true;
        auto disabled=h.prepare({0,0x014F},setup);
        h.run_fallback(disabled,{0,0x014F},RoutineFallback::unmapped_stack);
    }
    for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,0x00F4},setup);
        machine.memory().write(0xc212,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,{0,0x00F4});
    }
    {
        auto actual=h.prepare({0,0x00F4},{});
        actual.memory().write(0xc212,1);
        actual.memory().write(0xc202,5);
        actual.schedule_nmi(actual.cycles()+64);
        Machine native=actual;
        for (unsigned i=0;i<12;++i) {
            actual.step(h.registry());
            while(native.cycles()<actual.cycles()) native.step();
            probe::check(actual.equivalent(native),"pause wait changed a pending NMI boundary");
            if(actual.cpu().pc.uint16_value==0x0200) break;
        }
    }
    for (unsigned seed=0;seed<64;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare({0,0x014F},setup);
        for (unsigned i=0;i<14;++i) machine.memory().write(static_cast<std::uint16_t>(setup.sp+i),static_cast<std::uint8_t>(seed+i*19));
        h.run_cpp(machine,{0,0x014F});
        auto original=machine;
        machine.step(h.registry()); original.step();
        probe::check(machine.equivalent(original),"EI-delayed native return differs");
    }
    for (unsigned at:{0x0143u,0x0147u}) for (unsigned bank:{0u,1u,3u,15u,31u}) for (bool rom_stack:{false,true}) {
        probe::Setup setup; setup.seed=bank; setup.banks[2]=static_cast<std::uint8_t>(bank);
        if (rom_stack) setup.sp=0x8000;
        auto machine=h.prepare({0,static_cast<std::uint16_t>(at)},setup);
        if (!rom_stack) {
            machine.memory().write(setup.sp,0xa5);
            machine.memory().write(static_cast<std::uint16_t>(setup.sp+1),static_cast<std::uint8_t>(bank));
            machine.memory().write(static_cast<std::uint16_t>(setup.sp+2),0x5a);
            machine.memory().write(static_cast<std::uint16_t>(setup.sp+3),8);
        }
        h.run_cpp(machine,{0,static_cast<std::uint16_t>(at)});
    }
    {
        const RoutineEntry copy{0,0x030E};
        auto actual=h.prepare(copy,{});
        for (unsigned i=0;i<0xc0;++i) actual.memory().write(static_cast<std::uint16_t>(0xc940+i),static_cast<std::uint8_t>(i+1));
        actual.memory().write(0xc940,0);
        actual.cpu().hl.uint16_value=0xc940;
        actual.cpu().de.uint16_value=0xc941;
        actual.cpu().bc.uint16_value=0x00bf;
        actual.advance(Machine::frame_cycles-actual.cycles()-8);
        Machine native=actual;
        while(actual.cpu().pc.uint16_value==copy.address) {
            actual.step(h.registry());
            while(native.cycles()<actual.cycles()) native.step();
            probe::check(actual.equivalent(native),"sprite clear differs at a transfer boundary");
        }
        for (unsigned i=0;i<0xc0;++i)
            probe::check(actual.memory().read(static_cast<std::uint16_t>(0xc940+i))==0,"sprite clear missed a byte");
        probe::check(actual.cycles()>Machine::frame_cycles,"sprite clear did not cross frame boundary");
    }
    for (unsigned seed=0;seed<64;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare({0,0x0310},setup);
        h.run_cpp(machine,{0,0x0310});
        probe::check(machine.cpu().pc.uint16_value==0x0052,"sprite wait call target differs");
    }
    for (unsigned seed=0;seed<64;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare({0,0x02FD},setup);
        for (unsigned i=0;i<0x41;++i) machine.memory().write(static_cast<std::uint16_t>(0xc900+i),static_cast<std::uint8_t>(seed+i));
        h.run_cpp(machine,{0,0x02FD});
        for (unsigned i=0;i<0x41;++i) probe::check(machine.memory().read(static_cast<std::uint16_t>(0xc900+i))==0xe0,"sprite Y clear differs");
    }
    for (unsigned value=0;value<256;++value) for (unsigned at:{0x0318u,0x031bu,0x0321u,0x0327u,0x032eu}) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().hl.uint16_value=static_cast<std::uint16_t>(value*257);
        h.run_cpp(machine,{0,static_cast<std::uint16_t>(at)});
    }
    {
        auto machine=h.prepare({0,0x0124},{});
        machine.schedule_nmi(machine.cycles()+100);
        h.run_fallback(machine,{0,0x0124},RoutineFallback::nmi);
    }
    for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,0x0124},setup);
        machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(value);
        h.run_cpp(machine,{0,0x0124});
    }
    for (std::uint16_t pc:{0x0126,0x0128}) for (unsigned b=0;b<256;++b) {
        auto m=h.prepare({0,pc},{.seed=b});
        m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(b);
        m.memory().write(0xC212,b&1 ? 0xFF : 0);
        h.run_cpp(m,{0,pc});
        probe::check(m.cpu().bc.uint8_values.at_1==0,"video timing delay failed to exhaust B");
    }
    for (unsigned value=0;value<256;++value) for (unsigned at:{0x011du,0x012au}) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,static_cast<std::uint16_t>(at)},setup);
        machine.memory().write(at==0x011d ? 0xc20a : 0xc212,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,{0,static_cast<std::uint16_t>(at)});
    }
    for (bool pressed:{false,true}) for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,0x010F},setup);
        machine.set_reset(pressed);
        machine.advance(Machine::frame_cycles);
        machine.memory().write(0xc20b,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,{0,0x010F});
    }
    for (unsigned bank:{0u,1u,15u,31u}) for (unsigned control:{0u,8u,0x80u}) {
        probe::Setup setup; setup.seed=bank*31+control;
        auto machine=h.prepare({0,0x00FB},setup);
        machine.memory().write(0xffff,static_cast<std::uint8_t>(bank));
        machine.memory().write(0xfffc,static_cast<std::uint8_t>(control));
        h.run_cpp(machine,{0,0x00FB});
    }
    {
        auto machine=h.prepare({0,0x00FB},{});
        machine.schedule_nmi(machine.cycles()+4);
        h.run_fallback(machine,{0,0x00FB},RoutineFallback::nmi);
        probe::Setup setup; setup.sp=0xfffe;
        auto mapper_stack=h.prepare({0,0x00FB},setup);
        h.run_fallback(mapper_stack,{0,0x00FB},RoutineFallback::unmapped_stack);
    }
    for (unsigned pause=0;pause<256;++pause) for (unsigned at:{0x007au,0x0081u}) for (unsigned iff2:{0u,1u}) {
        probe::Setup setup; setup.seed=pause;
        auto machine=h.prepare({0,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().iff2=static_cast<decltype(machine.cpu().iff2)>(iff2);
        machine.memory().write(0xc212,static_cast<std::uint8_t>(pause));
        machine.memory().write(static_cast<std::uint16_t>(setup.sp+2),0x00);
        machine.memory().write(static_cast<std::uint16_t>(setup.sp+3),0x02);
        h.run_cpp(machine,{0,static_cast<std::uint16_t>(at)});
        probe::check(machine.memory().read(0xc212)==static_cast<std::uint8_t>(at==0x007a ? ~pause : pause),"pause NMI toggle differs");
    }
    // A real NMI enters 0066 with the core's NMI rejection active. The
    // original PUSH AF must execute before the translated continuation.
    for (unsigned mode : {0u,3u,8u,9u,10u,11u,255u}) {
        auto machine=h.prepare({0,0x0055},{});
        machine.memory().write(0xc202,static_cast<std::uint8_t>(mode));
        machine.schedule_nmi(machine.cycles());
        machine.advance(1);
        auto native=machine;
        probe::check(h.registry().find({0,0x0066})==nullptr,"NMI vector must remain native");
        probe::check(machine.step(h.registry())==11 && native.step()==11 && machine.equivalent(native),"NMI entry differs");
        probe::check(machine.cpu().pc.uint16_value==0x0066 && (machine.cpu().request&Z80_REQUEST_REJECT_NMI),"NMI vector rejection missing");
        const auto before=machine.cpu().sp.uint16_value;
        probe::check(machine.step(h.registry())==11 && native.step()==11 && machine.equivalent(native),"native NMI PUSH differs");
        probe::check(machine.cpu().pc.uint16_value==0x0067 && machine.cpu().sp.uint16_value==before-2 && !(machine.cpu().request&Z80_REQUEST_REJECT_NMI),"NMI continuation boundary differs");
        h.run_cpp(machine,{0,0x0067});
        while (native.cycles()<machine.cycles()) native.step();
        probe::check(native.equivalent(machine),"translated NMI continuation differs");
    }
    for (unsigned mode=0;mode<256;++mode) for (unsigned at:{0x0067u,0x006eu,0x0072u,0x0076u}) {
        probe::Setup setup; setup.seed=mode;
        auto machine=h.prepare({0,static_cast<std::uint16_t>(at)},setup);
        machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(mode);
        machine.memory().write(0xc202,static_cast<std::uint8_t>(mode));
        h.run_cpp(machine,{0,static_cast<std::uint16_t>(at)});
    }
    for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,0x0038},setup);
        machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
        h.run_cpp(machine,{0,0x0038});
    }
    {
        auto machine=h.prepare({0,0x0038},{});
        machine.write_port(0xbf,0x20,machine.cycles());
        machine.write_port(0xbf,0x81,machine.cycles());
        machine.advance(Machine::frame_cycles);
        probe::check(machine.vdp().irq(),"video IRQ fixture did not assert");
        h.run_cpp(machine,{0,0x0038});
        probe::check(!machine.vdp().irq(),"status read did not acknowledge IRQ");
    }
    {
        probe::Setup setup; setup.sp=0xfffe;
        auto machine=h.prepare({0,0x0038},setup);
        h.run_fallback(machine,{0,0x0038},RoutineFallback::unmapped_stack);
    }
    // The reset copy clears all 8191 following bytes, even across a frame
    // boundary. Compare every admitted slice with the original transfers.
    {
        const RoutineEntry copy{0,0x00A0};
        auto actual=h.prepare(copy,{});
        for (unsigned i=0;i<0x2000;++i) actual.memory().write(static_cast<std::uint16_t>(0xc000+i),static_cast<std::uint8_t>(i*37+1));
        actual.memory().write(0xc000,0);
        actual.cpu().hl.uint16_value=0xc000;
        actual.cpu().de.uint16_value=0xc001;
        actual.cpu().bc.uint16_value=0x1fff;
        actual.advance(Machine::frame_cycles-actual.cycles()-8);
        Machine native=actual;
        unsigned slices=0;
        while (actual.cpu().pc.uint16_value==copy.address) {
            actual.step(h.registry());
            while (native.cycles()<actual.cycles()) native.step();
            probe::check(actual.equivalent(native),"reset copy differs at a transfer boundary");
            probe::check(++slices<2000,"reset copy did not finish");
        }
        for (unsigned i=0;i<0x2000;++i)
            probe::check(actual.memory().read(static_cast<std::uint16_t>(0xc000+i))==0,"reset copy missed a work RAM byte");
        probe::check(actual.cpu().bc.uint16_value==0 && actual.cpu().hl.uint16_value==0xdfff &&
                     actual.cpu().de.uint16_value==0xe000 && actual.cycles()>Machine::frame_cycles,
                     "reset copy did not resume through frame boundary");
    }
    for (unsigned value=0;value<256;++value) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,0x0096},setup);
        machine.memory().write(0xc000,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,{0,0x0096});
        probe::check(machine.memory().read(0xc000)==0,"work RAM clear seed differs");
    }
    for (unsigned at:{0x0088u,0x008du,0x0090u,0x0093u}) for (unsigned bank:{0u,1u,15u,31u}) {
        probe::Setup setup; setup.banks={static_cast<std::uint8_t>(bank),static_cast<std::uint8_t>(bank),static_cast<std::uint8_t>(bank)};
        auto machine=h.prepare({0,static_cast<std::uint16_t>(at)},setup);
        if (at!=0x0088) machine.cpu().hl.uint16_value=at==0x008d ? 0xfffc : at==0x0090 ? 0xfffd : 0xfffe;
        h.run_cpp(machine,{0,static_cast<std::uint16_t>(at)});
    }
    for (unsigned value=0;value<256;++value) for (unsigned at:{0x003eu,0x0045u,0x004au}) {
        probe::Setup setup; setup.seed=value;
        auto machine=h.prepare({0,static_cast<std::uint16_t>(at)},setup);
        machine.memory().write(0xc201,static_cast<std::uint8_t>(value));
        h.run_cpp(machine,{0,static_cast<std::uint16_t>(at)});
    }
    // RST 08 remains native: test both pre-existing control-port latch
    // phases and verify the two VDP writes and their timing after the jump.
    for (unsigned latch=0;latch<2;++latch) {
        auto actual=h.prepare({0,0x0045},{});
        if (latch) actual.write_port(0xbf,0x35,actual.cycles());
        Machine native=actual;
        Machine::PortTrace native_ports(native), cpp_ports(actual);
        do native.step(); while (native.cpu().pc.uint16_value!=0x0008);
        h.run_cpp(actual,{0,0x0045});
        do { native.step(); actual.step(); } while (native.cpu().pc.uint16_value!=0x0051);
        probe::check(actual.equivalent(native) && cpp_ports.accesses()==native_ports.accesses(),"display RST port order, latch or state differs");
    }
    {
        probe::Setup setup; setup.sp=0xfffe;
        auto machine=h.prepare({0,0x0045},setup);
        h.run_fallback(machine,{0,0x0045},RoutineFallback::unmapped_stack);
    }
    for (RoutineEntry entry: {RoutineEntry{0,0x0055},RoutineEntry{0,0x00AF}}) {
    for (unsigned mode=0;mode<256;++mode) {
        probe::Setup setup; setup.seed=mode;
        auto machine=h.prepare(entry,setup);
        machine.memory().write(entry.address==0x0055 ? 0xc208 : 0xc202,static_cast<std::uint8_t>(mode));
        h.run_cpp(machine,entry);
    }
    for (unsigned fallback=0;fallback<2;++fallback) {
        auto machine=h.prepare(entry,{});
        if (fallback) machine.schedule_nmi(machine.cycles()+1);
        unsigned calls=0;
        const auto cycles=machine.cycles();
        const auto before=h.routine(entry).counters;
        machine.step([&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
            probe::check(pc==entry.address && mapping==Memory::Mapping{Memory::Kind::rom,0},"wrong mode observer entry");
            probe::check(machine.cycles()==cycles,"mode observer ran late");
            ++calls; return {};
        },&h.registry());
        const auto after=h.routine(entry).counters;
        probe::check(calls==1,"mode observer count differs");
        probe::check(fallback ? after.fallback_calls==before.fallback_calls+1 : after.cpp_calls==before.cpp_calls+1,"mode admission differs");
    }
    if (entry.address==0x0055) {
        auto replaced=h.prepare(entry,{}), original=replaced;
        replaced.memory().write(0xc208,1);
        original.memory().write(0xc208,1);
        unsigned visits=0;
        for (unsigned iteration=0;iteration<5;++iteration) {
            replaced.step(h.registry(),[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                probe::check(pc==0x0055,"wait hook missed its loop boundary");
                ++visits; return {};
            });
            for (unsigned instruction=0;instruction<3;++instruction) original.step();
            probe::check(replaced.equivalent(original),"wait loop iteration differs");
        }
        probe::check(visits==5,"wait hook did not fire on each iteration");
    }
    // Compare host redirection, disabled dispatch and a newly accepted NMI
    // with the native boundary, including the callback cycle.
    for (unsigned action=1;action<4;++action) {
        auto replaced=h.prepare(entry,{}), original=replaced;
        unsigned cpp_calls=0,native_calls=0;
        auto hook=[&](Machine& machine,unsigned& calls)->Machine::Hook {
            return [&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                probe::check(pc==entry.address,"unexpected hook PC");
                ++calls;
                if (action==1) return 0x00BB;
                if (action==3) { machine.schedule_nmi(machine.cycles()+1); machine.advance(3); }
                return {};
            };
        };
        h.registry().enabled=action!=2;
        original.step(hook(original,native_calls));
        replaced.step(h.registry(),hook(replaced,cpp_calls));
        h.registry().enabled=true;
        probe::check(cpp_calls==1 && native_calls==1 && replaced.equivalent(original),"mode hook or redirect differs");
    }
    }
    }
    std::cout<<"Common mode dispatch verified.\n"; return 0;
 } catch (const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
