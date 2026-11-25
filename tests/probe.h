// SPDX-License-Identifier: GPL-3.0-or-later
// State probes for rewritten game routines: start from a real power-on machine
// of the identified ROM, set up one entry state, dispatch it once under strict
// lockstep with an oracle (milestone 14: the state is transferred to a native
// machine and both are compared at every instruction boundary) and assert that
// the call really ran in C++. A refused call (the engine stops: it has no
// processor core to run the original) is only expected where a variation is
// meant to cause one.
#pragma once
#include "game/routines.h"
#include "machine/machine.h"
#include "replay/verification.h"
#include "replay/census.h"
#include "oracle/lockstep.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace psmodern::probe {
inline void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }

struct Rom {
    std::shared_ptr<const Memory::Rom> image;
    std::string sha1;
    RomVersion version;
    // The byte at a bank:address of the image, for expectations read from the
    // player's ROM rather than written into the tests.
    std::uint8_t byte(unsigned bank, std::uint16_t address) const { return image->at(bank*0x4000u+(address&0x3fffu)); }
};
// Command-line convention of the ROM tests: <rom path> <us|br>. Returns
// nothing when the path is empty or absent (the caller exits with 77) and
// throws when the file is not the named version.
inline std::optional<Rom> load(const char* path, std::string_view region) {
    check(region=="us" || region=="br","Expected ROM version us or br");
    if (!*path || !std::filesystem::exists(path)) return std::nullopt;
    const auto version=region=="us" ? RomVersion::us_rev_a : RomVersion::brazilian;
    check(identify_rom(path)==version,"Wrong ROM version.");
    std::ifstream file(path,std::ios::binary);
    auto image=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    auto digest=sha1(*image);
    return Rom{std::move(image),std::move(digest),version};
}

// The entry state of one probe. Everything not named here comes from the
// power-on machine, except the unrelated registers randomized from seed.
struct Setup {
    std::array<std::uint8_t,3> banks{0,1,3}; // slot 0-2 ROM banks; the entry's own slot is forced to its bank
    bool cartridge_ram=false;                 // map cartridge RAM into slot 2 through $FFFC
    bool work_ram_disabled=false;             // memory control bit 4: $C000-$FFFF reads the open bus
    std::uint16_t sp=0xdf00;
    std::uint16_t return_address=0x0180;      // written at SP when the stack is writable and below $FFFC
    bool iff1=false, iff2=false;
    std::uint8_t r=0;
    std::uint64_t seed=0;                     // AF, BC, DE, HL, IX, IY, I, MEMPTR, Q and the alternate set
};

// Unrelated state from a fixed seed, so a hidden dependence shows up as a
// divergence on some probe rather than passing by coincidence.
inline void randomize(Z80& cpu, std::uint64_t seed) {
    std::mt19937_64 random(seed*0x9e3779b97f4a7c15ull+0x632be59bd9b4e019ull);
    auto word=[&] { return static_cast<std::uint16_t>(random()); };
    for (auto* pair:{&cpu.af,&cpu.bc,&cpu.de,&cpu.hl,&cpu.af_,&cpu.bc_,&cpu.de_,&cpu.hl_,&cpu.memptr})
        pair->uint16_value=word();
    cpu.ix_iy[0].uint16_value=word(); cpu.ix_iy[1].uint16_value=word();
    cpu.i=static_cast<std::uint8_t>(random());
    cpu.q=static_cast<std::uint8_t>(random());
}

