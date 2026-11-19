// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 11, part video: the picture's work in the frame interrupt.
//   no arguments                       registration catalog (no ROM)
//   <rom> <us|br> [<recordings folder> [<reference folder>]]
//                                      state probes and scenario probes on
//                                      that ROM (tests/map_probe.h)
// A ROM run exits 77 without the ROM.
//
// The ROM run has three parts:
//   - the ROM's code shape the segments rely on (the unrolled OUTI tail, the
//     jump targets into it, the kind table of 01:61F5), read from the image;
//   - state probes from power-on: every one of the 27 entries with random
//     inputs biased towards each branch, every stack placement and every
//     slot-2 mapping, each call verified against the original and required to
//     cost exactly its announced bound; every declared exit must be taken;
//   - state probes from a real map frame of later-town (needs the reference
//     folder), the same entries over the game's own RAM and VRAM.
#include "game/map_kit.h"
#include "map_probe.h"
#include <vector>
#include <functional>
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <chrono>
#include <sstream>

using namespace psmodern;
using probe::check;
namespace map=game::map;
namespace kit=game::kit;
namespace {
constexpr map::Part part=map::Part::video;
constexpr RoutineEntry at0(std::uint16_t address) { return {0,address}; }
constexpr RoutineEntry at1(std::uint16_t address) { return {1,address}; }

// ---- Registration catalog --------------------------------------------------------
SegmentExit R(std::uint16_t address, std::int8_t stack=0) { return kit::returned_at(address,stack); }
SegmentExit F(std::uint16_t address) { return kit::core_final_at(address); }
SegmentExit X(std::uint16_t address) { return kit::conditional_at(address); }
SegmentExit XL(std::uint16_t address, std::uint16_t after) { return game::map_kit::conditional_loop_at(address,after); }

struct Want {
    RoutineEntry entry, unit;
    std::vector<SegmentExit> exits;
    kit::Permissions permissions={};
    bool single=false;          // a one-instruction joining segment (milestone 14)
};
std::vector<Want> wanted() {
    const auto handler=map::frame_handler_unit, loading=map::load_frame_handler_unit, sprites=map::sprite_upload_unit,
        party=map::party_frames_unit, animators=map::tile_animators_unit;
    const std::vector<SegmentExit> loading_loop{XL(0x02D2,128),F(0x02D9)}, slots{X(0x6214),R(0x621E)},
        tiles{XL(0x633C,32),XL(0x6340,32),R(0x6344,+2)}, tail{R(0x599E)};
    std::vector<Want> w{
        {handler,handler,{F(0x0247)}},
        {loading,loading,{F(0x02B1)}},{at0(0x02B4),loading,loading_loop,{.scratch_stack=2}},
        {at0(0x02D0),loading,loading_loop},{at0(0x02D5),loading,loading_loop},
        {sprites,sprites,tail,{.scratch_stack=2}},{at1(0x589E),sprites,tail},{at1(0x595E),sprites,tail},
        {party,party,slots,{.slot2_mapper=true}},{at1(0x6217),party,slots},
        {at1(0x6229),party,tail,{.scratch_stack=2}},{at1(0x6246),party,tail,{.scratch_stack=2}},
        {at1(0x6263),party,tail,{.scratch_stack=2}},{at1(0x6280),party,tail,{.scratch_stack=2}},
        {at1(0x6293),party,{R(0x629C),R(0x599E)},{.slot2_mapper=true,.scratch_stack=2}},
        {animators,animators,{F(0x62CA)},{.slot2_mapper=true}},
        {at1(0x630A),animators,{R(0x630C),R(0x6311),XL(0x633C,32),XL(0x6340,32),R(0x6344,+2)},{.scratch_stack=2,.inherited_stack=2}},
        {at1(0x6337),animators,tiles,{.inherited_stack=2}},
        {at1(0x6339),animators,tiles,{.inherited_stack=2}},{at1(0x633F),animators,tiles,{.inherited_stack=2}},{at1(0x6343),animators,{R(0x6344,+2)},{.inherited_stack=2}},
        {map::strip_upload_unit,map::strip_upload_unit,{R(0x73D5),R(0x741B),R(0x744A)},{.slot2_mapper=true,.scratch_stack=4}},
        {map::colour_cycle_unit,map::colour_cycle_unit,{R(0x7C1C),R(0x7C29),R(0x7C38),R(0x7C3D),R(0x7C6C),R(0x599E)},{.scratch_stack=2}},
        // Joining stretches (milestone 14): the tail's RET, the 512-byte frame after its VDP address, the
        // animators' RET and tile loop, and the strip's column loop with its RET.
        {at1(0x599E),sprites,{R(0x599E)},{},true},{at1(0x62AE),party,{R(0x599E)},{.scratch_stack=2}},
        {at1(0x6309),animators,{R(0x6309)},{},true},{at1(0x6334),animators,tiles,{.inherited_stack=2}},
        {at1(0x7403),map::strip_upload_unit,{R(0x741B)},{.scratch_stack=4}},{at1(0x741B),map::strip_upload_unit,{R(0x741B)},{},true},
    };
    for (const std::uint16_t call:{0x62CD,0x62D9,0x62E5,0x62F1,0x62FD})
        w.push_back({at1(call),animators,{F(static_cast<std::uint16_t>(call+9))}});
    return w;
}
void registration() {
    const auto entries=map_probe::registered_entries(part,game::register_map_video);
    RoutineRegistry registry;
    game::register_map_video(registry);
    const auto want=wanted();
    // The planned list includes the two entries added inside 01:62BC (the
    // resumes of the inner loop exit at 01:633C).
    check(want.size()==map::video_segments.size() && entries.size()==want.size(),"the 34 planned video segments are registered");
    for (const auto& w:want) {
        const auto listed=std::find_if(map::video_segments.begin(),map::video_segments.end(),[&](const auto& s) { return s.entry==w.entry; });
        check(listed!=map::video_segments.end() && listed->unit==w.unit,"a video entry is planned with its unit");
        const auto* routine=registry.find(w.entry);
        check(routine!=nullptr,"a planned video entry is registered");
        const auto& spec=routine->spec;
        check(spec.unit==w.unit && spec.slot2_mapper==w.permissions.slot2_mapper && spec.scratch_stack==w.permissions.scratch_stack &&
              spec.inherited_stack==w.permissions.inherited_stack && spec.single_instruction==w.single,
              "video entry "+std::to_string(w.entry.address)+" has the unit and permissions of the 4V table");
        check(spec.exits.size()==w.exits.size(),"video entry "+std::to_string(w.entry.address)+" declares the exits of the 4V table");
        for (const auto& exit:w.exits)
            check(std::count_if(spec.exits.begin(),spec.exits.end(),[&](const auto& e) { return e.same_declaration(exit); })==1,
                  "an exit of the 4V table is declared exactly as written");
    }
    std::cout<<entries.size()<<" video segments registered (all planned) with the exits and permissions of the 4V table.\n";
}

// ---- The ROM's code shape ------------------------------------------------------------------
std::uint16_t word(const probe::Rom& rom, unsigned bank, std::uint16_t address) {
    return static_cast<std::uint16_t>(rom.byte(bank,address)|(rom.byte(bank,static_cast<std::uint16_t>(address+1))<<8));
}
void code_shape(const probe::Rom& rom) {
    // 01:589E-01:599C: 128 OUTI, then RET.
    for (std::uint16_t a=0x589E;a<0x599E;a+=2) check(rom.byte(1,a)==0xED && rom.byte(1,a+1)==0xA3,"the upload tail is unrolled OUTI");
    check(rom.byte(1,0x599E)==0xC9,"the upload tail ends with RET");
    // The calls and jumps the segments render inline land in the tail.
    const std::pair<std::uint16_t,std::uint16_t> into[]{{0x5894,0x591E},{0x6240,0x589E},{0x6243,0x591E},{0x625D,0x589E},{0x6260,0x591E},
        {0x627A,0x589E},{0x627D,0x591E},{0x6290,0x589E},{0x62B0,0x589E},{0x62B3,0x589E},{0x62B6,0x589E},{0x62B9,0x589E},{0x7C82,0x595E}};
    for (const auto& [from,target]:into) check(word(rom,1,from+1)==target,"a call or jump into the upload tail has its planned target");
    check(word(rom,0,0x02BE)==0x595E,"the loading handler calls the 32-byte tail");
    // RST 08 is the VDP address helper.
    for (const std::uint16_t a:{0x5891,0x589D,0x623D,0x625A,0x6277,0x628D,0x62AD,0x6333,0x7404,0x743C,0x7C63,0x7C7F})
        check(rom.byte(1,a)==0xCF,"an RST 08 is where the segments render it");
    check(rom.byte(0,0x02BA)==0xCF,"the loading handler's RST 08");
    // The kind table of 01:61F5 (HL = 621Dh, kind 1-5).
    const std::uint16_t kinds[]{0x6229,0x6246,0x6263,0x6280,0x6293};
    check(word(rom,1,0x6212)==0x621D,"01:61F5 jumps through the table at 01:621D");
    for (unsigned k=0;k<5;++k) check(word(rom,1,static_cast<std::uint16_t>(0x621F+2*k))==kinds[k],"the kind table lists the five frame uploads");
    std::cout<<"code shape: the unrolled tail, its callers and the kind table are as the segments render them.\n";
}

// ---- State probes -------------------------------------------------------------------------
// The verifier of the harness, wrapped to learn which exit each call took.
// The exit each call of the harness took (milestone 14: from the registry).
struct Spy { std::optional<SegmentExit> last; };

using Prepare=std::function<void(Machine&,std::mt19937_64&)>;
struct Rig {
    probe::Harness harness;
    Spy spy;
    std::mt19937_64 random{0x7C18};
    std::uint64_t seed=1, count=0, refusals=0, unmapped=0, rom_stack_probes=0;
    std::map<RoutineEntry,std::set<std::uint16_t>> reached;
    std::map<RoutineEntry,std::uint64_t> per_entry;
    explicit Rig(const probe::Rom& rom) : harness(rom) { harness.exit_sink=&spy.last; }

