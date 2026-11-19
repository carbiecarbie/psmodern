// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 12, part fades: the registration catalog and the probes.
//   no arguments   the part's registration against its plan (every entry
//                  planned with its unit, or inside the part's ranges with a
//                  unit of the part; never a native instruction), and the
//                  exits and permissions of the design; no ROM
//   <rom> <us|br> <recordings folder> [<reference folder> [<work folder>]]
//                  the part's state and scenario probes on that ROM (exit 77
//                  without the ROM; a missing reference folder skips what needs
//                  it with a message and returns 77 at the end)
//
// The ROM probes:
//   1. power-on state probes of every entry (random work RAM and registers,
//      every colour value of the per-colour steps, the flash counters, every
//      stack placement, slot 2 mapping and refresh wrap): each call runs in
//      C++ against the original at the cost its bound announced, and every
//      declared exit is taken by some probe;
//   2. chains from a real frame (later-town): whole calls of the fade waits,
//      the flashes and the four palette effects, which no recording reaches,
//      run with routines on beside routines off; the two machines must be
//      equal at the end, frame interrupts included.
#include "game/menu_kit.h"
#include "menu_probe.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace psmodern;
using probe::check;
namespace menu=game::menu;
namespace mk=game::menu_kit;

namespace {
// ---- Registration catalog --------------------------------------------------------
SegmentExit R(std::uint16_t address, std::int8_t stack=0) { return mk::returned_at(address,stack); }
SegmentExit F(std::uint16_t address, std::int8_t stack=0) { return mk::core_final_at(address,stack); }

struct Want {
    std::uint16_t entry, unit;
    std::vector<SegmentExit> exits;
    mk::Permissions permissions={};
};
// The design of segments.md section 4D, unchanged.
std::vector<Want> wanted() {
    return {
        {0x7AFD,0x7AFD,{F(0x7B0D)}},
        {0x7B05,0x7B05,{F(0x7B0D)}},
        {0x7B10,0x7B05,{F(0x7B0D),R(0x7B17)}},
        {0x7B18,0x7B18,{F(0x7B35)}},
        {0x7B20,0x7B20,{F(0x7B35)}},
        {0x7B38,0x7B20,{F(0x7B35),R(0x7B3F)}},
        {0x7B40,0x7B40,{R(0x7B44),R(0x7B51),R(0x7B5E),R(0x7B80),R(0x7B91)},{.scratch_stack=2}},
        {0x7B5F,0x7B5F,{R(0x7B61),R(0x7B67),R(0x7B71),R(0x7B75),R(0x7B79)}},
        {0x7B92,0x7B92,{R(0x7B94),R(0x7B9D),R(0x7BA7),R(0x7BAB)}},
        {0x7BAC,0x7BAC,{F(0x7BB9)}},
        {0x7BBC,0x7BAC,{F(0x7BB9),R(0x7BC3)}},
        {0x7BC4,0x7BC4,{F(0x7BE3)}},
        {0x7BE6,0x7BC4,{F(0x7BE3),R(0x7BED)}},
        {0x7BEE,0x7BEE,{R(0x7BF2),R(0x7C06),R(0x7C17)}},
        {0x7C85,0x7C85,{F(0x7C9E,-2)},{.slot2_mapper=true,.scratch_stack=2}},
        {0x7CA1,0x7C85,{F(0x7C9E),R(0x7CA5,2)},{.inherited_stack=2}},
        {0x7CA6,0x7CA6,{F(0x7CB8)},{.slot2_mapper=true}},
        {0x7CBB,0x7CBB,{F(0x7CC0)},{.slot2_mapper=true}},
        {0x7CC9,0x7CBB,{F(0x7CDB,-2)},{.scratch_stack=2}},
        {0x7CDE,0x7CBB,{F(0x7CDB),R(0x7CE3,2)},{.inherited_stack=2}},
        {0x7CE4,0x7CE4,{F(0x7D0E,-2)},{.slot2_mapper=true,.scratch_stack=2}},
        {0x7CF1,0x7CF1,{F(0x7D0E,-2)},{.scratch_stack=2}},
        {0x7D11,0x7CF1,{F(0x7D0E),R(0x7D16,2)},{.inherited_stack=2}},
    };
}
constexpr RoutineEntry at(std::uint16_t address) { return {1,address}; }

void registration() {
    const auto entries=menu_probe::registered_entries(menu::Part::fades,game::register_menu_fades);
    const auto want=wanted();
    check(entries.size()==menu::fades_segments.size() && entries.size()==want.size(),"every planned fades segment is registered, none extra");
    RoutineRegistry registry;
    game::register_menu_fades(registry);
    for (const auto& w:want) {
        const auto listed=std::find_if(menu::fades_segments.begin(),menu::fades_segments.end(),[&](const auto& s) { return s.entry==at(w.entry); });
        check(listed!=menu::fades_segments.end() && listed->unit==at(w.unit),"the entry is planned with its unit");
        const auto* routine=registry.find(at(w.entry));
        check(routine!=nullptr,"the planned entry is registered");
        const auto& spec=routine->spec;
        check(spec.unit==at(w.unit) && spec.slot2_mapper==w.permissions.slot2_mapper && spec.scratch_stack==w.permissions.scratch_stack &&
              spec.inherited_stack==w.permissions.inherited_stack && !spec.single_instruction,"the entry has the unit and permissions of the fades table");
        check(spec.exits.size()==w.exits.size(),"the entry declares the exits of the fades table");
        for (const auto& exit:w.exits)
            check(std::count_if(spec.exits.begin(),spec.exits.end(),[&](const auto& e) { return e.same_declaration(exit); })==1,
                  "an exit of the fades table is declared exactly as written");
    }
    std::cout<<menu::fades_segments.size()<<" planned fades segments registered with their exits and permissions.\n";
}

// ---- Probes ------------------------------------------------------------------------
// The fade state (menu.h names the palette buffers; the rest is cited from the ROM operands).
constexpr std::uint16_t fade_timer=0xC21B, fade_count=0xC21C, fade_divider=0xC21D, palette=menu::palette, target=menu::palette_target;
constexpr std::uint16_t flash_counter=0xC2BE, flash_run=0xC2C0, effect_id=0xC2E6;

std::string hex(unsigned value) { std::ostringstream text; text<<std::hex<<value; return text.str(); }
void write_word(Machine& m, std::uint16_t address, std::uint16_t v) {
    m.memory().write(address,static_cast<std::uint8_t>(v)); m.memory().write(static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(v>>8));
}

// The exit each call of the harness took (milestone 14: from the registry).
struct Spy {
    std::optional<SegmentExit> last;
    // entry -> exit -> calls, from the finals the registry counts per exit (core finals and rendered ones)
    static std::map<RoutineEntry,std::map<std::uint16_t,std::uint64_t>> taken(const RoutineRegistry& registry) {
        std::map<RoutineEntry,std::map<std::uint16_t,std::uint64_t>> result;
        for (const auto& [entry,routine]:registry.entries())
            for (std::size_t i=0;i<routine.spec.exits.size();++i)
                if (routine.rendered[i]) result[entry][routine.spec.exits[i].address]+=routine.rendered[i];
        return result;
    }
};

struct Rig {
    probe::Harness harness;
    Spy spy;
    const probe::Rom& rom;
    std::mt19937_64 random{0xFADE12};
    std::uint64_t seed=1, count=0, fallbacks=0;

