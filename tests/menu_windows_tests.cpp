// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 12, part windows: the registration catalog and the probes.
//   no arguments   the part's registration against its plan (every entry
//                  planned with its planned unit, and the exits and
//                  permissions of segments.md section 4W2; never a native
//                  instruction); no ROM
//   <rom> <us|br> <recordings folder> [<reference folder> [<work folder>]]
//                  the part's state and scenario probes on that ROM (exit 77
//                  without the ROM; a missing reference folder skips what needs
//                  it with a message and returns 77 at the end)
// State probes (from a power-on machine, every C++ call checked against the
// original by the per-call verifier): every one of the 125 segments from 24
// seeded states (registers, work RAM with zero-heavy bytes so that both sides
// of every branch are met, interrupt flag, refresh counter, inherited stack
// words), every stack placement of probe::stack_cases() (C++ or the predicted
// unmapped_stack fallback), every slot-2 mapping, and the frame boundary
// swept across each segment's window (C++ at the bound, a frame fallback one
// cycle short). A scenario probe plays recordings with routines on and off.
#include "menu_probe.h"
#include <iostream>
#include <map>
#include <set>

using namespace psmodern;
using probe::check;
namespace menu=game::menu;

namespace {
struct ExitSpec { RoutineExit kind; std::uint16_t address; int stack; bool conditional; };
ExitSpec F(std::uint16_t address, int stack=0) { return {RoutineExit::core_final,address,stack,false}; }
ExitSpec R(std::uint16_t address, int stack=0) { return {RoutineExit::returned,address,stack,false}; }
ExitSpec X(std::uint16_t address, int stack=0) { return {RoutineExit::core_final,address,stack,true}; }
struct Expect { std::uint16_t entry; std::vector<ExitSpec> exits; bool slot2; unsigned scratch, inherited; };
constexpr RoutineEntry at(std::uint16_t address) { return menu::at0(address); }

// segments.md section 4W2, entry by entry.
const std::vector<Expect>& expectations() {
    static const std::vector<Expect> table{
        {0x2EAC,{F(0x2EB5)},false,0,0},
        {0x2EB8,{F(0x2ECA)},false,0,0},
        {0x2ECD,{F(0x2ED6)},false,0,0},
        {0x2ED9,{F(0x2EE2)},false,0,0},
        {0x2EE5,{F(0x2EEF)},false,0,0},
        {0x2EF2,{F(0x2EFC)},false,0,0},
        {0x2EFF,{F(0x2F09)},false,0,0},
        {0x2F0C,{R(0x2F1A),F(0x2F1E)},false,0,0},
        {0x2F16,{R(0x2F1A),F(0x2F1E)},false,0,0},
        {0x2F21,{F(0x2F27)},false,0,0},
        {0x2F2A,{F(0x2F30)},false,0,0},
        {0x2F33,{F(0x2F39)},false,0,0},
        {0x2F3C,{F(0x2F46)},false,0,0},
        {0x2F49,{F(0x2F53)},false,0,0},
        {0x2F56,{F(0x2F60)},false,0,0},
        {0x2F63,{R(0x2F71),F(0x2F75)},false,0,0},
        {0x2F6D,{R(0x2F71),F(0x2F75)},false,0,0},
        {0x2F78,{F(0x2F7E)},false,0,0},
        {0x2F81,{F(0x2F87)},false,0,0},
        {0x2F8A,{F(0x2F90)},false,0,0},
        {0x2F93,{F(0x2F9D,-2)},false,2,0},
        {0x2FA0,{F(0x2F1E,2)},false,0,2},
        {0x30B7,{F(0x30C0)},false,0,0},
        {0x30C3,{F(0x30CC)},false,0,0},
        {0x30CF,{F(0x30D2)},false,0,0},
        {0x30D5,{F(0x30DE)},false,0,0},
        {0x30E1,{F(0x30EA)},false,0,0},
        {0x30ED,{F(0x30F6)},false,0,0},
        {0x30F9,{F(0x3102)},false,0,0},
        {0x3105,{F(0x310E)},false,0,0},
        {0x3111,{F(0x3116)},false,0,0},
        {0x3119,{F(0x311B)},false,0,0},
        {0x311E,{F(0x3124)},false,0,0},
        {0x3127,{R(0x312C),F(0x3136)},false,0,0},
        {0x3139,{F(0x3148,-2)},false,2,0},
        {0x314B,{F(0x3148),F(0x3159,2)},false,0,2},
        {0x319E,{F(0x31AC)},false,0,0},
        {0x31AF,{F(0x31B8)},false,0,0},
        {0x3464,{R(0x3469),F(0x3475)},false,0,0},
        {0x3478,{F(0x3483,-4)},false,4,0},
        {0x3486,{F(0x34A7,2),F(0x34C6,4)},false,0,4},
        {0x34AA,{F(0x34BD,2)},false,0,2},
        {0x34C9,{F(0x34D2)},false,0,0},
        {0x34D5,{F(0x34E1)},false,0,0},
        {0x34E4,{X(0x34E8)},false,0,0},
        {0x34EB,{F(0x350C)},false,0,0},
        {0x350F,{R(0x3520)},false,0,0},
        {0x3521,{F(0x352A)},false,0,0},
        {0x352D,{F(0x3536)},false,0,0},
        {0x353C,{F(0x3547)},false,0,0},
        {0x354A,{F(0x3556)},false,0,0},
        {0x3559,{F(0x355B)},false,0,0},
        {0x355E,{F(0x3556),F(0x3567)},false,0,0},
        {0x3656,{F(0x3660,-2)},false,2,0},
        {0x3663,{R(0x3664,2)},false,0,2},
        {0x3665,{R(0x3669),F(0x3673)},false,0,0},
        {0x3679,{F(0x367F)},false,0,0},
        {0x3682,{R(0x3686),F(0x3690)},false,0,0},
        {0x3696,{F(0x369C)},false,0,0},
        {0x369F,{R(0x36A3),F(0x36AF)},false,0,0},
        {0x36B2,{F(0x36B8)},false,0,0},
        {0x36BB,{R(0x36BF),F(0x36C9)},false,0,0},
        {0x36CC,{R(0x36D0),F(0x36DA)},false,0,0},
        {0x36DD,{F(0x36E6)},false,0,0},
        {0x36E9,{F(0x36EC)},false,0,0},
        {0x36EF,{F(0x36F8)},false,0,0},
        {0x36FB,{F(0x3704)},false,0,0},
        {0x3707,{F(0x3711,-2)},false,2,0},
        {0x3714,{F(0x371D)},false,0,0},
        {0x3720,{F(0x3737)},true,0,2},
        {0x373A,{F(0x373C)},false,0,0},
        {0x373F,{F(0x3737),F(0x3748)},false,0,0},
        {0x374B,{R(0x374C,2)},false,0,2},
        {0x374D,{F(0x3756)},false,0,0},
        {0x3759,{F(0x3762)},false,0,0},
        {0x3765,{F(0x3768)},false,0,0},
        {0x376B,{F(0x3774)},false,0,0},
        {0x3777,{F(0x3780)},false,0,0},
        {0x3783,{F(0x3786)},false,0,0},
        {0x3789,{F(0x3794)},false,0,0},
        {0x3797,{F(0x37A0)},false,0,0},
        {0x37A3,{F(0x37AC)},false,0,0},
        {0x37AF,{F(0x37B2)},false,0,0},
        {0x37B5,{F(0x37C0)},false,0,0},
        {0x37C3,{F(0x37CC)},false,0,0},
        {0x37CF,{F(0x37E7)},false,2,0},
        {0x37EA,{F(0x37F0)},false,0,0},
        {0x37F3,{F(0x37F9)},false,0,0},
        {0x37FC,{F(0x3805)},false,0,0},
        {0x3808,{F(0x380E)},false,0,0},
        {0x3811,{F(0x3817)},false,0,0},
        {0x381A,{F(0x3820)},false,0,0},
        {0x3823,{F(0x3829)},false,0,0},
        {0x382C,{F(0x3832)},false,0,0},
        {0x3835,{F(0x383B)},false,0,0},
        {0x383E,{F(0x3844)},false,0,0},
        {0x3847,{F(0x384D)},false,0,0},
        {0x3850,{F(0x3856)},false,0,0},
        {0x385C,{F(0x3862)},false,0,0},
        {0x38C1,{F(0x38CA)},false,0,0},
        {0x38CD,{F(0x38D6)},false,0,0},
        {0x38D9,{F(0x38E2)},false,0,0},
        {0x38E5,{F(0x3908,-6)},true,6,0},
        {0x390B,{F(0x390F)},false,0,2},
        {0x3912,{F(0x3908),F(0x391F,4)},false,0,4},
        {0x3922,{F(0x3928)},false,0,0},
        {0x392B,{R(0x3939,2)},true,0,2},
        {0x3999,{F(0x39A2)},false,0,0},
        {0x39A5,{F(0x39AE)},false,0,0},
        {0x39B6,{F(0x39BF)},false,0,0},
        {0x39C7,{F(0x39D2)},false,0,0},
        {0x39D5,{R(0x39DC)},false,0,0},
        {0x39DD,{F(0x39E6)},false,0,0},
        {0x39E9,{F(0x39F3,-2)},false,2,0},
        {0x39F6,{F(0x3A01)},false,0,2},
        {0x39F7,{F(0x3A01,-2)},false,2,0},
        {0x3A07,{F(0x3A0D)},false,0,0},
        {0x3A10,{R(0x3A11,2)},false,0,2},
        {0x3A12,{F(0x3A1C,-2)},false,2,0},
        {0x3A1F,{R(0x3A20,2)},false,0,2},
        {0x3A21,{F(0x3A2A)},false,0,0},
        {0x3A2D,{F(0x3A35)},true,0,0},
        {0x3A38,{F(0x3A43)},false,0,0},
        {0x3A46,{F(0x3A51,-4)},false,4,0},
        {0x3A54,{R(0x3A56,4)},false,0,4},
    };
    return table;
}

void catalog() {
    const auto entries=menu_probe::registered_entries(menu::Part::windows,game::register_menu_windows);
    const auto& planned=menu::windows_segments;
    std::cout<<entries.size()<<" of "<<planned.size()<<" planned windows segments registered.\n";
    check(entries.size()==planned.size(),"every planned windows segment is registered");
    check(expectations().size()==planned.size(),"the catalog lists every planned windows segment");
    RoutineRegistry registry;
    game::register_menu_windows(registry);
    for (const auto& expect:expectations()) {
        const auto* routine=registry.find(at(expect.entry));
        check(routine!=nullptr,"a planned windows entry is not registered");
        const auto& spec=routine->spec;
        check(std::any_of(planned.begin(),planned.end(),[&](const auto& s) { return s.entry==at(expect.entry) && s.unit==spec.unit; }),
              "a windows segment names another unit than planned");
        check(spec.exits.size()==expect.exits.size(),"a windows segment declares another number of exits");
        for (std::size_t i=0;i<expect.exits.size();++i) {
            const auto& exit=spec.exits[i];
            check(exit.kind==expect.exits[i].kind && exit.address==expect.exits[i].address && exit.stack==expect.exits[i].stack &&
                  exit.conditional==expect.exits[i].conditional && exit.after==0,"a windows exit differs from the plan");
        }
        check(spec.slot2_mapper==expect.slot2 && spec.scratch_stack==expect.scratch && spec.inherited_stack==expect.inherited &&
              !spec.single_instruction && !spec.force_admit,"a windows segment declares other permissions");
    }
    // No entry or exit at an instruction that stays native, none inside the save copy.
    for (const auto& [entry,routine]:registry.entries()) {
        const auto lies_in_save_copy=[](RoutineEntry e) {
            return e.bank==menu::save_copy_stretch.bank && e.address>=menu::save_copy_stretch.first && e.address<=menu::save_copy_stretch.last;
        };
        check(!lies_in_save_copy(entry),"a windows segment starts inside the save copy");
        for (const auto& exit:routine.spec.exits) {
            const RoutineEntry at_exit=at(exit.address);
            check(std::find(menu::native_instructions.begin(),menu::native_instructions.end(),at_exit)==menu::native_instructions.end() &&
                  at_exit!=menu::menu_cancel_point && at_exit!=menu::save_observation_point,
                  "a windows segment declares an exit at an instruction that stays native");
        }
    }
}

// ---- State probes ---------------------------------------------------------------------------
struct Rig {
    probe::Harness harness;
    std::map<RoutineEntry,unsigned> runs, fallbacks;
    std::uint64_t probes=0, exact=0;
    explicit Rig(const probe::Rom& rom) : harness(rom) {}

