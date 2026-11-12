// SPDX-License-Identifier: GPL-3.0-or-later
// State probes for the random-number routine (00:05B1): every generator
// state, every refresh-counter value, both IFF2 values, and the mapping and
// stack variations of the probe harness, each verified call by call against
// the original and checked against an independent description of the result.
#include "probe.h"
#include <chrono>
#include <iostream>

using namespace psmodern;
using probe::check;
namespace {

// Independent description: the state shifts left by one and enters at bit 0
// the exclusive-or of its bits 15, 13, 4 and 0. Only the all-zero state
// would map to zero; it is replaced by a fixed state taken from the ROM.
struct Expected { std::uint16_t state; bool recovered; };
Expected next(std::uint16_t state, std::uint16_t recovery) {
    const unsigned feedback=((state>>15)^(state>>13)^(state>>4)^state)&1;
    const auto shifted=static_cast<std::uint16_t>((state<<1)|feedback);
    return shifted ? Expected{shifted,false} : Expected{recovery,true};
}

struct Probe {
    probe::Harness& harness;
    std::uint16_t recovery;
    std::uint16_t state_address; // the operand of the state load at 00:05B2, read from the image
    unsigned calls=0, recoveries=0;

    // Runs one probe. The stack may alias the state, so the state as read is
    // whatever the push left there (given by the caller as `as_read`). When
    // the stored state overwrites the return address, the original's 21 or 22
    // instructions are compared instead of the verifier's stop at the return.
    void run(probe::Setup setup, std::uint16_t state, std::optional<std::uint16_t> as_read={}, bool own_return=false) {
        auto machine=harness.prepare(game::random_entry,setup);
        auto& memory=machine.memory();
        if (!setup.work_ram_disabled) {
            memory.write(state_address,static_cast<std::uint8_t>(state));
            memory.write(state_address+1,static_cast<std::uint8_t>(state>>8));
        }
        const auto entry_hl=machine.cpu().hl.uint16_value;
        const auto r=machine.cpu().r;
        const auto stack=machine.cpu().sp.uint16_value;
        const auto expected=next(as_read.value_or(state),recovery);
        const auto result=own_return ? harness.run_cpp_against_steps(machine,game::random_entry,expected.recovered ? 22 : 21) :
            harness.run_cpp(machine,game::random_entry);
        ++calls;
        recoveries+=expected.recovered;
        check(result.cycles==(expected.recovered ? 156u : 151u),"cycles differ from the path");
        check(result.fetches==(expected.recovered ? 24u : 23u),"opcode fetches differ from the path");
        const auto& cpu=machine.cpu();
        check(cpu.sp.uint16_value==static_cast<std::uint16_t>(stack+2),"did not return");
        if (setup.work_ram_disabled || as_read) return; // the stored state and HL are then elsewhere
        check((memory.read(state_address)|(memory.read(state_address+1)<<8))==expected.state,"stored state differs");
        // R counts the fetches up to and including the refresh read, keeping bit 7.
        const unsigned before_read=expected.recovered ? 20 : 19;
        const auto refresh=static_cast<std::uint8_t>((r&0x80)|((r+before_read)&0x7f));
        const auto draw=static_cast<std::uint8_t>(refresh^static_cast<std::uint8_t>(expected.state));
        check(cpu.af.uint8_values.at_1==draw,"returned draw differs");
        check((cpu.af.uint8_values.at_0&0x41)==(draw ? 0 : 0x40),"zero and carry flags of the draw differ");
        if (setup.sp>=0xc000 && setup.sp<0xfffb) check(cpu.hl.uint16_value==entry_hl,"HL not restored");
    }
};
}

