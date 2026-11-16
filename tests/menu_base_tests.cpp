// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 12 base checks shared by every menu part.
//   no arguments   the planned segment lists and every part's registration:
//                  ranges that never overlap (each other, the map parts', or
//                  any other system's registered entries), distinct planned
//                  entries inside their own part's code with a unit of the
//                  part, none at an instruction that stays native or at a
//                  host-observed instruction, no exit there either, every
//                  unit its own first entry unless it starts natively, and
//                  the six parts together accepted by the host's guard (no ROM)
//   <rom> <us|br> <recordings folder> [<reference folder> [<work folder>]]
//                  1. the host-observed events with routines on and off: every
//                     native instruction the host watches (the field menu's
//                     cancel point 00:2D93 and the save's observation point
//                     00:1D2D among them), with its cycle, stack pointer and
//                     return word, and every write of the sound request byte
//                     with the bank of the code that wrote it, equal in order
//                     on opening, first-battle, first-dungeon and later-town
//                     (they open the field menu, save and visit the hospital)
//                     and, for the US cartridge, field-menu, which saves twice
//                     and must meet 00:1D2D at least twice;
//                  2. the host's hold of the native field menu at the town
//                     wait 00:0B14 (first-battle) and the dungeon wait 00:0F48
//                     (first-dungeon): two FrameDrivers, routines on and off,
//                     compared every frame through the hold and the close
//                     (needs the work folder)
// A ROM run exits 77 without the ROM, or when a start image or the work
// folder is not available (after running everything else).
#include "menu_probe.h"
#include <filesystem>
#include <iostream>
#include <set>
#include <thread>

