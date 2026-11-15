// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 11 base checks shared by every map part.
//   no arguments   the planned segment lists and every part's registration:
//                  distinct entries, each inside its own part's code with a
//                  unit of its part, none inside a native stretch (DI to the
//                  instruction after EI) or at a host-observed instruction,
//                  no exit there either, and the six parts together accepted
//                  by the host's guard (no ROM)
//   <rom> <us|br> <recordings folder> [<reference folder>]
//                  the host-observed events with routines on and off: every
//                  native instruction the host watches (the Options and
//                  Language boundary at 00:0B14 among them), with its cycle,
//                  stack pointer and return word, and every write of the
//                  sound request byte with the bank of the code that wrote it,
//                  equal in order on later-town and first-battle
// A ROM run exits 77 without the ROM, or when the later-town start image is
// not available (after running first-battle).
#include "app/frame_driver.h"
#include "map_probe.h"
#include <iostream>
#include <set>
#include <thread>

using namespace psmodern;
using probe::check;
namespace map=game::map;
namespace {
std::string name(RoutineEntry entry) {
    static constexpr char hex[]="0123456789ABCDEF";
    std::string text{hex[(entry.bank>>4)&15],hex[entry.bank&15],':'};
    for (int shift=12;shift>=0;shift-=4) text+=hex[(entry.address>>shift)&15];
    return text;
}
bool inside(RoutineEntry entry, const map::Range& range) {
    return entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last;
}
constexpr std::array<void (*)(RoutineRegistry&),static_cast<unsigned>(map::Part::count)> registrations{
    game::register_map_video,game::register_map_scroll,game::register_map_objects,
    game::register_map_terrain,game::register_map_entrances,game::register_map_modes};
constexpr unsigned part_count=static_cast<unsigned>(map::Part::count);

// An entry or exit the host or a native stretch forbids.
void allowed(RoutineEntry where, const std::string& what) {
    for (const auto& stretch:map::native_stretches)
        check(!inside(where,stretch),what+" "+name(where)+" lies in a stretch that stays native");
    for (const auto watched:app::host_observed_instructions)
        check(where!=watched,what+" "+name(where)+" is an instruction the host watches");
}

void plan() {
    // Ranges of different parts never overlap, so every address has one owner.
    for (unsigned a=0;a<part_count;++a) for (unsigned b=a;b<part_count;++b)
        for (const auto& x:map::part_ranges(static_cast<map::Part>(a)))
            for (const auto& y:map::part_ranges(static_cast<map::Part>(b))) {
                if (&x==&y) continue;
                check(x.bank!=y.bank || x.last<y.first || y.last<x.first,"two map code ranges overlap");
            }
    std::set<RoutineEntry> entries;
    std::size_t total=0;
    for (unsigned p=0;p<part_count;++p) {
        const auto part=static_cast<map::Part>(p);
        std::set<RoutineEntry> units;
        for (const auto& [entry,unit]:map::planned_segments(part)) {
            check(entries.insert(entry).second,"planned map entry "+name(entry)+" is listed twice");
            bool in_part=false;
            for (const auto& range:map::part_ranges(part)) in_part=in_part || inside(entry,range);
            check(in_part,"planned entry "+name(entry)+" lies outside its part's code");
            allowed(entry,"planned entry");
            units.insert(unit);
            ++total;
        }
        // A unit is its own first entry unless its first instruction is a
        // bare native call (the plan names those units with a later entry).
        for (const auto unit:units) {
            bool own=false;
            for (const auto& segment:map::planned_segments(part)) own=own || (segment.entry==unit && segment.unit==unit);
            const std::set<RoutineEntry> native_first{map::full_screen_unit,map::map_object_types[8],map::map_object_types[9],
                map::vehicle_step_unit,map::ground_damage_unit,map::load_map_unit,map::sprite_clear_unit};
            check(own || native_first.count(unit),"unit "+name(unit)+" is not its own entry");
        }
    }
    check(total==153,"153 planned map segments");
    check(entries.count(map::options_boundary)==0,"00:0B14 is not a planned entry");
}

void registrations_together() {
    RoutineRegistry all;
    std::size_t registered=0;
    for (unsigned p=0;p<part_count;++p) {
        const auto part=static_cast<map::Part>(p);
        const auto entries=map_probe::registered_entries(part,registrations[p]);
        RoutineRegistry one;
        registrations[p](one);
        for (const auto& [entry,routine]:one.entries()) {
            allowed(entry,"registered entry");
            for (const auto& exit:routine.spec.exits) {
                allowed({entry.bank,exit.address},"declared exit");
                // The guard compares exit addresses without their bank.
                for (const auto watched:app::host_observed_instructions)
                    check(exit.address!=watched.address,"declared exit at a watched address in "+name(entry));
            }
            check(!routine.spec.force_admit,"map segments are never force-admitted");
        }
        registrations[p](all);   // throws on an entry registered by two parts
        registered+=entries.size();
    }
    check(all.entries().size()==registered,"the six parts register distinct entries");
    app::guard_observed_instructions(all);
    std::cout<<registered<<" of 153 planned map segments registered; plan and registrations pass the base checks.\n";
}

// ---- Host-observed events ------------------------------------------------------------
struct Event {
    std::uint64_t frame;
    char kind;                       // 'i' watched instruction, 'w' sound request write
    unsigned bank=0;
    std::uint16_t pc=0, sp=0, word=0;
    std::uint64_t cycles=0;
    std::uint8_t value=0;
    bool driver_write=false;         // written by the sound driver's bank (12)
    bool operator==(const Event&) const = default;
};
// One machine with the host's watch, run through a whole recording: for each
// frame the number of events, a digest of them in order, and the number of
// arrivals at 00:0B14.
struct FrameEvents {
    std::uint64_t count=0, digest=0xcbf29ce484222325ull, boundary=0;
    bool operator==(const FrameEvents&) const = default;
    void add(const Event& e) {
        ++count;
        if (e.kind=='i' && RoutineEntry{e.bank,e.pc}==map::options_boundary) ++boundary;
        for (const std::uint64_t field:{std::uint64_t(e.kind),std::uint64_t(e.bank),std::uint64_t(e.pc),std::uint64_t(e.sp),
                                        std::uint64_t(e.word),e.cycles,std::uint64_t(e.value),std::uint64_t(e.driver_write)})
            for (unsigned i=0;i<8;++i) { digest^=(field>>(8*i))&0xff; digest*=0x100000001b3ull; }
    }
};
std::vector<FrameEvents> watch(const probe::Rom& rom, const map_probe::StartImage& image, const replay::Recording& recording, bool routines_on) {
    Machine m(rom.image,image);
    RoutineRegistry registry;
    check(game::register_routines(registry,rom.sha1),"identified ROM registry");
    app::guard_observed_instructions(registry);
    registry.enabled=routines_on;
    std::vector<bool> watched_pc(0x10000,false);
    for (const auto watched:app::host_observed_instructions) watched_pc[watched.address]=true;
    std::vector<FrameEvents> frames(recording.frames);
    std::uint64_t frame=0;
    Machine::Hook hook=[&](std::uint16_t pc, Memory::Mapping mapping) -> std::optional<std::uint16_t> {
        if (!watched_pc[pc]) return {};
        for (const auto watched:app::host_observed_instructions)
            if (watched.address==pc && mapping==Memory::Mapping{Memory::Kind::rom,watched.bank}) {
                const auto sp=m.cpu().sp.uint16_value;
                const auto word=static_cast<std::uint16_t>(m.peek(sp)|(m.peek(static_cast<std::uint16_t>(sp+1))<<8));
                frames[frame].add({frame,'i',watched.bank,pc,sp,word,m.cycles()});
            }
        return {};
    };
    Machine::WriteObserver writes=[&](std::uint16_t address, std::uint8_t value) {
        if ((address&0x1fff)!=4 || m.memory().mapping(address).kind!=Memory::Kind::ram) return;
        const auto writer=m.memory().mapping(m.cpu().pc.uint16_value);
        frames[frame].add({frame,'w',0,0,0,0,0,value,writer==Memory::Mapping{Memory::Kind::rom,12}});
    };
    for (;frame<recording.frames;++frame) {
        m.run_frame(recording.input_at(frame),registry,hook,writes);
        m.take_psg_writes(); m.take_samples();
    }
    return frames;
}
// Routines on and off run on two threads; their events must agree frame by frame.
void compare_events(const map_probe::Inputs& inputs, const std::string& stem, const map_probe::StartImage& image, const replay::Recording& recording) {
    std::vector<FrameEvents> off;
    std::exception_ptr failure;
    std::thread native([&] { try { off=watch(inputs.rom,image,recording,false); } catch (...) { failure=std::current_exception(); } });
    std::vector<FrameEvents> on;
    try { on=watch(inputs.rom,image,recording,true); } catch (...) { native.join(); throw; }
    native.join();
    if (failure) std::rethrow_exception(failure);
    std::uint64_t total=0, boundary=0;
    for (std::uint64_t frame=0;frame<recording.frames;++frame) {
        check(on[frame]==off[frame],stem+": host-observed events differ in frame "+std::to_string(frame)+": routines off "+
              std::to_string(off[frame].count)+" events, routines on "+std::to_string(on[frame].count)+" (or their order, cycles or values)");
        total+=on[frame].count; boundary+=on[frame].boundary;
    }
    check(boundary>0,stem+": the map's Options boundary 00:0B14 was never reached");
    std::cout<<stem<<": "<<total<<" host-observed events equal with routines on and off ("<<boundary<<" at 00:0B14).\n";
}
}

int main(int argc, char** argv) {
    try {
        plan();
        registrations_together();
        if (argc==1) return 0;
        const auto inputs=map_probe::load(argc,argv);
        if (!inputs) return 77;
        check(!inputs->recordings.empty(),"the observed-events test needs the recordings folder");
        bool skipped=false;
        for (const std::string stem:{"first-battle","later-town"}) {
            const auto recording=replay::read(inputs->recording(stem));
            check(recording.rom_sha1==inputs->rom.sha1,stem+" was recorded with another ROM");
            const auto image=map_probe::start_image(*inputs,stem,recording);
            if (!image) { std::cout<<stem<<": start image missing (reference folder not configured), skipped.\n"; skipped=true; continue; }
            compare_events(*inputs,stem,*image,recording);
        }
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