// Sweep results: runs made, interrupts delivered inside the body, and runs in
// which the interrupt fell outside the body (accepted before its entry).
struct SweepResult {
    unsigned runs=0, irq_deliveries=0, nmi_deliveries=0, outside=0;
};
// Evidence that one sweep run's interrupt was taken (milestone 19 review,
// 2026-11-25: a sweep that counts its attempts passes even when nothing is
// injected). Only filled when the caller asks for it (sweep_interrupts'
// `accepted`); every other sweep runs as before.
struct SweepAcceptance {
    int kind=0;                 // 0 IRQ, 1 NMI
    std::uint64_t offset=0;     // cycles from the entry's state
    bool inside_body=false;     // delivered inside the segment's body
    // For an interrupt accepted at a top-level boundary: how many top-level
    // steps had completed since the entry (0: before the segment ran), the
    // PC the response pushed and SP before it, and whether the handler
    // returned to that PC with SP restored (the interrupted code resumed).
    // `top` is the word at SP before the response (the interrupted code's
    // own stack top, a CALL's return address right after a CALL).
    unsigned boundary=0;
    std::uint16_t resume=0, sp=0, vector=0, top=0;
    bool returned=false;
};
// The positions of a sweep. Stride 1 is every cycle of the body's run. Any
// other stride (`auto_stride`: about `sampled_positions` positions per kind
// spread over the whole body, starting at a phase that depends on the entry)
// is a sample, to which one position inside every instruction interval of the
// plain run is always added (review R3 M1: the interval between two
// instruction boundaries, taken from the cycles of the plain run's boundaries,
// at an offset that depends on the entry and the interval), so that every
// boundary of the body is interrupted. The environment variable
// PSMODERN_FULL_SWEEPS (CTest label `full_sweep`) turns every sample into the
// complete sweep, every cycle (milestone 14: the long sweep files keep the
// suite near its time).
inline constexpr unsigned auto_stride=0, sampled_positions=64;
inline bool full_sweeps() {
    const char* value=std::getenv("PSMODERN_FULL_SWEEPS");
    return value && *value && std::string_view(value)!="0";
}

class Harness {
public:
    explicit Harness(Rom rom, const game::Options& options = {})
        : rom_(std::move(rom)), power_on_(rom_.image) {
        // The VDP powers on with its frame interrupt pending. Reading its
        // status clears it, and one cycle later the CPU has sampled the
        // released line, so a probe can set IFF1 without an immediate
        // interrupt. The next one is a whole frame away.
        (void)power_on_.read_port(0xbf,power_on_.cycles());
        power_on_.advance(1);
        check(!power_on_.vdp().irq(),"probe machine still has an interrupt pending");
        check(game::register_routines(registry_,rom_.sha1,options),"identified ROM registry");
    }
    Harness(const Harness&)=delete;
    Harness& operator=(const Harness&)=delete;
    // With PSMODERN_CENSUS_DIR set, each harness leaves the engine census of
    // everything its probes ran there (milestone 14).
    ~Harness() {
        const char* folder=std::getenv("PSMODERN_CENSUS_DIR");
        if (!folder || !*folder) return;
        static unsigned count=0;
        try {
            std::filesystem::create_directories(folder);
            const auto name="probe-"+std::string(rom_.version==RomVersion::us_rev_a ? "us" : "br")+"-"+
                std::to_string(process_id())+"-"+std::to_string(count++)+".tsv";
            std::ofstream out(std::filesystem::path(folder)/name,std::ios::binary);
            replay::write_transition_census(out,registry_);
        } catch (...) {}
    }
    const Rom& rom() const { return rom_; }
    RoutineRegistry& registry() { return registry_; }
    const RoutineRegistry& registry() const { return registry_; }
    // Off for an option state, where the game differs from the original by
    // design: probes then run without the oracle.
    bool verified=true;
    // When set, receives the exit each C++ call of run_cpp took.
    std::optional<SegmentExit>* exit_sink=nullptr;
    const RegisteredRoutine& routine(RoutineEntry entry) const {
        const auto* found=registry_.find(entry);
        check(found!=nullptr,"probe entry is not registered");
        return *found;
    }

    // A power-on machine stopped at the entry, with the setup applied.
    Machine prepare(RoutineEntry entry, const Setup& setup) const {
        Machine machine=power_on_;
        auto& memory=machine.memory();
        auto banks=setup.banks;
        if (entry.address>=0x0400) banks.at(entry.address>>14)=static_cast<std::uint8_t>(entry.bank);
        for (unsigned slot=0;slot<3;++slot) memory.write(static_cast<std::uint16_t>(0xfffd+slot),banks[slot]);
        memory.write(0xfffc,setup.cartridge_ram ? 0x08 : 0x00);
        auto& cpu=machine.cpu();
        randomize(cpu,setup.seed);
        cpu.pc.uint16_value=entry.address;
        cpu.sp.uint16_value=setup.sp;
        cpu.iff1=setup.iff1; cpu.iff2=setup.iff2;
        cpu.r=setup.r;
        if (setup.sp<0xfffc-1) {
            memory.write(setup.sp,static_cast<std::uint8_t>(setup.return_address));
            memory.write(static_cast<std::uint16_t>(setup.sp+1),static_cast<std::uint8_t>(setup.return_address>>8));
        }
        // Last, so that the stack writes above still reach work RAM.
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        return machine;
    }