using namespace psmodern;
using probe::check;
namespace menu=game::menu;
namespace map=game::map;
namespace {
std::string name(RoutineEntry entry) {
    static constexpr char hex[]="0123456789ABCDEF";
    std::string text{hex[(entry.bank>>4)&15],hex[entry.bank&15],':'};
    for (int shift=12;shift>=0;shift-=4) text+=hex[(entry.address>>shift)&15];
    return text;
}
template<class R> bool inside(RoutineEntry entry, const R& range) {
    return entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last;
}
constexpr std::array<void (*)(RoutineRegistry&),static_cast<unsigned>(menu::Part::count)> registrations{
    game::register_menu_engine,game::register_menu_windows,game::register_menu_select,
    game::register_menu_field,game::register_menu_places,game::register_menu_fades};
constexpr unsigned part_count=static_cast<unsigned>(menu::Part::count);
bool in_menu_ranges(RoutineEntry entry) {
    for (unsigned p=0;p<part_count;++p)
        for (const auto& range:menu::part_ranges(static_cast<menu::Part>(p))) if (inside(entry,range)) return true;
    return false;
}

// An entry or exit the plan or the host forbids.
void allowed(RoutineEntry where, const std::string& what) {
    check(std::find(menu::native_instructions.begin(),menu::native_instructions.end(),where)==menu::native_instructions.end(),
          what+" "+name(where)+" is an instruction that stays native");
    check(!inside(where,menu::save_copy_stretch),what+" "+name(where)+" lies in the save copy stretch");
    for (const auto watched:app::host_observed_instructions)
        check(where!=watched,what+" "+name(where)+" is an instruction the host watches");
}

void plan() {
    // Ranges of different parts never overlap, nor the map parts' ranges.
    for (unsigned a=0;a<part_count;++a) {
        for (unsigned b=a;b<part_count;++b)
            for (const auto& x:menu::part_ranges(static_cast<menu::Part>(a)))
                for (const auto& y:menu::part_ranges(static_cast<menu::Part>(b))) {
                    if (&x==&y) continue;
                    check(x.bank!=y.bank || x.last<y.first || y.last<x.first,"two menu code ranges overlap");
                }
        for (const auto& x:menu::part_ranges(static_cast<menu::Part>(a)))
            for (unsigned m=0;m<static_cast<unsigned>(map::Part::count);++m)
                for (const auto& y:map::part_ranges(static_cast<map::Part>(m)))
                    check(x.bank!=y.bank || x.last<y.first || y.last<x.first,"a menu code range overlaps a map part's");
    }
    // Native instructions, host-observed instructions and the save stretch lie inside the menu code.
    for (const auto native:menu::native_instructions) check(in_menu_ranges(native),"native instruction "+name(native)+" lies outside the menu code");
    for (const auto point:{menu::menu_cancel_point,menu::save_observation_point,menu::host_menu_entry})
        check(in_menu_ranges(point),"host point "+name(point)+" lies outside the menu code");
    check(std::find(menu::native_instructions.begin(),menu::native_instructions.end(),menu::menu_cancel_point)!=menu::native_instructions.end() &&
          std::find(menu::native_instructions.begin(),menu::native_instructions.end(),menu::save_observation_point)!=menu::native_instructions.end(),
          "the host's cancel and observation points stay native");
    std::set<RoutineEntry> entries;
    std::size_t total=0;
    for (unsigned p=0;p<part_count;++p) {
        const auto part=static_cast<menu::Part>(p);
        std::set<RoutineEntry> units;
        for (const auto& [entry,unit]:menu::planned_segments(part)) {
            check(entries.insert(entry).second,"planned menu entry "+name(entry)+" is listed twice");
            bool in_part=false, unit_in_part=false;
            for (const auto& range:menu::part_ranges(part)) { in_part=in_part || inside(entry,range); unit_in_part=unit_in_part || inside(unit,range); }
            check(in_part,"planned entry "+name(entry)+" lies outside its part's code");
            check(unit_in_part,"unit "+name(unit)+" lies outside its part's code");
            allowed(entry,"planned entry");
            units.insert(unit);
            ++total;
        }
        // A unit is its own first entry unless its first instruction stays native.
        for (const auto unit:units) {
            bool own=false;
            for (const auto& segment:menu::planned_segments(part)) own=own || (segment.entry==unit && segment.unit==unit);
            const bool native_first=std::find(menu::native_first_units.begin(),menu::native_first_units.end(),unit)!=menu::native_first_units.end();
            check(own!=native_first,"unit "+name(unit)+(own ? " is its own entry but listed as starting natively" : " is not its own entry"));
        }
    }
    check(total==menu::planned_total,"the planned total matches the six lists");
    check(entries.count(menu::host_menu_entry)==1,"the host's redirect target 00:1BE1 is a planned entry (its first instruction runs on the core on the host path)");
    // Window size sites lie in code of the menu parts or of the text and the scene that open windows.
    check(!menu::window_size_sites.empty(),"window size sites are listed");
}

void registrations_together() {
    RoutineRegistry all;
    std::size_t registered=0;
    for (unsigned p=0;p<part_count;++p) {
        const auto part=static_cast<menu::Part>(p);
        const auto entries=menu_probe::registered_entries(part,registrations[p]);
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
        }
        registrations[p](all);   // throws on an entry registered by two parts
        registered+=entries.size();
    }
    check(all.entries().size()==registered,"the six parts register distinct entries");
    // The plan lists what stays native (menu::native_instructions), so the parts register every planned
    // entry and nothing else outside the plan's own table.
    check(registered==menu::planned_total,"the six parts register exactly the planned segments");
    app::guard_observed_instructions(all);
    // No other system registers inside the menu code, and every menu entry the
    // game registers comes from the six parts.
    RoutineRegistry game_registry;
    check(game::register_routines(game_registry,"07fcf297be4f4c9d92cd3f119a7ac48467e06838"),"identified ROM registry");
    std::size_t in_menu=0;
    for (const auto& [entry,routine]:game_registry.entries())
        if (in_menu_ranges(entry)) { ++in_menu; check(all.find(entry)!=nullptr,"entry "+name(entry)+" in the menu code is registered by another system"); }
    check(in_menu==registered,"the game registers exactly the menu parts' entries in the menu code");
    app::guard_observed_instructions(game_registry);
    std::cout<<registered<<" of "<<menu::planned_total<<" planned menu segments registered; plan and registrations pass the base checks.\n";
}