int main(int argc, char** argv) { try {
    if (argc!=3) return 1;
    const auto rom=probe::load(argv[1],argv[2]);
    if (!rom) { std::cout<<"ROM path missing; skipping.\n"; return 77; }
    const auto start=std::chrono::steady_clock::now();
    probe::Harness harness(*rom);
    const auto& spec=harness.routine(game::random_entry).spec;
    check(spec.unit==game::random_entry && spec.scratch_stack==2 && !spec.slot2_mapper &&
          spec.exits.size()==1 && spec.exits[0].same_declaration({RoutineExit::returned,0x05CF}),"random segment declaration");
    // The recovery state is the operand of the instruction at 00:05C5.
    Probe p{harness,static_cast<std::uint16_t>(rom->byte(0,0x05C6)|(rom->byte(0,0x05C7)<<8)),
        static_cast<std::uint16_t>(rom->byte(0,0x05B3)|(rom->byte(0,0x05B4)<<8))};

    // The harness catches a wrong body: the same routine starting with R one
    // fetch ahead must be reported as a divergence.
    {
        const auto& real=harness.routine(game::random_entry);
        RoutineRegistry sabotaged;
        replay::CallVerifier verifier("sabotage");
        sabotaged.verifier=&verifier;
        sabotaged.add(game::random_entry,real.spec,real.bound,[body=real.body](RoutineContext& c) {
            auto& r=c.cpu().r; r=static_cast<std::uint8_t>((r&0x80)|((r+1)&0x7f)); body(c);
        });
        auto machine=harness.prepare(game::random_entry,{});
        bool caught=false;
        try { machine.step(sabotaged); } catch (const std::runtime_error& e) { caught=std::string(e.what()).find("Divergence")==0; }
        check(caught,"the verifier did not catch a sabotaged body");
    }

    // Every state with both IFF2 values. R varies with the state so that all
    // 256 refresh values occur on every 256 consecutive states; IFF1 varies
    // independently.
    for (unsigned state=0;state<0x10000;++state)
        for (bool iff2:{false,true}) {
            probe::Setup setup;
            setup.seed=state*2+iff2;
            setup.r=static_cast<std::uint8_t>(state+(state>>8)+(iff2 ? 0x5b : 0));
            setup.iff2=iff2;
            setup.iff1=iff2 && (state&4);
            p.run(setup,static_cast<std::uint16_t>(state));
        }
    check(p.recoveries==2,"only the zero state recovers");

    // Every refresh value with both IFF2 values on both paths.
    for (unsigned r=0;r<256;++r)
        for (bool iff2:{false,true})
            for (std::uint16_t state:{std::uint16_t{0},std::uint16_t{0x8000},std::uint16_t{0x1234}}) {
                probe::Setup setup; setup.r=static_cast<std::uint8_t>(r); setup.iff1=setup.iff2=iff2; setup.seed=r*7+iff2;
                p.run(setup,state);
            }
    // The 7-bit wrap during the routine's own fetches, both bit-7 values.
    for (auto r:probe::r_wrap_values(24))
        for (std::uint16_t state:{std::uint16_t{0},std::uint16_t{0x4d2f}}) {
            probe::Setup setup; setup.r=r; setup.seed=r; p.run(setup,state);
        }

    // Slot 2 holding other ROM banks or cartridge RAM changes nothing here.
    for (const auto& setup:probe::slot2_variants({}))
        for (std::uint16_t state:{std::uint16_t{0},std::uint16_t{1},std::uint16_t{0xffff}}) p.run(setup,state);

    // Stack placements: admitted ones run in C++ (a ROM stack reads ROM bytes
    // back into HL); the edge cases fall back with unmapped_stack.
    for (const auto& c:probe::stack_cases()) {
        probe::Setup setup; setup.sp=c.sp; setup.cartridge_ram=c.cartridge_ram; setup.work_ram_disabled=c.work_ram_disabled;
        for (std::uint16_t state:{std::uint16_t{0},std::uint16_t{0x2b01}}) {
            setup.seed=c.sp+state;
            if (c.admitted(spec.scratch_stack)) {
                // With work RAM disabled the state reads the open bus: the
                // operand's high byte, twice.
                const auto bus=rom->byte(0,0x05B4);
                if (c.work_ram_disabled) p.run(setup,state,static_cast<std::uint16_t>(bus|(bus<<8)));
                else p.run(setup,state);
            } else {
                auto machine=harness.prepare(game::random_entry,setup);
                harness.run_fallback(machine,game::random_entry,RoutineFallback::unmapped_stack);
            }
        }
    }
    // The scratch stack on the state itself: the saved pair is what the
    // routine then reads as its state (fully or one byte of it), also through
    // the work-RAM mirror. At $C20D the stored state also overwrites the low
    // byte of the return address, so that call returns elsewhere.
    for (std::uint16_t sp:{std::uint16_t{0xc20e},std::uint16_t{0xe20e},std::uint16_t{0xc20f},std::uint16_t{0xc20d}})
        for (std::uint16_t state:{std::uint16_t{0},std::uint16_t{0x55aa}})
            for (std::uint64_t seed:{1u,2u,3u}) {
                probe::Setup setup; setup.sp=sp; setup.seed=seed*0x10000+sp;
                auto machine=harness.prepare(game::random_entry,setup);
                const auto hl=machine.cpu().hl.uint16_value;
                const auto h=static_cast<std::uint8_t>(hl>>8), l=static_cast<std::uint8_t>(hl);
                std::uint16_t read=state;
                if ((sp&0x1fff)==0x020e) read=hl;
                if ((sp&0x1fff)==0x020f) read=static_cast<std::uint16_t>((state&0xff)|(l<<8));
                if ((sp&0x1fff)==0x020d) read=static_cast<std::uint16_t>((state&0xff00)|h);
                p.run(setup,state,read,(sp&0x1fff)==0x020d);
            }

    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<p.calls<<" random-number probes in C++ ("<<p.recoveries<<" zero-state recoveries), "
             "verifier clean; 65536 states x IFF2, R x IFF2 x both paths, R wrap, slot 2, stack placements; "<<seconds<<" s.\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