    struct Result { std::uint64_t cycles, fetches; };
    // One dispatch that must run in C++ (cpp 1, refused 0), prepared on the
    // engine, transferred to an oracle and compared with it under strict
    // lockstep: at every instruction boundary of the segment, inside its body
    // and in any handler delivered there, and as whole machines where the
    // step ends.
    Result run_cpp(Machine& machine, RoutineEntry entry) {
        check(machine.memory().mapping(entry.address)==Memory::Mapping{Memory::Kind::rom,entry.bank} &&
              machine.cpu().pc.uint16_value==entry.address,"probe machine is not at its entry");
        const auto before=routine(entry).counters;
        const auto scaffold=registry_.census().scaffold_steps;
        const auto cycle=machine.cycles(), fetches=machine.opcode_fetches();
        if (verified) {
            oracle::Lockstep lockstep(machine,"probe "+describe(entry,machine),oracle::Mode::instructions);
            lockstep.step(machine,&registry_);
        } else machine.step(registry_);
        if (exit_sink) *exit_sink=routine(entry).last_exit;
        const auto& after=routine(entry).counters;
        check(after.cpp_calls==before.cpp_calls+1 && after.fallback_calls==before.fallback_calls,
              "probe did not run in C++: "+describe(entry,machine)+reasons(before,after));
        check(registry_.census().scaffold_steps==scaffold,"a probe of the game ran test scaffolding: "+describe(entry,machine));
        return {machine.cycles()-cycle,machine.opcode_fetches()-fetches};
    }
    // For a state the lockstep cannot stop on as one step: the original runs
    // a known number of instructions on an oracle instead, and the whole
    // machines are compared through the same equality.
    Result run_cpp_against_steps(Machine& machine, RoutineEntry entry, unsigned instructions) {
        oracle::NativeMachine original(machine);
        for (unsigned i=0;i<instructions;++i) original.step();
        const auto before=routine(entry).counters;
        const auto scaffold=registry_.census().scaffold_steps;
        const auto cycle=machine.cycles(), fetches=machine.opcode_fetches();
        machine.step(registry_);
        const auto& after=routine(entry).counters;
        check(after.cpp_calls==before.cpp_calls+1 && after.fallback_calls==before.fallback_calls,
              "probe did not run in C++: "+describe(entry,machine)+reasons(before,after));
        check(registry_.census().scaffold_steps==scaffold,"a probe of the game ran test scaffolding: "+describe(entry,machine));
        std::string fields;
        for (const auto& difference:oracle::differences(machine,original,oracle::Scope::whole)) fields+=" "+difference;
        for (const auto& difference:oracle::queue_differences(machine,original)) fields+=" "+difference;
        check(fields.empty(),"C++ differs from the original's "+std::to_string(instructions)+" instructions:"+fields);
        return {machine.cycles()-cycle,machine.opcode_fetches()-fetches};
    }
    // One dispatch that must be refused for exactly this reason: the engine
    // stops with UnhandledState (there is no core to run the original) and
    // counts the refusal; nothing is executed. Milestone 14: an exact segment
    // takes interrupts and frame ends at its own boundaries, so for it the
    // window reasons (irq, nmi, ei_delay, and frame when the estimated bound
    // fits in one frame) no longer refuse it: the call must then run in C++
    // with the exact result instead.
    void run_fallback(Machine& machine, RoutineEntry entry, RoutineFallback reason) {
        const auto& r=routine(entry);
        // The guards that still fall back with reason frame: an uncertain
        // bound, or a sprite slice on another port than BE.
        const bool sprite_port=!r.spec.exits.empty() && r.spec.exits.front().sprite_outputs &&
            (machine.cpu().bc.uint16_value&255)!=0xbe;
        const bool window=reason==RoutineFallback::irq || reason==RoutineFallback::nmi ||
            reason==RoutineFallback::ei_delay ||
            (reason==RoutineFallback::frame && r.bound(machine)<=Machine::frame_cycles && !sprite_port);
        if (r.exact && window) { (void)run_cpp(machine,entry); ++exact_instead_; return; }
        const auto before=r.counters;
        const auto pc=machine.cpu().pc.uint16_value, sp=machine.cpu().sp.uint16_value;
        const auto cycle=machine.cycles();
        bool refused=false;
        try { machine.step(registry_); }
        catch (const UnhandledState&) { refused=true; }
        const auto& after=routine(entry).counters;
        check(refused && after.cpp_calls==before.cpp_calls && after.fallback_calls==before.fallback_calls+1 &&
              after.fallback(reason)==before.fallback(reason)+1,"probe was not refused as expected: "+describe(entry,machine));
        check(machine.cpu().pc.uint16_value==pc && machine.cpu().sp.uint16_value==sp && machine.cycles()==cycle,
              "a refused call changed the machine");
    }
    // Calls of run_fallback that ran in C++ instead (see there).
    unsigned exact_instead() const { return exact_instead_; }