    // Work RAM the wrappers read (C200h-C6FFh) from the seed: a third of the
    // bytes zero, a few set to the values the branches compare (the enemy
    // slot cp 49h, the open flag, the counts), others random.
    static void fill(Machine& m, std::uint64_t seed) {
        std::mt19937_64 random(seed*0x9e3779b97f4a7c15ull+11);
        for (unsigned a=0xC200;a<0xC700;++a)
            m.memory().write(static_cast<std::uint16_t>(a),random()%3==0 ? 0 : static_cast<std::uint8_t>(random()));
        if (seed%3==0) m.memory().write(0xC2E6,0x49);
        if (seed%2==0) m.memory().write(menu::dialogue_open,0);
        if (seed%4==1) m.memory().write(menu::inventory_count,0);
        if (seed%4==2) m.memory().write(menu::inventory_count,static_cast<std::uint8_t>(1+random()%8));
        if (seed%5==0) m.memory().write(menu::inventory_count,static_cast<std::uint8_t>(9+random()%12));
        m.memory().write(0xC2C7,static_cast<std::uint8_t>(1+random()%4));
    }
    // The words the segment inherits from an earlier segment of its unit and
    // the return address above them.
    static void stack(Machine& m, std::uint16_t sp, unsigned inherited, std::uint64_t seed) {
        std::mt19937_64 random(seed*0xd1342543de82ef95ull+7);
        const auto put=[&](unsigned offset, std::uint8_t value) {
            const auto address=static_cast<std::uint16_t>(sp+offset);
            if (address<0xFFFC) m.memory().write(address,value);
        };
        for (unsigned i=0;i<inherited;++i) put(i,static_cast<std::uint8_t>(random()));
        put(inherited,0x80); put(inherited+1,0x01);
    }

