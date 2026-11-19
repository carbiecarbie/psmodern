// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 12, part places: the registration catalog and the probes.
//   no arguments   the part's registration against its plan (every entry
//                  planned with its unit, or inside the part's ranges with a
//                  unit of the part; never a native instruction) and each
//                  spec's exits and permissions; no ROM
//   <rom> <us|br> <recordings folder> [<reference folder> [<work folder>]]
//                  the part's state and scenario probes on that ROM (exit 77
//                  without the ROM; a missing reference folder skips what needs
//                  it with a message and returns 77 at the end)
//
// The state probes start a machine at one entry of the hospital, the church or
// the shops with the state the callers leave (registers, the party records,
// the money, the item, the counters, the words the segment pops) and dispatch
// it with the per-call verifier (the original runs first, the whole machines
// are compared). Every declared exit of every entry must be taken at least
// once (the last instruction the original ran is the exit), and the cost the
// call really had must equal its bound. The paths no recording reaches (the
// second shop kind, the Road Pass branch, a vehicle already owned, a lone
// party, a member at the top level, an empty shop line) are reached here by
// the state, on both ROMs. A scenario probe plays the places recording with
// edited state (the party's money, an item) and compares routines on and off
// every frame.
#include "game/menu_kit.h"
#include "menu_probe.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <set>

using namespace psmodern;
using probe::check;
namespace menu=game::menu;