// ---- 1. Host-observed events ------------------------------------------------------------
struct Event {
    char kind;                       // 'i' watched instruction, 'w' sound request write
    unsigned bank=0;
    std::uint16_t pc=0, sp=0, word=0;
    std::uint64_t cycles=0;
    std::uint8_t value=0;
    bool driver_write=false;         // written by the sound driver's bank (12)
};
struct FrameEvents {
    std::uint64_t count=0, digest=0xcbf29ce484222325ull, cancel=0, save=0;
    bool operator==(const FrameEvents&) const = default;
    void add(const Event& e) {
        ++count;
        if (e.kind=='i' && RoutineEntry{e.bank,e.pc}==menu::menu_cancel_point) ++cancel;
        if (e.kind=='i' && RoutineEntry{e.bank,e.pc}==menu::save_observation_point) ++save;
        for (const std::uint64_t field:{std::uint64_t(e.kind),std::uint64_t(e.bank),std::uint64_t(e.pc),std::uint64_t(e.sp),
                                        std::uint64_t(e.word),e.cycles,std::uint64_t(e.value),std::uint64_t(e.driver_write)})
            for (unsigned i=0;i<8;++i) { digest^=(field>>(8*i))&0xff; digest*=0x100000001b3ull; }
    }
};
std::vector<FrameEvents> watch(const probe::Rom& rom, const menu_probe::StartImage& image, const replay::Recording& recording, bool routines_on) {
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
                frames[frame].add({'i',watched.bank,pc,sp,menu_probe::word(m,sp),m.cycles()});
            }
        return {};
    };
    Machine::WriteObserver writes=[&](std::uint16_t address, std::uint8_t value) {
        if ((address&0x1fff)!=4 || m.memory().mapping(address).kind!=Memory::Kind::ram) return;
        const auto writer=m.memory().mapping(m.cpu().pc.uint16_value);
        frames[frame].add({'w',0,0,0,0,0,value,writer==Memory::Mapping{Memory::Kind::rom,12}});
    };
    for (;frame<recording.frames;++frame) {
        m.run_frame(recording.input_at(frame),registry,hook,writes);
        m.take_psg_writes(); m.take_samples();
    }
    return frames;
}
void compare_events(const menu_probe::Inputs& inputs, const std::string& stem, const menu_probe::StartImage& image, const replay::Recording& recording,
                    std::uint64_t min_saves=0) {
    std::vector<FrameEvents> off;
    std::exception_ptr failure;
    std::thread native([&] { try { off=watch(inputs.rom,image,recording,false); } catch (...) { failure=std::current_exception(); } });
    std::vector<FrameEvents> on;
    try { on=watch(inputs.rom,image,recording,true); } catch (...) { native.join(); throw; }
    native.join();
    if (failure) std::rethrow_exception(failure);
    std::uint64_t total=0, cancel=0, save=0;
    for (std::uint64_t frame=0;frame<recording.frames;++frame) {
        check(on[frame]==off[frame],stem+": host-observed events differ in frame "+std::to_string(frame)+": routines off "+
              std::to_string(off[frame].count)+" events, routines on "+std::to_string(on[frame].count)+" (or their order, cycles or values)");
        total+=on[frame].count; cancel+=on[frame].cancel; save+=on[frame].save;
    }
    check(save>=min_saves,stem+": the recording reaches the save point 00:1D2D "+std::to_string(save)+" times, expected at least "+std::to_string(min_saves));
    std::cout<<stem<<": "<<total<<" host-observed events equal with routines on and off ("<<cancel<<" at 00:2D93, "
             <<save<<" at 00:1D2D).\n";
}

// ---- 2. The host hold -----------------------------------------------------------------------
void hold(const menu_probe::Inputs& inputs, const std::filesystem::path& work) {
    struct Point { const char* stem; std::uint64_t frame; std::uint8_t mode; };
    // The frames of the Options tests (tests/options_native_tests.cpp): a town
    // in first-battle, a dungeon in first-dungeon.
    for (const Point point:{Point{"first-battle",3400,0x09},Point{"first-dungeon",7000,0x0B}}) {
        const auto recording=replay::read(inputs.recording(point.stem));
        check(recording.rom_sha1==inputs.rom.sha1,std::string(point.stem)+" was recorded with another ROM");
        const auto report=menu_probe::hold_compare(inputs.rom,recording,point.frame,90,30,work/point.stem,point.stem);
        check(report.mode==point.mode,std::string(point.stem)+": the hold was not taken at the expected wait");
        std::cout<<point.stem<<": host hold at the "<<(point.mode==0x09 ? "00:0B14" : "00:0F48")<<" wait equal with routines on and off ("
                 <<report.held<<" frames held, "<<report.closing<<" closing, "<<report.menu_cpp<<" C++ calls of menu segments).\n";
    }
}
}

int main(int argc, char** argv) {
    try {
        plan();
        registrations_together();
        if (argc==1) return 0;
        std::filesystem::path work;
        const auto inputs=menu_probe::load(argc,argv,&work);
        if (!inputs) return 77;
        check(!inputs->recordings.empty(),"the observed-events test needs the recordings folder");
        bool skipped=false;
        // field-menu (US only, there is no Brazilian one) saves twice, into a used and an
        // empty slot: 00:1D2D is met more than once.
        std::vector<std::string> stems{"opening","first-battle","first-dungeon","later-town"};
        if (std::filesystem::exists(inputs->recording("field-menu"))) stems.push_back("field-menu");
        for (const std::string& stem:stems) {
            const auto recording=replay::read(inputs->recording(stem));
            check(recording.rom_sha1==inputs->rom.sha1,stem+" was recorded with another ROM");
            const auto image=menu_probe::start_image(*inputs,stem,recording);
            if (!image) { std::cout<<stem<<": start image missing (reference folder not configured), skipped.\n"; skipped=true; continue; }
            compare_events(*inputs,stem,*image,recording,stem=="field-menu" ? 2 : 0);
        }
        if (work.empty()) { std::cout<<"host hold: no work folder given, skipped.\n"; skipped=true; }
        else hold(*inputs,work);
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