    // One probe: C++ when the stack is admitted, else a fallback for the
    // unmapped stack.
    void probe_state(const menu::Segment& segment, probe::Setup setup, std::uint64_t seed) {
        const auto entry=segment.entry;
        const auto& spec=harness.routine(entry).spec;
        auto m=harness.prepare(entry,setup);
        if (!setup.work_ram_disabled) { fill(m,seed); stack(m,setup.sp,spec.inherited_stack,seed); }
        ++probes;
        if (!menu_probe::admitted(m,spec)) { harness.run_fallback(m,entry,RoutineFallback::unmapped_stack); ++fallbacks[entry]; return; }
        const auto bound=harness.routine(entry).bound(m);
        // A path that reads unmapped memory (work RAM switched off) has an
        // uncertain cost: the bound is two frames and dispatch falls back.
        if (bound>Machine::frame_cycles-m.cycles()%Machine::frame_cycles) {
            check(setup.work_ram_disabled,"a windows segment fell back for its bound with work RAM present");
            harness.run_fallback(m,entry,RoutineFallback::frame); ++fallbacks[entry]; return;
        }
        const auto result=harness.run_cpp(m,entry);
        check(result.cycles<=bound,"a windows segment ran longer than its bound");
        if (result.cycles==bound) ++exact;
        ++runs[entry];
    }