    // Forced-interrupt sweep (milestone 14 planning 4): from one prepared
    // state, an IRQ (the frame interrupt moved to that cycle, IFF1 and IFF2
    // set) and, separately, an NMI raised at every `stride`-th cycle of the
    // segment's run. Each run must reach the segment's C++ call and match the
    // oracle at every instruction boundary; an interrupt landing at the entry
    // is accepted first and the segment runs after its handler.
    //
    // With `accepted` (opt-in; nullptr leaves the sweep exactly as before),
    // each run goes on under the same lockstep after the segment's C++ call
    // until its interrupt has been taken: delivered inside the body, or
    // accepted at a top-level boundary (the response step: PC at the vector,
    // the interrupted PC pushed at SP-2) and then returned from (the handler
    // back at the pushed PC with SP restored). A run in which neither happens
    // within the step limit fails, so suppressed injection fails the sweep.
    // One record per run is appended.
    SweepResult sweep_interrupts(RoutineEntry entry, const Setup& setup,
                                 const std::function<void(Machine&)>& adjust = {}, unsigned stride=1,
                                 std::vector<SweepAcceptance>* accepted=nullptr) {
        auto base=prepare(entry,setup);
        if (adjust) adjust(base);
        auto plain=base;
        const auto length=run_cpp(plain,entry).cycles;
        if (stride!=1 && full_sweeps()) stride=1;
        std::uint64_t phase=0;
        if (stride==auto_stride) {
            stride=static_cast<unsigned>(std::max<std::uint64_t>(1,(length+sampled_positions-1)/sampled_positions));
            phase=entry.address%stride;
        }
        // The instruction boundaries of the plain run (cycles from the entry, at
        // the top level of the body), for one position in each interval.
        std::vector<std::uint64_t> boundaries;
        if (stride>1) {
            auto timing=base;
            const auto start=timing.cycles();
            const Machine::BoundaryObserver observer=[&](const Machine::Boundary& at) {
                if (!at.depth) boundaries.push_back(timing.cycles()-start);
            };
            timing.set_boundary_observer(&observer);
            try { timing.step(registry_); } catch (...) { timing.set_boundary_observer(nullptr); throw; }
            timing.set_boundary_observer(nullptr);
            check(timing.cycles()-start==length,"sweep: the timing run of "+describe(entry,timing)+" differs from the plain run");
            boundaries.push_back(length);
            std::sort(boundaries.begin(),boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
        }
        SweepResult result;
        for (int kind=0;kind<2;++kind) {
            std::vector<std::uint64_t> offsets;
            for (std::uint64_t k=phase;k<length;k+=stride) offsets.push_back(k);
            for (std::size_t i=0;i+1<boundaries.size();++i) {
                const auto from=boundaries[i], width=boundaries[i+1]-from;
                const auto mix=(static_cast<std::uint64_t>(entry.bank)<<16|entry.address)*0x9e3779b97f4a7c15ull+i*0xbf58476d1ce4e5b9ull+
                    static_cast<std::uint64_t>(kind)*0x94d049bb133111ebull;
                offsets.push_back(from+(mix>>33)%width);
            }
            std::sort(offsets.begin(),offsets.end());
            offsets.erase(std::unique(offsets.begin(),offsets.end()),offsets.end());
            for (const auto k:offsets) {
                last_sweep_run_={kind,k};
                auto m=base;
                if (kind==0) {
                    m.cpu().iff1=m.cpu().iff2=1;
                    const auto irq=m.next_irq();
                    check(irq && *irq>m.cycles()+k,"sweep needs the frame interrupt enabled and ahead");
                    m.advance(*irq-m.cycles()-k);
                } else m.schedule_nmi(m.cycles()+k);
                const auto before=routine(entry).counters;
                oracle::Lockstep lockstep(m,"sweep "+describe(entry,m)+(kind ? " NMI +" : " IRQ +")+std::to_string(k),
                                          oracle::Mode::instructions);
                SweepAcceptance evidence{kind,k};
                const auto& census=registry_.census();
                bool responded=false;
                unsigned drain=0;
                // One top-level step under lockstep; with evidence asked for,
                // a response of the run's kind is recognized and checked.
                auto step=[&] {
                    const auto pc=m.cpu().pc.uint16_value, sp=m.cpu().sp.uint16_value;
                    const auto top=kind ? census.top_nmi : census.top_irq;
                    const auto word=static_cast<std::uint16_t>(m.peek(sp) | m.peek(static_cast<std::uint16_t>(sp+1))<<8);
                    lockstep.step(m,&registry_);
                    if (!accepted) return;
                    if (++drain%256==0) {      // a long run (a whole handler) drains the sound queues as a frame would
                        (void)m.take_psg_writes(); (void)m.take_samples();
                        (void)lockstep.oracle().take_psg_writes(); (void)lockstep.oracle().take_samples();
                    }
                    if (responded) {
                        if (!evidence.returned && m.depth()==0 && m.cpu().pc.uint16_value==evidence.resume &&
                            m.cpu().sp.uint16_value==evidence.sp) evidence.returned=true;
                        return;
                    }
                    if ((kind ? census.top_nmi : census.top_irq)==top) { ++evidence.boundary; return; }
                    responded=true;
                    evidence.resume=pc; evidence.sp=sp; evidence.vector=m.cpu().pc.uint16_value; evidence.top=word;
                    const auto pushed=static_cast<std::uint16_t>(m.peek(static_cast<std::uint16_t>(sp-2)) |
                                                                 m.peek(static_cast<std::uint16_t>(sp-1))<<8);
                    check(m.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp-2) && pushed==pc &&
                          evidence.vector==(kind ? 0x0066 : 0x0038),
                          "sweep: the response did not push the interrupted PC and jump to the vector: "+describe(entry,m));
                };
                for (unsigned steps=0;routine(entry).counters.cpp_calls==before.cpp_calls;++steps) {
                    check(steps<200000 && routine(entry).counters.fallback_calls==before.fallback_calls,
                          "sweep did not reach the segment in C++: "+describe(entry,m));
                    step();
                }
                if (accepted) {
                    const auto& body=routine(entry).counters;
                    evidence.inside_body=(kind ? body.nmi_deliveries-before.nmi_deliveries : body.irq_deliveries-before.irq_deliveries)>0;
                    for (unsigned steps=0;!evidence.inside_body && !responded;++steps) {
                        check(steps<200000,"sweep: the interrupt was never taken (not injected?): "+describe(entry,m)+
                                               (kind ? " NMI +" : " IRQ +")+std::to_string(k));
                        step();
                    }
                    for (unsigned steps=0;responded && !evidence.returned;++steps) {
                        check(steps<200000,"sweep: the handler never returned to the interrupted code: "+describe(entry,m));
                        step();
                    }
                    accepted->push_back(evidence);
                }
                const auto& after=routine(entry).counters;
                check(registry_.census().scaffold_steps==0,"a sweep of the game ran test scaffolding: "+describe(entry,m));
                ++result.runs;
                result.irq_deliveries+=static_cast<unsigned>(after.irq_deliveries-before.irq_deliveries);
                result.nmi_deliveries+=static_cast<unsigned>(after.nmi_deliveries-before.nmi_deliveries);
                if (after.irq_deliveries==before.irq_deliveries && after.nmi_deliveries==before.nmi_deliveries) ++result.outside;
            }
        }
        return result;
    }
    // The run of the last sweep_interrupts started (or running when it threw):
    // kind 0 an IRQ, 1 an NMI, at `offset` cycles from the entry's state.
    struct SweepRun { int kind=0; std::uint64_t offset=0; };
    SweepRun last_sweep_run() const { return last_sweep_run_; }

private:
    static unsigned long process_id();
    static std::string describe(RoutineEntry entry, const Machine& machine) {
        return "entry "+std::to_string(entry.bank)+":"+std::to_string(entry.address)+
            ", SP "+std::to_string(machine.cpu().sp.uint16_value);
    }
    static std::string reasons(const RoutineCounters& before, const RoutineCounters& after) {
        std::string text;
        for (unsigned i=0;i<routine_fallback_names.size();++i)
            if (after.reasons[i]!=before.reasons[i]) text+=", "+std::string(routine_fallback_names[i]);
        return text;
    }
    Rom rom_;
    Machine power_on_;
    RoutineRegistry registry_;
    unsigned exact_instead_=0;
    SweepRun last_sweep_run_;
};
inline unsigned long Harness::process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(::_getpid());
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