    explicit Rig(const probe::Rom& r) : harness(r), rom(r) { harness.exit_sink=&spy.last; }
    std::uint8_t byte() { return static_cast<std::uint8_t>(random()); }
    unsigned below(unsigned n) { return static_cast<unsigned>(random()%n); }

    using Prepare=std::function<void(Machine&)>;
    Machine make(RoutineEntry entry, probe::Setup setup, const Prepare& prepare) {
        setup.seed=seed++;
        auto machine=harness.prepare(entry,setup);
        auto& memory=machine.memory();
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        for (std::uint16_t a=0xC000;a<0xDE00;++a) memory.write(a,byte());
        if (prepare) prepare(machine);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        return machine;
    }
    // One dispatch in C++ against the original at the cost the bound announced; returns the exit taken.
    // `allow_frame`: a state whose estimate is uncertain (a stack in ROM, work RAM off) falls back (frame).
    std::uint16_t dispatch(Machine& machine, RoutineEntry entry, const std::string& what, bool allow_frame=false) {
        const auto bound=harness.routine(entry).bound(machine);
        if (bound>Machine::frame_cycles) {
            check(allow_frame,"a fades segment refused a plain state: "+what);
            harness.run_fallback(machine,entry,RoutineFallback::frame);
            ++fallbacks;
            return 0;
        }
        spy.last.reset();
        probe::Harness::Result result{};
        try { result=harness.run_cpp(machine,entry); }
        catch (const std::exception& error) { throw std::runtime_error(std::string(error.what())+" ["+what+"]"); }
        ++count;
        check(spy.last.has_value(),"the probe's call took no exit: "+what);
        if (!allow_frame) check(result.cycles==bound,what+": cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        return spy.last->address;
    }
    // Entries that pop words an earlier segment pushed find them on the stack: `held` is the word at SP
    // (low byte first), the caller's return address follows it.
    std::uint16_t run(RoutineEntry entry, const Prepare& prepare, const std::string& what, std::optional<std::uint16_t> held=std::nullopt) {
        probe::Setup setup;
        setup.iff1=setup.iff2=(seed&1);
        setup.r=byte();
        if (held) setup.return_address=*held;
        auto machine=make(entry,setup,[&](Machine& m) {
            if (held) write_word(m,static_cast<std::uint16_t>(m.cpu().sp.uint16_value+2),probe::Setup{}.return_address);
            if (prepare) prepare(m);
        });
        return dispatch(machine,entry,what);
    }
};

void set_a(Machine& m, std::uint8_t a) { m.cpu().af.uint8_values.at_1=a; }
void set_bc(Machine& m, std::uint16_t v) { m.cpu().bc.uint16_value=v; }
void set_de(Machine& m, std::uint16_t v) { m.cpu().de.uint16_value=v; }
void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value=v; }
// -- 1. Power-on state probes ----------------------------------------------------------
void wait_probes(Rig& rig) {
    // The four set-ups store the timer pair the ROM loads; the two loop tests take both ways.
    for (const auto& [entry,operand]:{std::pair<std::uint16_t,std::uint16_t>{0x7AFD,0x7AFE},{0x7B05,0x7B06},{0x7B18,0x7B19},{0x7B20,0x7B21}})
        for (unsigned i=0;i<20;++i) {
            probe::Setup setup; setup.seed=rig.seed++; setup.r=rig.byte();
            auto m=rig.make(at(entry),setup,{});
            (void)rig.dispatch(m,at(entry),hex(entry));
            check(m.peek(fade_timer)==(rig.rom.byte(1,operand)&0xFF) && m.peek(fade_count)==rig.rom.byte(1,static_cast<std::uint16_t>(operand+1)),
                  "the fade set-up stores its timer pair "+hex(entry));
            if (entry==0x7B20) for (unsigned k=0;k<32;++k) check(m.peek(static_cast<std::uint16_t>(palette+k))==0,"fade-in clears the palette buffer");
        }
    for (const std::uint16_t entry:{0x7B10,0x7B38})
        for (const std::uint8_t timer:{0x00,0x01,0x10,0x20,0x80,0xFF})
            for (unsigned i=0;i<3;++i)
                (void)rig.run(at(entry),[&](Machine& m) { m.memory().write(fade_timer,timer); set_a(m,rig.byte()); },hex(entry));
}
void flash_probes(Rig& rig) {
    for (unsigned i=0;i<6;++i) (void)rig.run(at(0x7BAC),{},"7BAC");
    for (const std::uint8_t id:{0x46,0x49,0x4A,0x00,0x45,0x47,0x48,0x4B,0xFF})
        for (unsigned i=0;i<3;++i) (void)rig.run(at(0x7BC4),[&](Machine& m) { m.memory().write(effect_id,id); },"7BC4 "+hex(id));
    for (const std::uint16_t entry:{0x7BBC,0x7BE6})
        for (const std::uint8_t counter:{0x00,0x01,0x05,0x0A,0xFF})
            (void)rig.run(at(entry),[&](Machine& m) { m.memory().write(flash_counter,counter); },hex(entry));
    // The flash step: every counter value (odd = paint white from entry L for H colours, even = restore).
    for (unsigned counter=0;counter<256;++counter)
        for (unsigned k=0;k<(counter&1 ? 4u : 1u);++k)
            (void)rig.run(at(0x7BEE),[&](Machine& m) {
                m.memory().write(flash_counter,static_cast<std::uint8_t>(counter));
                const std::uint8_t first=static_cast<std::uint8_t>(rig.below(32)), count=static_cast<std::uint8_t>(1+rig.below(32));
                write_word(m,flash_run,static_cast<std::uint16_t>((count<<8)|first));
            },"7BEE "+hex(counter));
}
void colour_probes(Rig& rig) {
    // 01:7B5F: every colour byte, at a few places of the palette buffer.
    for (unsigned colour=0;colour<256;++colour)
        for (const std::uint16_t where:{palette,static_cast<std::uint16_t>(palette+7),static_cast<std::uint16_t>(palette+31)})
            (void)rig.run(at(0x7B5F),[&](Machine& m) { set_hl(m,where); m.memory().write(where,static_cast<std::uint8_t>(colour)); },"7B5F "+hex(colour));
    // 01:7B92: every pair of 6-bit colours (current, target), the unused top bits random.
    for (unsigned now=0;now<64;++now)
        for (unsigned goal=0;goal<64;++goal)
            (void)rig.run(at(0x7B92),[&](Machine& m) {
                set_de(m,palette+3); set_hl(m,target+3);
                m.memory().write(palette+3,static_cast<std::uint8_t>(now|(rig.below(2) ? 0xC0 : 0)));
                m.memory().write(target+3,static_cast<std::uint8_t>(goal|(rig.below(2) ? 0xC0 : 0)));
            },"7B92 "+hex(now)+" "+hex(goal));
}
void step_probes(Rig& rig) {
    constexpr std::uint8_t dividers[]{0x00,0x01,0x02,0x03,0x7F,0x80,0xFF};
    constexpr std::uint8_t timers[]{0x00,0x01,0x10,0x20,0x7F,0x80,0x81,0x89,0xC0,0xFF};
    for (unsigned i=0;i<1500;++i)
        (void)rig.run(at(0x7B40),[&](Machine& m) {
            auto& memory=m.memory();
            // Mostly the 'time to step' divider, so that the colour loops run.
            memory.write(fade_divider,rig.below(5) ? 0x00 : dividers[rig.below(7)]);
            memory.write(fade_timer,rig.below(4) ? timers[rig.below(10)] : rig.byte());
            memory.write(fade_count,static_cast<std::uint8_t>(1+rig.below(32)));
            for (unsigned k=0;k<32;++k) {
                memory.write(static_cast<std::uint16_t>(palette+k),rig.below(6)==0 ? 0 : static_cast<std::uint8_t>(rig.byte()&(rig.below(3)==0 ? 0x03 : 0x3F)));
                memory.write(static_cast<std::uint16_t>(target+k),static_cast<std::uint8_t>(rig.byte()&0x3F));
            }
        },"7B40");
    // Whole palettes: every channel combination in turn (fade-out one colour at a time, fade-in both ways).
    for (unsigned base=0;base<64;++base)
        for (const std::uint8_t timer:{0x01,0x80,0x81})
            (void)rig.run(at(0x7B40),[&](Machine& m) {
                auto& memory=m.memory();
                memory.write(fade_divider,0); memory.write(fade_timer,timer); memory.write(fade_count,32);
                for (unsigned k=0;k<32;++k) {
                    memory.write(static_cast<std::uint16_t>(palette+k),static_cast<std::uint8_t>((base+k)&0x3F));
                    memory.write(static_cast<std::uint16_t>(target+k),static_cast<std::uint8_t>((base*5+k*3)&0x3F));
                }
            },"7B40 palette");
}
void effect_probes(Rig& rig) {
    // The effects that write $FFFF: the usual bank, other banks, cartridge RAM (slot2_variants runs below).
    for (const std::uint16_t entry:{0x7C85,0x7CA6,0x7CBB,0x7CE4,0x7CF1,0x7CC9})
        for (unsigned i=0;i<6;++i) (void)rig.run(at(entry),{},hex(entry));
    // 01:7CA1: the countdown held on the stack (A in the high byte), low nibble zero or not.
    for (const unsigned a:{0x01u,0x02u,0x10u,0x11u,0x20u,0x6Fu,0x60u,0x61u,0x7Fu,0xFFu,0x00u,0x30u})
        for (unsigned i=0;i<3;++i)
            (void)rig.run(at(0x7CA1),[&](Machine& m) {
                const auto flags=static_cast<std::uint16_t>(rig.byte());
                write_word(m,m.cpu().sp.uint16_value,static_cast<std::uint16_t>((a<<8)|flags));
                set_hl(m,static_cast<std::uint16_t>(0x8000+rig.below(0x3000)));
            },"7CA1 "+hex(a));
    // 01:7CDE: B the inner count, the held BC the outer (B) and a stale C.
    for (const unsigned inner:{1u,2u,8u,0u,255u})
        for (const unsigned outer:{1u,2u,13u,0u})
            (void)rig.run(at(0x7CDE),[&](Machine& m) {
                write_word(m,m.cpu().sp.uint16_value,static_cast<std::uint16_t>((outer<<8)|rig.byte()));
                set_bc(m,static_cast<std::uint16_t>((inner<<8)|rig.byte()));
                set_hl(m,static_cast<std::uint16_t>(0x8000+rig.below(0x3000)));
            },"7CDE");
    // 01:7D11: the same pair for the step shared by two effects.
    for (const unsigned inner:{1u,2u,3u,24u,0u})
        for (const unsigned outer:{1u,2u,9u,24u,3u})
            (void)rig.run(at(0x7D11),[&](Machine& m) {
                write_word(m,m.cpu().sp.uint16_value,static_cast<std::uint16_t>((outer<<8)|(1+rig.below(30))));
                set_bc(m,static_cast<std::uint16_t>((inner<<8)|rig.byte()));
                set_hl(m,static_cast<std::uint16_t>(0x8000+rig.below(0x3000)));
            },"7D11");
}

// Slot 2 mappings, refresh wrap and stack placements on every segment (the entries that pop held words
// use work RAM stacks only: their words and the return address must be real).
bool admitted(const probe::StackCase& stack, const SegmentSpec& spec) {
    if (!stack.admitted(spec.scratch_stack)) return false;
    if (spec.inherited_stack) for (unsigned i=0;i<spec.inherited_stack+2;++i) {
        const auto address=static_cast<std::uint16_t>(stack.sp+i);
        if (address>=0xfffc || (address>=0xc000 && stack.work_ram_disabled)) return false;
    }
    return true;
}
void machine_probes(Rig& rig) {
    for (const auto& w:wanted()) {
        const auto entry=at(w.entry);
        const auto& spec=rig.harness.routine(entry).spec;
        const bool held=spec.inherited_stack!=0;
        auto prepare=[&](Machine& m) {
            if (held) {
                write_word(m,m.cpu().sp.uint16_value,static_cast<std::uint16_t>((1+rig.below(30))<<8|rig.byte()));
                write_word(m,static_cast<std::uint16_t>(m.cpu().sp.uint16_value+2),probe::Setup{}.return_address);
            }
            set_bc(m,static_cast<std::uint16_t>((1+rig.below(8))<<8|rig.byte()));
            m.memory().write(fade_timer,rig.byte()); m.memory().write(fade_divider,0);
        };
        for (auto setup:probe::slot2_variants({})) {
            setup.seed=rig.seed++;
            auto m=rig.make(entry,setup,prepare);
            (void)rig.dispatch(m,entry,"slot2 "+hex(w.entry));
        }
        for (const auto r:probe::r_wrap_values(40)) {
            probe::Setup setup; setup.r=r;
            auto m=rig.make(entry,setup,prepare);
            (void)rig.dispatch(m,entry,"r "+hex(w.entry));
        }
        for (const auto& stack:probe::stack_cases()) {
            if (held && (stack.sp<0xC002 || stack.sp>=0xFFF0 || stack.work_ram_disabled || stack.cartridge_ram)) continue;
            probe::Setup setup; setup.sp=stack.sp; setup.cartridge_ram=stack.cartridge_ram; setup.work_ram_disabled=stack.work_ram_disabled;
            auto m=rig.make(entry,setup,held ? prepare : Rig::Prepare{});
            if (!admitted(stack,spec)) {
                rig.harness.run_fallback(m,entry,RoutineFallback::unmapped_stack);
                ++rig.fallbacks;
                continue;
            }
            // A ROM stack hands back ROM bytes: the inline call of 01:7B40 then returns elsewhere and falls back.
            (void)rig.dispatch(m,entry,std::string("stack ")+stack.name+" "+hex(w.entry),true);
        }
    }
}
// Every declared exit of every entry was taken by some probe.
void exits_covered(const Rig& rig) {
    for (const auto& w:wanted()) {
        const auto taken=Spy::taken(rig.harness.registry());
        const auto found=taken.find(at(w.entry));
        check(found!=taken.end(),"no probe ran entry "+hex(w.entry));
        for (const auto& exit:w.exits)
            check(found->second.count(exit.address)==1,"the probes of "+hex(w.entry)+" never took the exit "+hex(exit.address));
    }
}

// -- 3. Interrupt sweeps (milestone 14) ----------------------------------------------------
// An IRQ and an NMI at every cycle (every fourth for the second variant) of every segment, each run compared
// with the oracle at every instruction boundary. The states are the paths the probes above exercise.
void sweeps(Rig& rig) {
    for (const auto& w:wanted()) {
        const auto entry=at(w.entry);
        const bool held=rig.harness.routine(entry).spec.inherited_stack!=0;
        for (unsigned variant=0;variant<2;++variant) {
            probe::Setup setup; setup.seed=rig.seed++;
            std::mt19937_64 g(0xFADE00u+variant*31u+w.entry);
            probe::sweep(rig.harness,entry,setup,[&](Machine& m) {
                auto& memory=m.memory();
                if (held) {
                    write_word(m,m.cpu().sp.uint16_value,static_cast<std::uint16_t>((1+g()%30)<<8|(g()&255)));
                    write_word(m,static_cast<std::uint16_t>(m.cpu().sp.uint16_value+2),probe::Setup{}.return_address);
                }
                set_bc(m,static_cast<std::uint16_t>((1+g()%8)<<8|(g()&255)));
                memory.write(fade_timer,variant ? 0x81 : 0x01); memory.write(fade_divider,0);
                memory.write(fade_count,static_cast<std::uint8_t>(1+g()%32));
                for (unsigned k=0;k<32;++k) {
                    memory.write(static_cast<std::uint16_t>(palette+k),static_cast<std::uint8_t>(1+g()%63));
                    memory.write(static_cast<std::uint16_t>(target+k),static_cast<std::uint8_t>(g()&0x3F));
                }
                if (w.entry==0x7B5F) set_hl(m,palette+7);
                if (w.entry==0x7B92) { set_de(m,palette+3); set_hl(m,target+3); }
                if (w.entry==0x7BEE) {
                    memory.write(flash_counter,variant ? 2 : 3);
                    write_word(m,flash_run,static_cast<std::uint16_t>((4<<8)|3));
                }
                if (w.entry>=0x7C85) set_hl(m,static_cast<std::uint16_t>(0x8000+g()%0x3000));
            },variant ? 4 : 1);
        }
    }
}

// -- 2. Chains from a real frame ---------------------------------------------------------
struct ChainCount { std::uint64_t cpp=0, fallbacks=0; };
ChainCount chain(Rig& rig, const Machine& base, std::uint16_t entry, const Rig::Prepare& prepare, const std::string& what) {
    // A stop no code runs through: the byte after the last effect (01:7D17), data.
    const std::uint16_t stop=0x7D17;
    probe::Setup setup;
    setup.return_address=stop;
    setup.seed=rig.seed++;
    setup.r=rig.byte();
    setup.iff1=setup.iff2=true;
    auto machine=map_probe::prepare_at(base,at(entry),setup);
    if (prepare) prepare(machine);
    Machine native=machine;
    auto totals=[&] { std::uint64_t c=0, f=0; for (const auto& [e,r]:rig.harness.registry().entries()) { c+=r.counters.cpp_calls; f+=r.counters.fallback_calls; } return std::pair{c,f}; };
    const auto [c0,f0]=totals();
    // Every PSG write of each side, drained as it runs (review R3 M4).
    std::vector<PsgWrite> native_writes, engine_writes;
    auto run=[&](Machine& m, RoutineRegistry* registry, const char* side) {
        auto& writes=registry ? engine_writes : native_writes;
        auto drain=[&] { for (const auto& w:m.take_psg_writes()) writes.push_back(w); (void)m.take_samples(); };
        for (unsigned long steps=0;m.cpu().pc.uint16_value!=stop;++steps) {
            check(steps<40'000'000,std::string("a ")+side+" chain did not reach its stop: "+what+" (PC "+hex(m.cpu().pc.uint16_value)+")");
            if (steps%256==0) drain();
            try { if (registry) m.step(*registry); else m.step(); }
            catch (const std::exception& error) { throw std::runtime_error(std::string(error.what())+" ["+what+"]"); }
        }
        drain();
    };
    run(native,nullptr,"native");
    run(machine,&rig.harness.registry(),"routines-on");
    std::string fields;
    if (native_writes!=engine_writes) fields+=" PSG writes differ";
    // The documented equality (milestone 14; it also leaves out the sample
    // queues): the core's per-step scratch is not machine state.
    for (const auto& difference:oracle::differences(machine,native,oracle::Scope::whole)) fields+=" "+difference;
    for (const auto& difference:oracle::queue_differences(machine,native)) fields+=" "+difference;
    check(fields.empty(),"the chain differs from the native call ("+what+"):"+fields);
    const auto [c1,f1]=totals();
    return {c1-c0,f1-f0};
}
void chain_probes(Rig& rig, const Machine& base, const std::string& name) {
    ChainCount total;
    unsigned chains=0;
    auto add=[&](ChainCount c) { total.cpp+=c.cpp; total.fallbacks+=c.fallbacks; ++chains; };
    auto fill=[&](Machine& m, bool known) {
        for (unsigned k=0;k<32;++k) {
            m.memory().write(static_cast<std::uint16_t>(palette+k),static_cast<std::uint8_t>(known ? (k*5)&0x3F : rig.byte()&0x3F));
            m.memory().write(static_cast<std::uint16_t>(target+k),static_cast<std::uint8_t>(known ? (k*7+9)&0x3F : rig.byte()&0x3F));
        }
    };
    // Fade out and in, short and long, over a whole palette; and from random palettes.
    for (const std::uint16_t entry:{0x7AFD,0x7B05,0x7B18,0x7B20})
        for (unsigned i=0;i<3;++i) add(chain(rig,base,entry,[&](Machine& m) { fill(m,i==0); m.memory().write(fade_divider,static_cast<std::uint8_t>(i)); },name+" "+hex(entry)));
    // The flashes (the two ids of the magic flash).
    add(chain(rig,base,0x7BAC,[&](Machine& m) { fill(m,true); },name+" 7BAC"));
    for (const std::uint8_t id:{0x46,0x49,0x4A,0x00})
        add(chain(rig,base,0x7BC4,[&](Machine& m) { fill(m,true); m.memory().write(effect_id,id); },name+" 7BC4"));
    // The palette effects no recording reaches.
    for (const std::uint16_t entry:{0x7C85,0x7CA6,0x7CBB,0x7CE4})
        for (unsigned i=0;i<2;++i) add(chain(rig,base,entry,[&](Machine& m) { fill(m,i==0); },name+" "+hex(entry)));
    check(total.cpp>0,"the chains ran C++ segments");
    std::cout<<name<<": "<<chains<<" whole-call chains from a real frame equal to the native calls ("<<total.cpp<<" C++ calls, "
             <<total.fallbacks<<" fallbacks).\n";
}

void probes(const menu_probe::Inputs& inputs, bool& skipped) {
    Rig rig(inputs.rom);
    wait_probes(rig);
    flash_probes(rig);
    colour_probes(rig);
    step_probes(rig);
    effect_probes(rig);
    machine_probes(rig);
    exits_covered(rig);
    sweeps(rig);
    std::cout<<rig.count<<" power-on state probes ran in C++ ("<<rig.fallbacks<<" stack fallbacks); every declared exit was taken.\n";
    if (inputs.recordings.empty()) { std::cout<<"No recordings folder: the real-frame chains are skipped.\n"; skipped=true; return; }
    const std::string stem="later-town";
    const auto recording=replay::read(inputs.recording(stem));
    check(recording.rom_sha1==inputs.rom.sha1,stem+" was recorded with another ROM");
    const auto image=menu_probe::start_image(inputs,stem,recording);
    if (!image) { std::cout<<stem<<": start image missing (reference folder not configured), chains skipped.\n"; skipped=true; return; }
    menu_probe::Scenario s(inputs.rom,*image,stem);
    std::uint64_t from=0;
    check(s.play(recording,from,[](const Machine& m) { return map_probe::on_map(m); }),stem+" never settles on the map");
    s.frames({},8);
    chain_probes(rig,s.machine(),stem);
}
}

int main(int argc, char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        registration();
        if (argc==1) return 0;
        std::filesystem::path work;
        const auto inputs=menu_probe::load(argc,argv,&work);
        if (!inputs) return 77;
        bool skipped=false;
        probes(*inputs,skipped);
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