    void seeds(const menu::Segment& segment) {
        const auto r_values=probe::r_wrap_values(40);
        for (unsigned seed=0;seed<24;++seed) {
            probe::Setup setup; setup.seed=seed+1; setup.iff1=setup.iff2=seed%2==1;
            setup.r=r_values[seed%r_values.size()];
            setup.sp=static_cast<std::uint16_t>(0xDF00-2*(seed%5));
            probe_state(segment,setup,seed);
        }
    }
    void stacks(const menu::Segment& segment) {
        unsigned n=0;
        for (const auto& stack_case:probe::stack_cases()) {
            probe::Setup setup; setup.seed=100+n; setup.sp=stack_case.sp;
            setup.cartridge_ram=stack_case.cartridge_ram; setup.work_ram_disabled=stack_case.work_ram_disabled;
            probe_state(segment,setup,100+n++);
        }
    }
    void slot2(const menu::Segment& segment) {
        unsigned n=0;
        for (auto setup:probe::slot2_variants(probe::Setup{})) { setup.seed=200+n; probe_state(segment,setup,200+n++); }
    }
    // The frame boundary across the segment's window: it runs in C++ with
    // exactly its bound left and falls back (frame) with one cycle less.
    void boundary(const menu::Segment& segment) {
        for (unsigned seed=0;seed<3;++seed) {
            probe::Setup setup; setup.seed=300+seed;
            const auto& spec=harness.routine(segment.entry).spec;
            auto base=harness.prepare(segment.entry,setup);
            fill(base,300+seed); stack(base,setup.sp,spec.inherited_stack,300+seed);
            const auto bound=harness.routine(segment.entry).bound(base);
            check(bound>1 && bound<Machine::frame_cycles,"a windows bound is out of range");
            for (const auto remaining:{bound,bound+1,bound+37}) {
                auto m=base; menu_probe::place_before_boundary(m,remaining);
                check(harness.routine(segment.entry).bound(m)==bound,"a windows bound changed with the cycle");
                (void)harness.run_cpp(m,segment.entry); ++probes;
            }
            auto m=base; menu_probe::place_before_boundary(m,bound-1);
            harness.run_fallback(m,segment.entry,RoutineFallback::frame); ++probes;
        }
    }
};

void state_probes(const probe::Rom& rom, const std::string& version) {
    Rig rig(rom);
    std::set<unsigned> slot2_entries;
    for (const auto& expect:expectations()) if (expect.slot2) slot2_entries.insert(expect.entry);
    for (const auto& segment:menu::windows_segments) {
        rig.seeds(segment);
        rig.stacks(segment);
        rig.boundary(segment);
        if (slot2_entries.count(segment.entry.address)) rig.slot2(segment);
    }
    unsigned never=0;
    for (const auto& segment:menu::windows_segments) if (!rig.runs[segment.entry]) ++never;
    check(never==0,"a windows segment never ran in C++ in the state probes");
    unsigned fell=0;
    for (const auto& [entry,count]:rig.fallbacks) { (void)entry; fell+=count; }
    std::cout<<"menu windows state probes on "<<version<<": "<<rig.probes<<" dispatches, all 125 segments in C++ ("
             <<rig.exact<<" at exactly their bound), "<<fell<<" predicted unmapped-stack fallbacks.\n";
}

// ---- Scenario probes ---------------------------------------------------------------------------
// A recording played with routines on (every C++ call verified) beside routines
// off, the machines compared every frame; the window segments must have run.
// A recording the version does not have is not a skip; a missing start image
// (the reference folder) is, and returns false.
bool scenario(const menu_probe::Inputs& inputs, const std::string& stem, std::uint64_t frames_limit) {
    const auto path=inputs.recording(stem);
    if (!std::filesystem::exists(path)) { std::cout<<"  "<<stem<<": no such recording for this version.\n"; return true; }
    const auto recording=replay::read(path);
    const auto image=menu_probe::start_image(inputs,stem,recording);
    if (!image) { std::cout<<"  "<<stem<<": no start image (reference folder), skipped.\n"; return false; }
    menu_probe::Scenario s(inputs.rom,*image,stem);
    std::uint64_t from=0;
    const auto limit=std::min<std::uint64_t>(frames_limit,recording.frames);
    while (from<limit) s.frame(recording.input_at(from++));
    std::uint64_t cpp=0, fallback=0, entries=0;
    for (const auto& segment:menu::windows_segments) {
        const auto counters=s.counters(segment.entry);
        cpp+=counters.cpp_calls; fallback+=counters.fallback_calls; entries+=counters.cpp_calls+counters.fallback_calls>0;
    }
    check(cpp>0,stem+": no window segment ran in C++");
    std::cout<<"  "<<stem<<": "<<limit<<" frames, "<<entries<<" window entries reached, "<<cpp<<" C++ calls, "<<fallback<<" fallbacks.\n";
    return true;
}
}

int main(int argc, char** argv) {
    try {
        catalog();
        if (argc==1) return 0;
        std::filesystem::path work;
        const auto inputs=menu_probe::load(argc,argv,&work);
        if (!inputs) return 77;
        bool skipped=false;
        state_probes(inputs->rom,inputs->version);
        std::cout<<"menu windows scenario probes on "<<inputs->version<<":\n";
        for (const auto& [stem,frames]:std::vector<std::pair<std::string,std::uint64_t>>{{"field-menu",~0ull},{"first-battle",5000}})
            skipped=!scenario(*inputs,stem,frames) || skipped;
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
