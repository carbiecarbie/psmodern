// SPDX-License-Identifier: GPL-3.0-or-later
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
}
int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
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
        probe::check(same(actual,native) && actual.cpu().sp.uint16_value==0xcb00 && cpp_visits==1 && native_visits==1 &&
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
            probe::check(same(actual,native),"pause wait changed a pending NMI boundary");
            if(actual.cpu().pc.uint16_value==0x0200) break;
        }
    }
    for (unsigned seed=0;seed<64;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare({0,0x014F},setup);
        for (unsigned i=0;i<14;++i) machine.memory().write(static_cast<std::uint16_t>(setup.sp+i),static_cast<std::uint8_t>(seed+i*19));
        h.run_cpp(machine,{0,0x014F});
        auto original=machine;
        // Milestone 14: the return after EI is its own segment (00:0158).
        h.run_cpp(machine,{0,0x0158}); original.step();
        probe::check(same(machine,original),"EI-delayed return differs");
    }
    // The interrupt-enable at the handler's end with the frame interrupt
    // already pending: the return after EI runs first (in C++, compared with
    // the oracle), and the interrupt is accepted only at the boundary after it.
    for (unsigned seed=0;seed<8;++seed) {
        probe::Setup setup; setup.seed=seed;
        auto machine=h.prepare({0,0x014F},setup);
        machine.write_port(0xbf,0x20,machine.cycles());
        machine.write_port(0xbf,0x81,machine.cycles());
        machine.advance(Machine::frame_cycles);
        probe::check(machine.vdp().irq(),"pending frame interrupt fixture did not assert");
        // The six saved register pairs, then the interrupted code's address.
        machine.memory().write(static_cast<std::uint16_t>(setup.sp+12),0x80);
        machine.memory().write(static_cast<std::uint16_t>(setup.sp+13),0x01);
        h.run_cpp(machine,{0,0x014F});
        probe::check(machine.cpu().iff1 && machine.cpu().pc.uint16_value==0x0158,"interrupts not enabled before the return");
        h.run_cpp(machine,{0,0x0158});
        probe::check(machine.cpu().pc.uint16_value==0x0180,"the return after EI was not first");
        oracle::Lockstep lockstep(machine,"interrupt after the handler's return",oracle::Mode::instructions);
        lockstep.step(machine,&h.registry());
        probe::check(machine.cpu().pc.uint16_value==0x0038,"the pending interrupt was not accepted after the return");
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
            probe::check(same(actual,native),"sprite clear differs at a transfer boundary");
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
    // (Milestone 14: 00:0066 is a single-instruction segment; its dispatch
    // clears the reject marker at the entry boundary, as the core does.)
    for (unsigned mode : {0u,3u,8u,9u,10u,11u,255u}) {
        auto machine=h.prepare({0,0x0055},{});
        machine.memory().write(0xc202,static_cast<std::uint8_t>(mode));
        machine.schedule_nmi(machine.cycles());
        machine.advance(1);
        oracle::NativeMachine native(machine);
        const auto* vector=h.registry().find({0,0x0066});
        probe::check(vector && vector->spec.single_instruction,"NMI vector is a single-instruction segment");
        probe::check(machine.step(h.registry())==11 && native.step()==11 && same(machine,native),"NMI entry differs");
        probe::check(machine.cpu().pc.uint16_value==0x0066 && (machine.cpu().request&Z80::request_reject_nmi),"NMI vector rejection missing");
        const auto before=machine.cpu().sp.uint16_value;
        const auto calls=vector->counters.cpp_calls;
        probe::check(machine.step(h.registry())==11 && native.step()==11 && same(machine,native),"NMI PUSH differs");
        probe::check(vector->counters.cpp_calls==calls+1,"NMI PUSH did not run in C++");
        probe::check(machine.cpu().pc.uint16_value==0x0067 && machine.cpu().sp.uint16_value==before-2 && !(machine.cpu().request&Z80::request_reject_nmi),"NMI continuation boundary differs");
        h.run_cpp(machine,{0,0x0067});
        while (native.cycles()<machine.cycles()) native.step();
        probe::check(same(native,machine),"translated NMI continuation differs");
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
            probe::check(same(actual,native),"reset copy differs at a transfer boundary");
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
        probe::check(same(actual,native) && cpp_ports.accesses()==native_ports.accesses(),"display RST port order, latch or state differs");
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
        // Milestone 14: the segment is exact, so the NMI is taken inside it (or
        // after its single instruction) and the hook also sees the handler's
        // native boundaries at depth 1; the entry's observation is at depth 0.
        machine.step([&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
            if (machine.depth()) return {};
            probe::check(pc==entry.address && mapping==Memory::Mapping{Memory::Kind::rom,0},"wrong mode observer entry");
            probe::check(machine.cycles()==cycles,"mode observer ran late");
            ++calls; return {};
        },&h.registry());
        const auto after=h.routine(entry).counters;
        probe::check(calls==1,"mode observer count differs");
        probe::check(after.cpp_calls==before.cpp_calls+1,"mode admission differs");
    }
    if (entry.address==0x0055) {
        auto replaced=h.prepare(entry,{}), original=replaced;
        replaced.memory().write(0xc208,1);
        original.memory().write(0xc208,1);
        unsigned visits=0;
        // Milestone 14: each of the wait's three instructions is a top-level
        // step with its final rendered (00:0055, 00:0058, 00:0059), and the
        // hook sees only the observed poll. The original is the oracle; the
        // machines are compared with the documented equality (the pinned
        // core's per-step scratch is not state, and the engine runs no core
        // step here).
        oracle::NativeMachine reference(original);
        for (unsigned iteration=0;iteration<5;++iteration) {
            for (unsigned instruction=0;instruction<3;++instruction)
                replaced.step(h.registry(),[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                    probe::check(pc==0x0055,"wait hook missed its loop boundary");
                    visits+=pc==0x0055; return {};
                });
            for (unsigned instruction=0;instruction<3;++instruction) reference.step();
            probe::check(same(replaced,reference),"wait loop iteration differs");
        }
        probe::check(visits==5,"wait hook did not fire on each iteration");
    }
    // Compare a host redirection and a newly accepted NMI (the callback
    // advancing the clock) with the original, which the oracle runs from the
    // state the hook leaves. (Milestone 14: after a redirect the segment at
    // the target runs; dispatch can no longer be disabled.)
    for (unsigned action:{1u,3u}) {
        auto replaced=h.prepare(entry,{});
        oracle::NativeMachine original(replaced);
        unsigned calls=0;
        replaced.step(h.registry(),[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
            probe::check(pc==entry.address,"unexpected hook PC");
            ++calls;
            if (action==1) return 0x00BB;
            replaced.schedule_nmi(replaced.cycles()+1); replaced.advance(3);
            return {};
        });
        if (action==1) original.cpu().pc.uint16_value=0x00BB;
        else { original.schedule_nmi(original.cycles()+1); original.advance(3); }
        while (original.cycles()<replaced.cycles()) original.step();
        probe::check(calls==1 && same(replaced,original),"mode hook or redirect differs");
    }
    }
    }
    probe::Harness h(*rom);
    // Power-on to the main loop under strict lockstep (milestone 14): every
    // instruction of 00:0000-00AF runs in C++ (the joining instructions are
    // segments too), compared with the oracle at every boundary, through the
    // reset code's calls (sound, VDP registers, video timing, tables).
    {
        auto machine=h.prepare({0,0x0000},{});
        oracle::Lockstep lockstep(machine,"power-on",oracle::Mode::instructions);
        for (unsigned steps=0;!(machine.cpu().pc.uint16_value==0x00AF && machine.cpu().sp.uint16_value==0xcb00);++steps) {
            probe::check(steps<2000000,"power-on did not reach the main loop");
            lockstep.step(machine,&h.registry());
            // The host drains the sound queues every frame (both machines).
            if (steps%256==0) {
                (void)machine.take_psg_writes(); (void)machine.take_samples();
                (void)lockstep.oracle().take_psg_writes(); (void)lockstep.oracle().take_samples();
            }
        }
        for (std::uint16_t at:{0x0000,0x0001,0x0003,0x0006,0x0084,0x0085,0x0088,0x008D,0x0090,0x0093,0x0096,0x00A0})
            probe::check(h.routine({0,at}).counters.cpp_calls>0 && !h.routine({0,at}).counters.fallback_calls,
                         "reset segment did not run in C++: 00:"+std::to_string(at));
        std::cout<<"power-on: "<<lockstep.comparisons()<<" comparisons under strict lockstep\n";
    }
    // Reset abandonment on the real code (review-2 of Part 0a): the Reset
    // button is pressed (its previous state, kept in work RAM by the
    // interrupt handler, released), and the frame interrupt lands at every
    // boundary of the main loop's dispatch body (00:00AF, exact). The
    // handler's reset check (00:010F) renders its branch at 00:011A to the
    // reset code at 00:0084, which abandons the suspended body; the reset
    // code then runs as segments. Compared with the oracle at every
    // instruction, nested handler included.
    {
        const RoutineEntry body{0,0x00AF}, reset_check{0,0x010F}, reset{0,0x0084};
        unsigned abandoned_runs=0;
        for (std::uint64_t offset=0;offset<60;++offset) {
            auto m=h.prepare(body,{.iff1=true,.iff2=true,.seed=7});
            const auto previous=static_cast<std::uint16_t>(m.peek(0x0114)|(m.peek(0x0115)<<8));
            m.memory().write(previous,0xFF);
            m.set_reset(true);
            m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles+1);
            (void)m.read_port(0xBF,m.cycles()); m.advance(1);
            m.cpu().iff1=m.cpu().iff2=1;
            const auto irq=m.next_irq();
            probe::check(irq && *irq>m.cycles()+offset,"reset probe needs the frame interrupt ahead");
            m.advance(*irq-m.cycles()-offset);
            const auto before=h.routine(body).counters.abandoned;
            const auto rendered=h.routine(reset_check).rendered.at(0), resets=h.routine(reset).counters.cpp_calls;
            oracle::Lockstep lockstep(m,"reset +"+std::to_string(offset),oracle::Mode::instructions);
            for (unsigned steps=0;steps<400 && !(m.cpu().pc.uint16_value>=0x0088 && m.cpu().pc.uint16_value<0x00AF &&
                                                  m.cpu().sp.uint16_value==0xCB00);++steps)
                lockstep.step(m,&h.registry());
            probe::check(m.depth()==0 && m.cpu().sp.uint16_value==0xCB00,"the engine did not reach the reset code at top level");
            probe::check(h.routine(reset_check).rendered.at(0)>rendered && h.routine(reset).counters.cpp_calls>resets,
                         "the reset branch or the reset code did not run in C++");
            if (h.routine(body).counters.abandoned>before) ++abandoned_runs;
        }
        probe::check(abandoned_runs>0,"no run abandoned the main loop's body");
        std::cout<<"reset: 60 runs, "<<abandoned_runs<<" abandoned the suspended 00:00AF body under strict lockstep\n";
    }
    // Interrupt sweeps (recipe step 5): an IRQ and an NMI at every cycle (or
    // every `stride` cycles) of each segment of more than one instruction.
    auto sweep=[&](std::uint16_t at, const std::function<void(Machine&)>& adjust={}, unsigned stride=1) {
        probe::sweep(h,{0,at},{.iff1=true,.iff2=true,.seed=7},adjust,stride);
    };
    auto stack=[](std::vector<std::uint8_t> bytes) {
        return [bytes](Machine& m) {
            std::uint16_t at=m.cpu().sp.uint16_value;
            for (auto b:bytes) m.memory().write(at++,b);
        };
    };
    sweep(0x0038);
    for (std::uint16_t at:{0x003E,0x0045,0x004A}) sweep(at);
    for (std::uint16_t at:{0x0067,0x006E,0x0072,0x0076}) sweep(at,[](Machine& m) { m.memory().write(0xc202,9); m.cpu().af.uint8_values.at_1=9; });
    for (std::uint16_t at:{0x007A,0x0081}) sweep(at,stack({0x34,0x12,0x00,0x02}));
    sweep(0x0088);
    for (std::uint16_t at:{0x008D,0x0090,0x0093})
        sweep(at,[at](Machine& m) { m.cpu().hl.uint16_value=at==0x008D ? 0xfffc : at==0x0090 ? 0xfffd : 0xfffe; });
    sweep(0x0096);
    sweep(0x00A0,[](Machine& m) { m.cpu().hl.uint16_value=0xc000; m.cpu().de.uint16_value=0xc001; m.cpu().bc.uint16_value=0x1fff; });
    sweep(0x00AF);
    sweep(0x00F4,[](Machine& m) { m.memory().write(0xc212,1); });
    sweep(0x00FB);
    sweep(0x010F);
    for (std::uint16_t at:{0x011D,0x012A}) sweep(at);
    sweep(0x0124,{},2);
    for (std::uint16_t at:{0x0126,0x0128}) sweep(at,[](Machine& m) { m.cpu().bc.uint8_values.at_1=0; },32);
    for (std::uint16_t at:{0x0143,0x0147}) sweep(at,stack({0xa5,3,0x5a,8}));
    sweep(0x014F,stack({1,2,3,4,5,6,7,8,9,10,11,12,0x80,0x01}));
    sweep(0x02FD,{},4);
    sweep(0x030E,[](Machine& m) { m.cpu().hl.uint16_value=0xc940; m.cpu().de.uint16_value=0xc941; m.cpu().bc.uint16_value=0x00bf; });
    sweep(0x0310);
    for (std::uint16_t at:{0x0318,0x031B,0x0321,0x0327,0x032E}) sweep(at);
    // The controller poll (00:0339, routines.cpp), now an exact segment:
    // every button state of the first pad against a previous held byte,
    // under strict lockstep, and its sweep.
    for (unsigned current=0;current<64;++current) {
        auto m=h.prepare(game::controller_poll_entry,{.seed=current});
        m.set_pad(0,Pad{bool(current&1),bool(current&2),bool(current&4),bool(current&8),bool(current&16),bool(current&32)});
        m.advance(1);
        m.memory().write(0xc204,static_cast<std::uint8_t>(current*37));
        h.run_cpp(m,game::controller_poll_entry);
        probe::check(m.memory().read(0xc205)==static_cast<std::uint8_t>(m.memory().read(0xc204)&~(current*37)),
                     "pressed buttons differ");
    }
    sweep(game::controller_poll_entry.address);
    std::cout<<"Common mode dispatch verified.\n"; return 0;
 } catch (const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
