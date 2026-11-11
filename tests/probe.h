// SPDX-License-Identifier: GPL-3.0-or-later
// State probes for rewritten game routines: start from a real power-on machine
// of the identified ROM, set up one entry state, dispatch it once with the
// per-call verifier (original first, whole-machine comparison) and assert that
// the call really ran in C++. A probe that falls back verifies nothing, so
// fallbacks are only expected where a variation is meant to cause one.
#pragma once
#include "game/routines.h"
#include "machine/machine.h"
#include "replay/verification.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
    std::uint64_t seed=0;                     // AF, BC, DE, HL, IX, IY, I, MEMPTR, Q, R7 and the alternate set
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
    cpu.r7=static_cast<std::uint8_t>(random()); // R7 is reloaded by every core step
}

class Harness {
public:
    explicit Harness(Rom rom, const game::Options& options = {})
        : rom_(std::move(rom)), power_on_(rom_.image), verifier_("probe") {
        // The VDP powers on with its frame interrupt pending. Reading its
        // status clears it, and one cycle later the CPU has sampled the
        // released line, so a probe can set IFF1 without an immediate
        // interrupt. The next one is a whole frame away.
        (void)power_on_.read_port(0xbf,power_on_.cycles());
        power_on_.advance(1);
        check(!power_on_.vdp().irq(),"probe machine still has an interrupt pending");
        check(game::register_routines(registry_,rom_.sha1,options),"identified ROM registry");
        registry_.verifier=&verifier_;
    }
    const Rom& rom() const { return rom_; }
    RoutineRegistry& registry() { return registry_; }
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
    // One dispatch that must run in C++ (cpp 1, fallback 0, forced as given),
    // checked by the per-call verifier against the original.
    Result run_cpp(Machine& machine, RoutineEntry entry, std::uint64_t forced=0) {
        check(machine.memory().mapping(entry.address)==Memory::Mapping{Memory::Kind::rom,entry.bank} &&
              machine.cpu().pc.uint16_value==entry.address,"probe machine is not at its entry");
        const auto before=routine(entry).counters;
        const auto cycle=machine.cycles(), fetches=machine.opcode_fetches();
        machine.step(registry_);
        const auto& after=routine(entry).counters;
        check(after.cpp_calls==before.cpp_calls+1 && after.fallback_calls==before.fallback_calls &&
              after.forced_calls==before.forced_calls+forced && after.forced_failed==before.forced_failed,
              "probe did not run in C++: "+describe(entry,machine)+reasons(before,after));
        return {machine.cycles()-cycle,machine.opcode_fetches()-fetches};
    }
    // For a state the verifier cannot stop on: a segment that overwrites its
    // own return address (a stack placed on the data it stores) returns
    // elsewhere, and the verifier recognizes a return only by the target it
    // read at entry. Here the original runs a known number of instructions on
    // a copy instead, and the whole machines are compared the same way.
    Result run_cpp_against_steps(Machine& machine, RoutineEntry entry, unsigned instructions) {
        Machine original=machine;
        for (unsigned i=0;i<instructions;++i) original.step();
        registry_.verifier=nullptr;
        Result result{};
        try { result=run_cpp(machine,entry); } catch (...) { registry_.verifier=&verifier_; throw; }
        registry_.verifier=&verifier_;
        std::string fields;
        for (const auto& difference:original.differences(machine)) fields+=" "+difference.field;
        check(fields.empty(),"C++ differs from the original's "+std::to_string(instructions)+" instructions:"+fields);
        return result;
    }
    // One dispatch that must fall back for exactly this reason (the core then
    // executes the entry's first instruction).
    void run_fallback(Machine& machine, RoutineEntry entry, RoutineFallback reason) {
        const auto before=routine(entry).counters;
        machine.step(registry_);
        const auto& after=routine(entry).counters;
        check(after.cpp_calls==before.cpp_calls && after.fallback_calls==before.fallback_calls+1 &&
              after.fallback(reason)==before.fallback(reason)+1,"probe did not fall back as expected: "+describe(entry,machine));
    }

private:
    static std::string describe(RoutineEntry entry, const Machine& machine) {
        return "entry "+std::to_string(entry.bank)+":"+std::to_string(entry.address)+
            ", SP "+std::to_string(machine.cpu().sp.uint16_value);
    }
    static std::string reasons(const RoutineCounters& before, const RoutineCounters& after) {
        std::string text;
        for (unsigned i=0;i<routine_fallback_names.size();++i)
            if (after.reasons[i]!=before.reasons[i] || after.forced_reasons[i]!=before.forced_reasons[i])
                text+=", "+std::string(routine_fallback_names[i]);
        return text;
    }
    Rom rom_;
    Machine power_on_;
    RoutineRegistry registry_;
    replay::CallVerifier verifier_;
};

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

// Stack placements. A segment with a scratch stack of N bytes falls back
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