namespace {
using game::menu_kit::core_final_at;
using game::menu_kit::returned_at;

// ---- Work RAM of the part (ROM static) ------------------------------------------------------
constexpr std::uint16_t ram_party=0xC4F0, ram_character=0xC2C2, ram_item=0xC2C4, ram_number=0xC2C5, ram_money=0xC4E0,
    ram_inventory=0xC4E2, ram_vehicles=0xC2EC, ram_scene=0xC2DB, ram_scene_copy=0xC317, ram_sound=0xC004, ram_records=0xC400,
    ram_item_record=0xC600;
constexpr std::uint16_t sentinel=0x0180;

// ---- Registration catalog (segments.md section 4P) ---------------------------------------------
struct Expected { std::uint16_t entry, unit; std::vector<SegmentExit> exits; unsigned scratch, inherited; bool mapper; };
std::vector<Expected> expected_catalog() {
    const auto F=[](std::uint16_t a, std::int8_t s=0) { return core_final_at(a,s); };
    const auto R=[](std::uint16_t a, std::int8_t s=0) { return returned_at(a,s); };
    return {
        {0x298D,0x298D,{F(0x2990)},0,0,false}, {0x2996,0x298D,{F(0x29A0),F(0x2A58)},0,0,false},
        {0x29A6,0x298D,{F(0x29AE),F(0x2A52)},0,0,false}, {0x29B1,0x298D,{F(0x29B6),F(0x29D2),F(0x29FB)},2,0,false},
        {0x29D5,0x298D,{F(0x29DE),F(0x2A48)},0,0,false}, {0x2A04,0x298D,{F(0x2A05,-2)},2,0,false},
        {0x2A08,0x298D,{F(0x2A1A,2),F(0x2A48,2),F(0x2A5E,2)},0,2,false}, {0x2A1D,0x298D,{F(0x2A31)},0,0,false},
        {0x2A37,0x298D,{F(0x2A40),F(0x2A6A)},0,0,false}, {0x2A46,0x298D,{F(0x2A48),F(0x2A52)},0,0,false},
        {0x2A4B,0x298D,{F(0x29A0),F(0x2A52)},0,0,false}, {0x2A55,0x298D,{F(0x2A58)},0,0,false},
        {0x2A64,0x298D,{F(0x2A67)},0,0,false},
        {0x2A6D,0x2A6D,{F(0x2A72)},0,0,false}, {0x2A75,0x2A6D,{F(0x2A72),R(0x2A84)},0,0,false},
        {0x2A85,0x2A85,{F(0x2A8E)},0,0,false}, {0x2A94,0x2A85,{F(0x2AA2),F(0x2B37),F(0x2B49)},0,0,false},
        {0x2A98,0x2A85,{F(0x2AA2),F(0x2B37)},0,0,false}, {0x2AA8,0x2A85,{F(0x2AAD),F(0x2B43)},0,0,false},
        {0x2AB0,0x2A85,{F(0x2ACB),F(0x2B37)},2,0,false}, {0x2AD4,0x2A85,{F(0x2AD5,-2)},2,0,false},
        {0x2AD8,0x2A85,{F(0x2ADE,2),F(0x2B18,2)},0,2,false}, {0x2AE1,0x2A85,{F(0x2AF1),F(0x2B29)},0,0,false},
        {0x2AF4,0x2A85,{F(0x2B07)},0,0,false}, {0x2B0A,0x2A85,{F(0x2B0F)},0,0,false},
        {0x2B15,0x2A85,{F(0x2B18)},0,0,false}, {0x2B1E,0x2A85,{F(0x2B20),F(0x2B43)},0,0,false},
        {0x2B2F,0x2A85,{F(0x2B18)},0,0,false}, {0x2B3A,0x2A85,{F(0x2B18),F(0x2B40)},0,0,false},
        {0x2B46,0x2A85,{F(0x2B49)},0,0,false}, {0x2B4C,0x2A85,{F(0x2B4F)},0,0,false},
        {0x2B58,0x2B58,{F(0x2B60)},0,0,false}, {0x2B63,0x2B58,{F(0x2B6C)},0,0,false},
        {0x2B6F,0x2B58,{F(0x2B78)},0,0,false}, {0x2B7B,0x2B58,{R(0x2B8B),F(0x2B96),F(0x2BBD)},2,0,true},
        {0x2B84,0x2B84,{R(0x2B8B),F(0x2B96),F(0x2BBD)},2,0,true},
        {0x2BC0,0x2BC0,{F(0x2BC3)},0,0,false}, {0x2BC9,0x2BC0,{F(0x2BCE),F(0x2BD5,-2)},2,0,false},
        {0x2BDB,0x2BC0,{F(0x2BDF,2)},0,2,false}, {0x2BE2,0x2BC0,{F(0x2BE3,-2)},2,0,false},
        {0x2BE6,0x2BC0,{F(0x2C0C,2),F(0x2C30,2),F(0x2C5B,2),F(0x2C71)},0,2,false},
        {0x2C0F,0x2BC0,{F(0x2C1A),F(0x2C1F)},0,0,false}, {0x2C1D,0x2BC0,{F(0x2C1F),F(0x2C25)},0,0,false},
        {0x2C22,0x2BC0,{F(0x2C25)},0,0,false}, {0x2C2B,0x2BC0,{F(0x2BDF),F(0x2C30)},0,0,false},
        {0x2C74,0x2BC0,{F(0x2C5B,2),F(0x2C71),F(0x2C7A,2)},0,2,false}, {0x2C80,0x2BC0,{F(0x2C83)},0,0,false},
        {0x2C8F,0x2C8F,{F(0x2C92)},0,0,false},
        {0x2C98,0x2C98,{F(0x2C9B)},0,0,false}, {0x2CA1,0x2C98,{F(0x2CA3,-4)},4,0,false},
        {0x2CA6,0x2C98,{F(0x2BD5,2),F(0x2CB4,4),F(0x2CED,4)},0,4,false},
        {0x2CB1,0x2CB1,{F(0x2CB4)},0,0,false}, {0x2CBA,0x2CB1,{F(0x2CBD,-2)},2,0,false},
        {0x2CC0,0x2CB1,{F(0x2CE4,2),F(0x2CED,2),F(0x2CF6,2)},0,2,true},
        {0x2CFC,0x2CB1,{F(0x2CB4),F(0x2D01)},0,0,false}, {0x2D04,0x2CB1,{F(0x2D07)},0,0,false},
        {0x2D0A,0x2CB1,{F(0x2D0D)},0,0,false},
        {0x2D13,0x2CB1,{F(0x2CB4),F(0x2CED)},0,0,false},
    };
}
void registration() {
    const auto entries=menu_probe::registered_entries(menu::Part::places,game::register_menu_places);
    std::cout<<entries.size()<<" of "<<menu::places_segments.size()<<" planned places segments registered.\n";
    check(entries.size()==menu::places_segments.size() && entries.size()==58,"all 58 planned places segments are registered, and nothing else");
    RoutineRegistry registry;
    game::register_menu_places(registry);
    const auto catalog=expected_catalog();
    check(catalog.size()==58,"the expected catalog lists 58 entries");
    for (const auto& expected:catalog) {
        const RoutineEntry entry{0,expected.entry};
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"a catalog entry is not registered");
        const auto& spec=routine->spec;
        check(spec.unit==RoutineEntry{0,expected.unit},"unit of a registered entry");
        check(spec.exits.size()==expected.exits.size(),"exit count of a registered entry");
        for (std::size_t i=0;i<expected.exits.size();++i)
            check(spec.exits[i].same_declaration(expected.exits[i]),"an exit of a registered entry is declared as expected");
        check(spec.scratch_stack==expected.scratch && spec.inherited_stack==expected.inherited && spec.slot2_mapper==expected.mapper,
              "the permissions of a registered entry");
        check(!spec.single_instruction,"no places segment is single-instruction or forced");
        bool planned=false;
        for (const auto& segment:menu::places_segments) planned=planned || (segment.entry==entry && segment.unit==spec.unit);
        check(planned,"a catalog entry is planned with its unit");
    }
}