// The interrupt sweep of one converted segment (milestone 14 recipe, step 5):
// an IRQ and an NMI at every cycle of the body's run (every `stride` cycles),
// each run under strict lockstep with the oracle; at least one IRQ and one NMI
// must be delivered inside the body. Prints one line.
inline SweepResult sweep(Harness& harness, RoutineEntry entry, const Setup& setup,
                         const std::function<void(Machine&)>& adjust = {}, unsigned stride=1) {
    const auto result=harness.sweep_interrupts(entry,setup,adjust,stride);
    const auto name=std::to_string(entry.bank)+":"+std::to_string(entry.address);
    check(result.irq_deliveries>0 && result.nmi_deliveries>0,"sweep "+name+": nothing was delivered inside the body");
    std::cout<<"sweep "<<name<<": "<<result.runs<<" runs, "<<result.irq_deliveries<<" IRQ and "<<result.nmi_deliveries
             <<" NMI inside the body\n";
    return result;
}

// Slot-2 mappings the review asked every probe family to vary: the game's
// usual bank 3, other ROM banks (first, last, unrelated) and cartridge RAM.
inline std::vector<Setup> slot2_variants(Setup base) {
    std::vector<Setup> variants;
    for (std::uint8_t bank:{3,0,2,15,31}) { base.banks[2]=bank; base.cartridge_ram=false; variants.push_back(base); }
    base.banks[2]=3; base.cartridge_ram=true; variants.push_back(base);
    return variants;
}