    // Admission of a stack placement: SP, SP+1, the scratch bytes below and
    // the inherited bytes above must be mapped and off the mapper registers.
    static bool admitted(const probe::Setup& s, unsigned scratch, unsigned inherited) {
        auto bad=[&](unsigned offset, bool strict) {
            const auto a=static_cast<std::uint16_t>(s.sp+offset);
            return (a>=0xC000 && s.work_ram_disabled) || (strict && a>=0xFFFC);
        };
        bool refused=bad(0,scratch>0) || bad(1,scratch>0);
        for (unsigned i=1;i<=scratch;++i) refused=refused || bad(0x10000-i,true);
        if (inherited) for (unsigned i=0;i<inherited+2;++i) refused=refused || bad(i,true);
        return !refused;
    }
    // The four bytes below SP read back what was written (work RAM on, or cartridge RAM).
    static bool stack_holds(const probe::Setup& s) {
        for (unsigned i=1;i<=4;++i) {
            const auto a=static_cast<std::uint16_t>(s.sp-i);
            if (a>=0xFFFC) return false;
            if (a>=0xC000) { if (s.work_ram_disabled) return false; }
            else if (a<0x8000 || !s.cartridge_ram) return false;
        }
        return true;
    }
    // One probe: admitted states run in C++ with the cost the bound announced
    // and match the original in the whole machine; a refused stack falls back
    // with unmapped_stack; a state the estimate cannot follow (an inline RST 08
    // or call whose return address does not read back, or work RAM off) falls
    // back with frame.
    void run(RoutineEntry entry, probe::Setup setup, const Machine* base, const Prepare& prepare) {
        setup.seed=seed++;
        auto machine=base ? map_probe::prepare_at(*base,entry,setup) : harness.prepare(entry,setup);
        auto& memory=machine.memory();
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        if (prepare) prepare(machine,random);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        const auto& routine=harness.routine(entry);
        // A stack in slot-2 ROM is remapped by a segment that writes the slot-2 register, so the word
        // the original returns through changes with it and the per-call verifier (which recognizes
        // the return by the word read at entry) cannot stop on it. Such probes are not dropped: the
        // C++ call runs unverified and the original is stepped natively until its cycles equal the
        // C++ call's; the whole machines must then match.
        const bool rom_stack=routine.spec.slot2_mapper && setup.sp>=0x8000 && setup.sp<0xC000 && !setup.cartridge_ram;
        if (!admitted(setup,routine.spec.scratch_stack,routine.spec.inherited_stack)) {
            harness.run_fallback(machine,entry,RoutineFallback::unmapped_stack);
            ++unmapped;
            return;
        }
        const auto bound=routine.bound(machine);
        if (bound>Machine::frame_cycles) {
            check(!stack_holds(setup) || setup.work_ram_disabled,"video entry "+std::to_string(entry.address)+" refused a state with a working stack");
            harness.run_fallback(machine,entry,RoutineFallback::frame);
            ++refusals;
            return;
        }
        spy.last.reset();
        probe::Harness::Result result{};
        try {
            if (rom_stack) {
                oracle::NativeMachine original(machine);
                harness.verified=false;
                try { result=harness.run_cpp(machine,entry); } catch (...) { harness.verified=true; throw; }
                harness.verified=true;
                const auto stop=original.cycles()+result.cycles;
                while (original.cycles()<stop) original.step();
                check(original.cycles()==stop,"ROM-stack probe: the original does not stop at the C++ call's cycle count");
                std::string fields;
                for (const auto& difference:oracle::differences(machine,original,oracle::Scope::whole)) fields+=" "+difference;
                check(fields.empty(),"ROM-stack probe: C++ differs from the original at equal cycles:"+fields);
                ++rom_stack_probes;
            } else result=harness.run_cpp(machine,entry);
        }
        catch (const std::exception& error) {
            std::ostringstream text;
            text<<error.what()<<" [entry "<<std::hex<<entry.address<<" seed "<<std::dec<<setup.seed<<" sp "<<std::hex<<setup.sp
                <<" ram_off "<<setup.work_ram_disabled<<" cart "<<setup.cartridge_ram<<" bank2 "<<+setup.banks[2]<<"]";
            throw std::runtime_error(text.str());
        }
        if (rom_stack) {   // no verifier ran, so no exit was observed; the cost is still the bound
            if (!setup.work_ram_disabled) check(result.cycles==bound,"ROM-stack probe cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
            ++count; ++per_entry[entry];
            return;
        }
        check(spy.last.has_value(),"a video probe took no exit");
        if (!setup.work_ram_disabled)
            check(result.cycles==bound,"video entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+
                  " cycles, its bound said "+std::to_string(bound));
        ++count; ++per_entry[entry];
        reached[entry].insert(spy.last->address);
    }
};

std::uint8_t byte(std::mt19937_64& g) { return static_cast<std::uint8_t>(g()); }
bool one_in(std::mt19937_64& g, unsigned n) { return g()%n==0; }
void fill(Machine& m, std::mt19937_64& g, std::uint16_t first, std::uint16_t last) {
    for (unsigned a=first;a<=last;++a) m.memory().write(static_cast<std::uint16_t>(a),byte(g));
}
// A small count biased to the edges: 1, 2, 0 (256 times) or anything.
std::uint8_t count(std::mt19937_64& g) {
    switch (g()%5) { case 0: return 1; case 1: return 2; case 2: return 0; case 3: return static_cast<std::uint8_t>(1+g()%4); default: return byte(g); }
}
std::uint16_t anywhere(std::mt19937_64& g) {
    switch (g()%4) { case 0: return static_cast<std::uint16_t>(0xC000+g()%0x2000); case 1: return static_cast<std::uint16_t>(g()%0x8000);
        case 2: return static_cast<std::uint16_t>(0x8000+g()%0x4000); default: return static_cast<std::uint16_t>(g()); }
}
void data_port(Machine& m) { m.cpu().bc.uint8_values.at_0=0xBE; }

// The inputs each entry reads, randomized; registers the code sets itself are
// left to the seed. Resume entries get the invariants the code before them
// guarantees (C = the VDP data port).
std::vector<std::pair<RoutineEntry,Prepare>> entry_inputs(const probe::Rom& rom) {
    std::vector<std::pair<RoutineEntry,Prepare>> list;
    auto add=[&](RoutineEntry e, Prepare p) { list.emplace_back(e,std::move(p)); };
    auto scroll=[](Machine& m, std::mt19937_64& g) { fill(m,g,0xC300,0xC306); };
    add(map::frame_handler_unit,scroll);
    add(map::load_frame_handler_unit,scroll);
    add(at0(0x02B4),[](Machine& m, std::mt19937_64& g) { fill(m,g,0xC220,0xC23F); fill(m,g,0xD000,0xD0FF); });
    auto name_loop=[](Machine& m, std::mt19937_64& g) {
        data_port(m); m.cpu().af.uint8_values.at_1=count(g); m.cpu().bc.uint8_values.at_1=count(g);
        m.cpu().hl.uint16_value=anywhere(g);
    };
    add(at0(0x02D0),name_loop);
    add(at0(0x02D5),name_loop);
    add(map::sprite_upload_unit,[](Machine& m, std::mt19937_64& g) { fill(m,g,0xC900,0xC9FF); });
    for (const std::uint16_t tail:{0x589E,0x595E})
        add(at1(tail),[](Machine& m, std::mt19937_64& g) { data_port(m); m.cpu().hl.uint16_value=anywhere(g); });
    // 01:61F5: four slots whose frame (+10h) equals the uploaded one (+11h)
    // or not, with kinds 0-5 or anything.
    auto slots=[](Machine& m, std::mt19937_64& g, std::uint16_t base) {
        for (unsigned s=0;s<5;++s) {
            const auto slot=static_cast<std::uint16_t>(base+0x20*s);
            const auto frame=byte(g);
            m.memory().write(static_cast<std::uint16_t>(slot+0x10),frame);
            m.memory().write(static_cast<std::uint16_t>(slot+0x11),one_in(g,2) ? frame : byte(g));
            m.memory().write(static_cast<std::uint16_t>(slot+1),one_in(g,3) ? 0 : one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(1+g()%5));
        }
    };
    add(map::party_frames_unit,[slots](Machine& m, std::mt19937_64& g) { slots(m,g,0xC800); });
    add(at1(0x6217),[slots](Machine& m, std::mt19937_64& g) {
        const auto ix=static_cast<std::uint16_t>(one_in(g,4) ? anywhere(g) : 0xC800+0x20*(g()%4));
        m.cpu().ix_iy[0].uint16_value=ix;
        if (ix>=0xC000 && ix<0xDF00) slots(m,g,static_cast<std::uint16_t>(ix+0x20));
        m.cpu().bc.uint8_values.at_1=one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(1+g()%4);
    });
    for (const std::uint16_t kind:{0x6229,0x6246,0x6263,0x6280})
        add(at1(kind),[](Machine& m, std::mt19937_64& g) { m.cpu().de.uint8_values.at_1=byte(g); });
    add(at1(0x6293),[](Machine& m, std::mt19937_64& g) {
        static constexpr std::uint8_t modes[]{5,9,8,0x0F,0x0D};
        m.memory().write(map::game_mode,one_in(g,5) ? byte(g) : modes[g()%std::size(modes)]);
        m.cpu().de.uint8_values.at_1=byte(g);
    });
    add(map::tile_animators_unit,{});
    for (const std::uint16_t call:{0x62CD,0x62D9,0x62E5,0x62F1,0x62FD}) add(at1(call),{});
    // 01:630A: a record (on, delay, countdown, step) in work RAM, a table of
    // source addresses and a tile count and number; the road index decides
    // the direction.
    add(at1(0x630A),[&rom](Machine& m, std::mt19937_64& g) {
        auto& cpu=m.cpu();
        const auto record=static_cast<std::uint16_t>(one_in(g,6) ? anywhere(g) : 0xC26F+4*(g()%6));
        cpu.hl.uint16_value=record;
        if (record>=0xC000 && record<0xDFF0) {
            m.memory().write(record,one_in(g,4) ? 0 : byte(g));
            m.memory().write(static_cast<std::uint16_t>(record+1),byte(g));
            m.memory().write(static_cast<std::uint16_t>(record+2),one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(g()%2));
            m.memory().write(static_cast<std::uint16_t>(record+3),byte(g));
        }
        m.memory().write(map::road_index,one_in(g,2) ? static_cast<std::uint8_t>(g()%8) : byte(g));
        // The six tables the animators use, or anywhere.
        static constexpr std::uint16_t loads[]{0x62C4,0x62D0,0x62DC,0x62E8,0x62F4,0x6300};
        cpu.de.uint16_value=one_in(g,4) ? anywhere(g) : word(rom,1,static_cast<std::uint16_t>(loads[g()%6]+1));
        cpu.bc.uint8_values.at_1=count(g);
        cpu.bc.uint8_values.at_0=byte(g);
    });
    for (const std::uint16_t resume:{0x6337,0x6339,0x633F})
        add(at1(resume),[](Machine& m, std::mt19937_64& g) {
            data_port(m); m.cpu().af.uint8_values.at_1=count(g); m.cpu().bc.uint8_values.at_1=count(g); m.cpu().hl.uint16_value=anywhere(g);
        });
    add(at1(0x6343),{});
    // Joining stretches (milestone 14): the tail's RET and the animators' RET need nothing; the 512-byte
    // frame after its VDP address, the animator's tile loop and the strip's column loop read the data port,
    // a count and a source pointer.
    for (const std::uint16_t ret:{0x599E,0x6309,0x741B}) add(at1(ret),{});
    add(at1(0x62AE),[](Machine& m, std::mt19937_64& g) { data_port(m); m.cpu().hl.uint16_value=anywhere(g); });
    add(at1(0x6334),[](Machine& m, std::mt19937_64& g) {
        data_port(m); m.cpu().af.uint8_values.at_1=count(g); m.cpu().bc.uint8_values.at_1=count(g); m.cpu().hl.uint16_value=anywhere(g);
    });
    add(at1(0x7403),[](Machine& m, std::mt19937_64& g) {
        data_port(m); m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(1+g()%3);     // a few cells: a long run would
        m.cpu().hl.uint16_value=anywhere(g); m.cpu().de.uint16_value=anywhere(g);          // take a frame interrupt inside its body
    });
    // 01:73D0: the pending strip (none, a row or a column), its bank and the
    // scroll the strip position comes from.
    add(map::strip_upload_unit,[](Machine& m, std::mt19937_64& g) {
        m.memory().write(0xC264,one_in(g,5) ? static_cast<std::uint8_t>(byte(g)&0xF0) : byte(g));
        m.memory().write(0xC263,byte(g));
        fill(m,g,0xC300,0xC306);
        fill(m,g,0xD000,0xD7FF);
    });
    // 01:7C18: on foot, the ship, the vehicles, past them; the countdown about to run out.
    add(map::colour_cycle_unit,[](Machine& m, std::mt19937_64& g) {
        static constexpr std::uint8_t transports[]{0,4,8,0x0C,0x10,0x11,0x0D,0x0E,0x0F,0x12};
        m.memory().write(map::transport,one_in(g,6) ? byte(g) : transports[g()%std::size(transports)]);
        m.memory().write(0xC265,one_in(g,3) ? 0 : byte(g));
        m.memory().write(0xC310,one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(g()%2));
        m.memory().write(0xC30F,one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(g()%4));
        fill(m,g,0xC220,0xC23F);
    });
    return list;
}

// Every declared exit taken somewhere, except those no state can reach from
// that entry: 02B4 passes 128 cycles before the name-table loop can end; from
// 630A the first arrival at 633C is past 32 cycles; from 6337 (B = 20h) the
// second arrival at 633C ends the chunk; from 6339 the earliest arrival at
// 6340 is at 34 cycles; from 633F the arrival at 6340 is at 4 cycles; from 6334 (B = 20h after the two loads,
// like 6337) the second arrival at 633C ends the chunk.
void exits_reached(const Rig& g) {
    const std::set<std::pair<std::uint16_t,std::uint16_t>> never{{0x02B4,0x02D9},{0x630A,0x6340},{0x630A,0x6344},
        {0x6337,0x6340},{0x6337,0x6344},{0x6339,0x6344},{0x633F,0x6340},
        {0x6334,0x6340},{0x6334,0x6344}};
    for (const auto& w:wanted())
        for (const auto& exit:w.exits) {
            if (never.count({w.entry.address,exit.address})) continue;
            const auto found=g.reached.find(w.entry);
            check(found!=g.reached.end() && found->second.count(exit.address),
                  "video entry "+std::to_string(w.entry.address)+" never took its exit "+std::to_string(exit.address));
        }
}

void power_on_probes(const probe::Rom& rom) {
    Rig g(rom);
    const auto list=entry_inputs(rom);
    for (const auto& [entry,prepare]:list) {
        // Random states with the usual stack and banks.
        for (unsigned i=0;i<120;++i) {
            probe::Setup setup;
            setup.iff1=setup.iff2=(i&1);
            setup.r=byte(g.random);
            setup.banks[2]=static_cast<std::uint8_t>(i%3==0 ? g.random()%32 : 3);
            g.run(entry,setup,nullptr,prepare);
        }
        // Every stack placement and every slot-2 mapping.
        for (const auto& c:probe::stack_cases()) {
            probe::Setup setup;
            setup.sp=c.sp; setup.cartridge_ram=c.cartridge_ram; setup.work_ram_disabled=c.work_ram_disabled;
            g.run(entry,setup,nullptr,prepare);
        }
        for (const auto& setup:probe::slot2_variants({})) g.run(entry,setup,nullptr,prepare);
        // A stack in slot-2 ROM (the segments that write the slot-2 register remap it under themselves).
        if (g.harness.routine(entry).spec.slot2_mapper)
            for (const std::uint8_t bank:{0,2,3,5,15,28,31}) for (const std::uint16_t sp:{0x8000,0x9000,0xA123,0xBFFE})
                for (unsigned k=0;k<3;++k) {
                    probe::Setup setup;
                    setup.sp=sp; setup.banks[2]=bank; setup.r=byte(g.random); setup.iff1=setup.iff2=(k&1);
                    g.run(entry,setup,nullptr,prepare);
                }
        // The refresh counter around its wrap.
        for (const auto r:probe::r_wrap_values(8)) { probe::Setup setup; setup.r=r; g.run(entry,setup,nullptr,prepare); }
    }
    exits_reached(g);
    check(g.rom_stack_probes>0,"no slot-2 mapper segment was probed with its stack in slot-2 ROM");
    std::cout<<"power-on probes: "<<g.count<<" calls in C++, "<<g.refusals<<" refused states (frame), "<<g.unmapped<<" refused stacks; "
             <<g.rom_stack_probes<<" of the calls had the stack in slot-2 ROM (compared with the original at equal cycles).\n";
}

// ROM run of the original (routines off): when an animator of 01:62BC fires,
// 01:630A pops 62BC's return address and returns to 62BC's caller, so the
// animators after it are not looked at in that frame (their countdowns stay).
void animator_skip_quirk(const probe::Rom& rom) {
    probe::Harness harness(rom);
    auto ready=[](Machine& m, std::uint16_t record, std::uint8_t on) {
        m.memory().write(record,on); m.memory().write(static_cast<std::uint16_t>(record+1),5);
        m.memory().write(static_cast<std::uint16_t>(record+2),0); m.memory().write(static_cast<std::uint16_t>(record+3),0);
    };
    auto run=[&](std::uint8_t first_on) {
        probe::Setup setup;
        setup.return_address=0x0180;
        auto m=harness.prepare(map::tile_animators_unit,setup);
        for (std::uint16_t r=0xC26F;r<0xC287;r+=4) ready(m,r,0);
        ready(m,0xC26F,first_on);
        ready(m,0xC273,1);
        m.memory().write(map::road_index,0);
        for (unsigned i=0;i<200000 && !(m.cpu().pc.uint16_value==0x0180 && m.cpu().sp.uint16_value==setup.sp+2);++i) m.step();
        check(m.cpu().pc.uint16_value==0x0180,"01:62BC returned to its caller");
        return std::pair{m.peek(0xC271),m.peek(0xC275)};
    };
    const auto both=run(1), second=run(0);
    check(both.first==5 && both.second==0,"with the first animator firing, the second is not looked at (its countdown stays 0)");
    check(second.second==5,"with the first animator off, the second fires (its countdown is reloaded)");
    std::cout<<"ROM run: a firing animator skips the animators after it for the frame.\n";
}

// The same entries over the RAM and VRAM of a real map frame.
bool map_frame_probes(const map_probe::Inputs& inputs) {
    if (inputs.recordings.empty()) { std::cout<<"map frame probes: no recordings folder, skipped.\n"; return false; }
    const std::string stem="later-town";
    const auto recording=replay::read(inputs.recording(stem));
    check(recording.rom_sha1==inputs.rom.sha1,stem+" was recorded with another ROM");
    const auto image=map_probe::start_image(inputs,stem,recording);
    if (!image) { std::cout<<"map frame probes: "<<stem<<" start image missing (reference folder not configured), skipped.\n"; return false; }
    map_probe::Scenario s(inputs.rom,*image,stem);
    std::uint64_t from=0;
    check(s.play(recording,from,map_probe::on_map),stem+" reaches the map");
    // A few frames on the map, then probes from that frame.
    std::uint64_t frames=0;
    s.play(recording,from,[&](const Machine&) { return ++frames>=30; });
    Rig g(inputs.rom);
    const auto list=entry_inputs(inputs.rom);
    for (const auto& [entry,prepare]:list)
        for (unsigned i=0;i<40;++i) {
            probe::Setup setup;
            setup.banks={0,1,static_cast<std::uint8_t>(s.machine().memory().mapping(0x8000).bank)};
            // Half the probes keep the frame's RAM untouched; the resume
            // entries still get C = the data port, as the code before them sets.
            static const std::set<std::uint16_t> resumes{0x02D0,0x02D5,0x589E,0x595E,0x62AE,0x6334,0x6337,0x6339,0x633F,0x7403};
            const Prepare port=[](Machine& m, std::mt19937_64&) { data_port(m); };
            g.run(entry,setup,&s.machine(),i%2 ? prepare : resumes.count(entry.address) ? port : Prepare{});
        }
    const auto [cpp,fallbacks]=s.calls(part);
    std::cout<<"map frame probes on "<<stem<<" (frame "<<s.frame_count()<<"): "<<g.count<<" calls in C++ ("<<g.rom_stack_probes<<" with the stack in slot-2 ROM); the scenario ran "
             <<cpp<<" video calls in C++ and "<<fallbacks<<" natively.\n";
    return true;
}
}

// ---- Interrupt sweeps (milestone 14, recipe step 5) ----------------------------------------------
// An IRQ and an NMI at every cycle of every converted body of this part
// (sampled: about 40 runs per kind), each run compared with the oracle at every
// instruction boundary, handler included. One-instruction segments have no
// boundary inside and are left out. The state is the map's work RAM with
// recognisable bytes, pointers into RAM and the video data port in C.
void sweep_state(RoutineEntry entry, Machine& m) {
    (void)entry;
    auto& cpu=m.cpu();
    for (unsigned a=0xC200;a<0xC700;++a) m.memory().write(static_cast<std::uint16_t>(a),static_cast<std::uint8_t>((a*167u+(a>>8)*13u)>>1));
    cpu.hl.uint16_value=0xC900; cpu.de.uint16_value=0xD000; cpu.bc.uint16_value=0x04BE;
    cpu.ix_iy[0].uint16_value=0xC800; cpu.ix_iy[1].uint16_value=0xC800;
}
void sweeps(const probe::Rom& rom) {
    probe::Harness h(rom);
    std::vector<RoutineEntry> entries;
    for (const auto& s:map::video_segments) entries.push_back(s.entry);
    for (std::uint16_t from=0x58AE;from<0x599E;from+=16) if (from!=0x595E) entries.push_back(at1(from));   // the eight-write slices
    unsigned swept=0;
    std::string failures;     // every failing segment is reported, not only the first
    for (const auto& entry:entries) {
        if (h.routine(entry).spec.single_instruction) continue;
        try {
            probe::Setup setup; setup.iff1=setup.iff2=true; setup.seed=entry.address;
            const auto adjust=[&](Machine& m) { sweep_state(entry,m); };
            auto base=h.prepare(entry,setup);
            adjust(base);
            const auto length=h.run_cpp(base,entry).cycles;
            probe::sweep(h,entry,setup,adjust,std::max<unsigned>(1,static_cast<unsigned>(length/40)));
            ++swept;
        } catch (const std::exception& error) {
            failures+="\n  "+std::to_string(entry.bank)+":"+std::to_string(entry.address)+": "+error.what();
        }
    }
    check(failures.empty(),"map sweeps failed:"+failures);
    std::cout<<"map video: "<<swept<<" segments swept\n";
}

int main(int argc, char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        registration();
        if (argc==1) return 0;
        const auto inputs=map_probe::load(argc,argv);
        if (!inputs) return 77;
        sweeps(inputs->rom);
        const auto start=std::chrono::steady_clock::now();
        auto seconds=[&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); };
        code_shape(inputs->rom);
        animator_skip_quirk(inputs->rom);
        power_on_probes(inputs->rom);
        std::cout<<"  ("<<seconds()<<" s)\n";
        const bool framed=map_frame_probes(*inputs);
        std::cout<<"  ("<<seconds()<<" s)\n";
        return framed ? 0 : 77;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