// ---- The lab ----------------------------------------------------------------------------------
struct Stats {
    std::uint64_t calls=0, fallbacks=0;
    std::map<std::uint16_t,std::uint64_t> per_entry;
    std::map<std::uint16_t,std::set<std::uint16_t>> exits;      // last instruction the original ran, per entry
};
using menu_probe::admitted;
class Lab {
public:
    Lab(probe::Rom rom, Stats& stats, std::uint64_t seed) : harness(std::move(rom)), stats_(stats), rng_(seed), seed_(seed) {}
    probe::Harness harness;

    static void poke(Machine& m, std::uint16_t address, std::uint8_t value) { if (address<0xfffc) m.memory().write(address,value); }
    static void poke_word(Machine& m, std::uint16_t address, std::uint16_t value) {
        poke(m,address,static_cast<std::uint8_t>(value)); poke(m,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
    }
    std::uint8_t byte() { return static_cast<std::uint8_t>(rng_()); }
    std::uint16_t word() { return static_cast<std::uint16_t>(rng_()); }
    unsigned below(unsigned n) { return static_cast<unsigned>(rng_()%n); }
    template<class T> T pick(std::initializer_list<T> values) { return *(values.begin()+below(static_cast<unsigned>(values.size()))); }

    probe::Setup next_setup() {
        const auto n=++serial_;
        probe::Setup s;
        s.seed=n*7919+seed_;
        s.iff1=(n&1); s.iff2=(n&2);
        switch (n%8) {
            case 4: s.banks[2]=0; break;
            case 5: { constexpr std::uint8_t other[3]{2,15,31}; s.banks[2]=other[(n/8)%3]; break; }
            case 6: s.banks[2]=3; s.cartridge_ram=true; break;
            default: s.banks[2]=3;
        }
        static const auto wraps=probe::r_wrap_values(900);
        s.r=(n%3==0) ? wraps[(n/3)%wraps.size()] : byte();
        s.sp=static_cast<std::uint16_t>(0xCC10+rng_()%0x1300);
        if (n%11==0) {
            static const auto cases=probe::stack_cases();
            const auto& c=cases[(n/11)%cases.size()];
            s.sp=c.sp; s.cartridge_ram=c.cartridge_ram; s.work_ram_disabled=c.work_ram_disabled;
            if (s.cartridge_ram) s.banks[2]=3;
        }
        return s;
    }

    // The state the callers leave, varied so that every branch of every segment is met.
    void state(Machine& m, std::uint16_t entry) {
        auto& cpu=m.cpu();
        poke(m,ram_party,pick<unsigned>({0,1,2,3,4,byte()}));
        for (unsigned c=0;c<4;++c) {
            const auto base=static_cast<std::uint16_t>(ram_records+16*c);
            for (unsigned i=0;i<16;++i) poke(m,static_cast<std::uint16_t>(base+i),byte());
            if (below(3)==0) poke(m,static_cast<std::uint16_t>(base+1),m.peek(static_cast<std::uint16_t>(base+6)));
            if (below(3)==0) poke(m,static_cast<std::uint16_t>(base+2),m.peek(static_cast<std::uint16_t>(base+7)));
            if (below(2)==0) {                                       // equal pairs: nothing to heal
                poke(m,static_cast<std::uint16_t>(base+1),m.peek(static_cast<std::uint16_t>(base+6)));
                poke(m,static_cast<std::uint16_t>(base+2),m.peek(static_cast<std::uint16_t>(base+7)));
            }
            poke(m,static_cast<std::uint16_t>(base+5),pick<unsigned>({0,1,2,29,30,31,255,byte()}));
        }
        const auto money=pick<unsigned>({0,1,55,0x7FFF,0x8000,word()});
        poke_word(m,ram_money,static_cast<std::uint16_t>(money));
        poke_word(m,ram_number,pick<unsigned>({money,money+1,money-1,0,word()}));
        poke(m,ram_character,byte());
        poke(m,ram_item,pick<unsigned>({0x00,0x20,0x21,0x22,0x23,0x24,0x3F,0x40,0x41,0x7F,0xFF,byte()}));
        poke(m,ram_inventory,pick<unsigned>({0,1,22,23,24,25,255,byte()}));
        poke(m,ram_vehicles,pick<unsigned>({0,1,2,3,byte()}));
        poke(m,ram_scene,byte()); poke(m,ram_scene_copy,byte()); poke(m,ram_sound,byte());
        // The shop's item record under HL: item, price (low, high).
        poke(m,ram_item_record,pick<unsigned>({0x00,0x21,0x23,0x24,0x3F,0x40,0x41,0x7F,0xFF,byte()}));
        poke_word(m,ram_item_record+1,pick<unsigned>({money,money+1,money-1,0,word()}));
        // Registers: a character record under HL or IY where the segment writes through it.
        const auto record=static_cast<std::uint16_t>(ram_records+16*below(4));
        cpu.hl.uint16_value=word();
        cpu.ix_iy[1].uint16_value=word();
        switch (entry) {
            case 0x29B1: case 0x2AB0: case 0x2A75: cpu.hl.uint16_value=record; break;
            case 0x2A1D: case 0x2AF4: cpu.ix_iy[1].uint16_value=record; break;
            case 0x2BE6: cpu.hl.uint16_value=ram_item_record; break;
            case 0x2C74: break;
            default: break;
        }
        // C: bit 4 (the first button) set or clear; B small for the restore loop; F from the seed.
        cpu.bc.uint8_values.at_0=static_cast<std::uint8_t>(below(2) ? (cpu.bc.uint8_values.at_0|0x10) : (cpu.bc.uint8_values.at_0&~0x10));
        if (entry==0x2A75 && below(3)==0) cpu.bc.uint8_values.at_1=1;
        if (entry==0x2A94 && below(2)==0) cpu.af.uint8_values.at_1=0;           // the answer in A: yes (zero) or no
        // The words above the return address (what an inherited segment pops).
        auto sp=cpu.sp.uint16_value;
        poke_word(m,sp,sentinel);
        for (unsigned i=1;i<=4;++i) poke_word(m,static_cast<std::uint16_t>(sp+2*i),word());
    }

    // A dispatch the machine refuses (an unmapped or mapper stack, or a frame boundary inside the bound),
    // predicted from its own rules.
    bool refused(Machine& m, RoutineEntry entry) {
        const auto& routine=harness.routine(entry);
        if (!admitted(m,routine.spec)) { harness.run_fallback(m,entry,RoutineFallback::unmapped_stack); ++stats_.fallbacks; return true; }
        const auto bound=routine.bound(m);
        if (Machine::frame_cycles-m.cycles()%Machine::frame_cycles<bound) {
            harness.run_fallback(m,entry,RoutineFallback::frame); ++stats_.fallbacks; return true;
        }
        return false;
    }
    void run(std::uint16_t address) {
        const RoutineEntry entry{0,address};
        const auto setup=next_setup();
        auto m=harness.prepare(entry,setup);
        state(m,address);
        if (refused(m,entry)) return;
        Machine original=m;
        const auto start=m.cycles();
        const bool ram_on=(m.memory().memory_control()&0x10)==0;
        const auto bound=harness.routine(entry).bound(m);
        const auto result=harness.run_cpp(m,entry);
        if (ram_on) check(result.cycles==bound,"entry "+std::to_string(address)+" cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        std::uint16_t last=0;
        unsigned guard=0;
        while (original.cycles()-start<result.cycles && ++guard<20000) { last=original.cpu().pc.uint16_value; original.step(); }
        check(original.cycles()-start==result.cycles,"the original does not end on the C++ call's last cycle");
        ++stats_.calls; ++stats_.per_entry[address]; stats_.exits[address].insert(last);
    }
private:
    Stats& stats_;
    std::mt19937_64 rng_;
    std::uint64_t seed_, serial_=0;
};

// Every declared exit of every entry must have been the last instruction of some probe.
void check_exits_taken(const Stats& stats, const std::string& version) {
    unsigned missing=0;
    for (const auto& expected:expected_catalog()) {
        const auto found=stats.exits.find(expected.entry);
        for (const auto& exit:expected.exits) {
            const bool seen=found!=stats.exits.end() && found->second.count(exit.address);
            if (!seen) { ++missing; std::cerr<<version<<": entry "<<std::hex<<expected.entry<<" never took the exit "<<exit.address<<std::dec<<"\n"; }
        }
        if (found!=stats.exits.end())
            for (const auto last:found->second) {
                bool declared=false;
                for (const auto& exit:expected.exits) declared=declared || exit.address==last;
                check(declared,"an entry ended on an instruction that is not a declared exit");
            }
    }
    check(missing==0,std::to_string(missing)+" declared exits were never taken");
}

// ---- Probes ---------------------------------------------------------------------------------------
void state_probes(const menu_probe::Inputs& inputs) {
    Stats stats;
    Lab lab(inputs.rom,stats,inputs.version=="us" ? 12001 : 12002);
    for (const auto& segment:menu::places_segments)
        for (unsigned k=0;k<600;++k) lab.run(segment.entry.address);
    check_exits_taken(stats,inputs.version);
    // Interrupt sweeps (milestone 14): an IRQ and an NMI at every cycle of every segment (every n-th of the
    // longest, about 300 runs per kind), each run compared with the oracle at every instruction boundary.
    for (const auto& segment:menu::places_segments)
        for (unsigned variant=0;variant<2;++variant) {
            probe::Setup setup; setup.seed=900+variant;
            auto pilot=lab.harness.prepare(segment.entry,setup);
            lab.state(pilot,segment.entry.address);
            const auto length=lab.harness.routine(segment.entry).bound(pilot);
            const unsigned stride=static_cast<unsigned>(std::max<std::uint64_t>(1,length/300));
            probe::sweep(lab.harness,segment.entry,setup,[&](Machine& m) { lab.state(m,segment.entry.address); },stride);
        }
    std::cout<<"places state probes on "<<inputs.version<<": "<<stats.calls<<" calls in C++, "<<stats.fallbacks
             <<" predicted fallbacks, every declared exit of the "<<menu::places_segments.size()<<" registered entries taken.\n";
    check(stats.calls>menu::places_segments.size()*300,"most state probes ran in C++");
}

// The places recording with edited state: the party's money and the shop counters are poked at frames
// where a place has been entered, and routines on and off are compared every frame.
bool scenario_probes(const menu_probe::Inputs& inputs) {
    if (inputs.version!="us") { std::cout<<"places scenarios: the recording is a US one; the BR ROM is covered by the state probes."<<std::endl; return false; }
    const auto recording=replay::read(inputs.recording("places"));
    const auto image=menu_probe::start_image(inputs,"places",recording);
    if (!image) { std::cout<<"places scenarios: skipped (no start image; give the reference folder).\n"; return true; }
    unsigned compared=0;
    for (const auto money:{0u,20000u}) {
        menu_probe::Scenario s(inputs.rom,*image,"places-money-"+std::to_string(money));
        std::uint64_t from=0;
        // Into the first dialogue of the recording, then the money is edited for the whole place.
        check(s.play(recording,from,[](const Machine& m) { return menu_probe::on_dialogue(m); }),"the places recording never opens a dialogue");
        s.poke_word(menu::money,static_cast<std::uint16_t>(money));
        for (unsigned frames=0;frames<2000 && from<recording.frames;++frames) { s.frame(recording.input_at(from++)); ++compared; }
    }
    std::cout<<"places scenarios: "<<compared<<" frames compared, routines on against off, with two edited amounts of money (none, and enough for the dearest shop line).\n";
    return false;
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
        state_probes(*inputs);
        return scenario_probes(*inputs) ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