// Refresh-counter values around the 7-bit wrap, with both values of bit 7.
inline std::vector<std::uint8_t> r_wrap_values(unsigned fetches) {
    std::vector<std::uint8_t> values;
    for (unsigned high:{0x00u,0x80u})
        for (unsigned distance=0;distance<=fetches+1;++distance)
            values.push_back(static_cast<std::uint8_t>(high|((0x80u-distance)&0x7f)));
    return values;
}

// Stack placements. A segment with a scratch stack of N bytes is refused
// (unmapped_stack) when any byte from SP-N to SP+1 is unmapped or a mapper
// register; every other placement must run in C++. A ROM stack ignores the
// saves and reads ROM bytes back, exactly as natively.
struct StackCase {
    const char* name;
    std::uint16_t sp;
    bool cartridge_ram=false, work_ram_disabled=false;
    bool admitted(unsigned scratch) const {
        auto unmapped=[&](std::uint16_t address) {
            if (address>=0xc000) return work_ram_disabled || (scratch && address>=0xfffc);
            return false;
        };
        bool bad=unmapped(sp) || unmapped(static_cast<std::uint16_t>(sp+1));
        for (unsigned i=1;i<=scratch;++i) bad=bad || unmapped(static_cast<std::uint16_t>(sp-i));
        return !bad;
    }
};
inline std::vector<StackCase> stack_cases() {
    return {
        {"work RAM",0xdf00}, {"work RAM bottom",0xc002}, {"work RAM mirror",0xe100},
        {"below the mapper registers",0xfffa}, {"slot 1 ROM",0x7000}, {"slot 2 ROM",0x9000},
        {"cartridge RAM",0x9000,true}, {"slot 0 ROM with work RAM disabled",0x2000,false,true},
        {"return address on the mapper registers",0xfffc}, {"return address high byte on a mapper register",0xfffb},
        {"scratch on the mapper registers",0x0000}, {"scratch low byte on a mapper register",0x0001},
        {"scratch through the wrap",0xfffe}, {"work RAM disabled",0xdf00,false,true},
    };
}
}
