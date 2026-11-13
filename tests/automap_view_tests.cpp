// SPDX-License-Identifier: GPL-3.0-or-later
// The automap view on the real tables, and the mutation test of the seen rule
// against the original view drawing (milestone 7, part M1). Arguments:
// <rom> <us|br> [tables|mutation-0|mutation-1] (exit 77 without the ROM).
// Without a part everything runs; the parts split the suite's time (the
// mutation halves run states 0-199 and 200-399 of the same random stream).
// Counts only; no table byte is written into the test.
#include "game/automap.h"
#include "probe.h"
#include <iostream>
#include <algorithm>
#include <map>
#include <set>

using namespace psmodern;
using probe::check;
using game::MapKind;
namespace dungeon=game::dungeon;
namespace {
using Block=std::array<std::uint8_t,dungeon::game_block_bytes>;
constexpr unsigned credits_floors_first=59;   // 3Bh and 3Ch: the credits walk's floors

// ---- The view offsets are the cone's geometry -------------------------------------

void offsets(const game::DungeonTables& t) {
    for (unsigned f=0;f<4;++f) {
        const int a=t.offset(f,1), r=t.offset(f,13), l=t.offset(f,12);
        check((a==-16 || a==16 || a==1 || a==-1) && l==-r && (l==16 || l==-16 || l==1 || l==-1) && l!=a && l!=-a,
              "ahead, left and right are unit steps at right angles");
        const int expected[14]{0,a,a+l,a+r,2*a,2*a+l,2*a+r,3*a,3*a+l,3*a+r,4*a,-a,l,r};
        for (unsigned k=0;k<14;++k) check(t.offset(f,k)==expected[k],"view offset "+std::to_string(k)+" follows the cone's geometry");
    }
    check(t.offset(0,1)==-16 && t.offset(1,1)==1 && t.offset(1,12)==-16,"facing 0 is up, facing 1 right with up on its left");
    std::cout<<"View offsets: index 1 ahead, 2/3 its sides, 4 two ahead, 5/6, 7, 8/9, 10 four ahead, 11-13 behind, left, right.\n";
}

// ---- Display and discovery counts on the real tables ------------------------------

void exit_cells(const game::DungeonTables& t) {
    unsigned cells=0, without=0, world=0, special=0, marked=0, plain=0;
    std::map<MapKind,unsigned> plain_kinds;
    for (unsigned f=0;f<dungeon::floor_count;++f) {
        const auto layout=t.layout(f);
        game::FloorExploration all; all.seen.set();
        const auto view=game::build_view(t,all,static_cast<std::uint8_t>(f),layout,0,0,Block{});
        for (unsigned c=0;c<256;++c) {
            const auto v=layout[c];
            if (!dungeon::exit_bit(v) || dungeon::object_cell(v)) { check(!view.cells[c].exit,"no exit marker without the exit bit"); continue; }
            ++cells;
            const auto e=t.exit_at(static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(c));
            const bool leads_out=!e || e->kind<dungeon::world_map_kinds_end;
            if (!e) ++without; else if (leads_out) ++world; else ++special;
            check(view.cells[c].exit==leads_out,"exit marker only where the lookup leads to the world map");
            const auto kind=view.cells[c].kind;
            check(kind>=MapKind::stairs_up && kind<=MapKind::magic_door,"exit-bit cells show as stairs or doors");
            if (view.cells[c].exit) ++marked; else { ++plain; ++plain_kinds[kind]; }
            game::FloorExploration none;
            check(game::build_view(t,none,static_cast<std::uint8_t>(f),layout,0,0,Block{}).cells[c]==game::MapCell{},
                  "an unseen exit cell shows nothing");
        }
    }
    check(cells==80 && without==16 && world==31 && special==33 && marked==47 && plain==33,
          "80 exit-bit cells: 31 below 80h and 16 without a record marked, 33 shown as plain stairs or doors");
    std::cout<<"Exit cells: "<<cells<<" ("<<world<<" below 80h and "<<without<<" without a record marked as exits; "
             <<special<<" plain:";
    for (const auto& [kind,n]:plain_kinds) std::cout<<" kind "<<static_cast<unsigned>(kind)<<" x"<<n;
    std::cout<<").\n";
}

void discoveries(const game::DungeonTables& t) {
    unsigned pits=0, chests=0, never=0, behind=0;
    std::set<unsigned> pit_floors;
    for (unsigned f=0;f<dungeon::floor_count;++f) {
        const auto floor=static_cast<std::uint8_t>(f);
        const auto layout=t.layout(f);
        for (unsigned c=0;c<256;++c) {
            if (!dungeon::object_cell(layout[c])) continue;
            const auto cell=static_cast<std::uint8_t>(c);
            const auto record=t.object_at(floor,cell);
            // Stood on, seen ahead from every side, and behind the party.
            game::ExplorationTracker on(t), ahead(t), back(t);
            on.observe({floor,cell,0,layout});
            for (unsigned facing=0;facing<4;++facing)
                ahead.observe({floor,static_cast<std::uint8_t>(c-t.offset(facing,1)),static_cast<std::uint8_t>(facing),layout});
            for (unsigned facing=0;facing<4;++facing)
                back.observe({floor,static_cast<std::uint8_t>(c-t.offset(facing,dungeon::behind)),static_cast<std::uint8_t>(facing),layout});
            Block block{};
            const bool pit=on.exploration().floors[f].found.test(c);
            const auto seen_kind=game::build_view(t,ahead.exploration().floors[f],floor,layout,0,0,block).cells[c].kind;
            const bool chest=seen_kind==MapKind::chest;
            if (pit) { ++pits; pit_floors.insert(f); check(!record,"a pit has no record"); }
            if (chest) { ++chests; check(record && record->type<=dungeon::money_chest,"a chest shown has a chest as first record"); }
            else check(seen_kind==MapKind::floor,"a seen value-8 cell that is not a chest shows as floor (pits only when stood on)");
            if (record && record->type>dungeon::money_chest) {
                ++never;
                check(!chest && !pit && !on.exploration().floors[f].found.test(c),"fixed fights and events are never marked");
            }
            check(!ahead.exploration().floors[f].found.test(c),
                  "a seen chest is not added to the found set");
            behind+=game::build_view(t,back.exploration().floors[f],floor,layout,0,0,block).cells[c].kind!=MapKind::unknown;
            if (chest) {
                block[record->flag-dungeon::game_block]=0xFF;
                check(game::build_view(t,ahead.exploration().floors[f],floor,layout,0,0,block).cells[c].kind==MapKind::opened_chest,
                      "a chest whose done flag is FFh shows opened");
            }
            const auto pit_kind=game::build_view(t,on.exploration().floors[f],floor,layout,0,0,Block{}).cells[c].kind;
            check(pit_kind==(pit ? MapKind::pitfall : record && record->type<=dungeon::money_chest ? MapKind::chest : MapKind::floor),
                  "stood on: a pit shows as a pitfall, a chest as a chest, anything else as floor");
        }
    }
    // 149 chest records, less the 10 shadowed second records of doubled cells.
    check(pits==33 && pit_floors.size()==22 && chests==139 && never==32 && behind==0,
          "33 pits on 22 floors, 139 chests shown once seen, 20 fights and 12 events never marked, nothing shown behind");
    std::cout<<"Discoveries: "<<pits<<" pits on "<<pit_floors.size()<<" floors, "<<chests<<" chests shown once seen, "
             <<never<<" fixed fights and events never marked, nothing shown behind the party."<<std::endl;
}

// ---- Mutation test of the seen rule against 01:6AED --------------------------------

constexpr RoutineEntry floor_load{1,0x6D56}, view_drawing{1,0x6AED};
constexpr std::uint16_t stack=0xDF00, sentinel=0x0180;

// Runs an original routine natively from its entry to its return.
void run_native(Machine& machine, RoutineEntry entry, std::uint8_t a) {
    auto& memory=machine.memory();
    memory.write(0xFFFE,static_cast<std::uint8_t>(entry.bank));
    auto& cpu=machine.cpu();
    cpu.pc.uint16_value=entry.address;
    cpu.sp.uint16_value=stack;
    cpu.af.uint8_values.at_1=a;
    memory.write(stack,static_cast<std::uint8_t>(sentinel)); memory.write(stack+1,static_cast<std::uint8_t>(sentinel>>8));
    for (unsigned steps=0;;++steps) {
        machine.step();
        if (steps%256==0) { (void)machine.take_samples(); (void)machine.take_psg_writes(); }
        if (cpu.pc.uint16_value==sentinel && cpu.sp.uint16_value==stack+2) return;
        check(steps<4'000'000,"an original routine did not return");
    }
}
constexpr std::uint16_t view_buffer=0xD000, view_buffer_end=0xD600;   // 32 x 24 name-table entries
bool picture_differs(const Machine& a, const Machine& b) {
    if (a.vdp().vram()!=b.vdp().vram() || a.vdp().cram()!=b.vdp().cram()) return true;
    for (std::uint16_t at=view_buffer;at<view_buffer_end;++at) if (a.peek(at)!=b.peek(at)) return true;
    return false;
}

void mutation(const probe::Rom& rom, const game::DungeonTables& t, unsigned first, unsigned last) {
    Machine power_on(rom.image);
    (void)power_on.read_port(0xbf,power_on.cycles());
    power_on.advance(1);
    // A machine per normal floor at the dungeon wait: mode $0B, the floor
    // loaded by the game's own loader (01:6D56 fills $CB00 and the floor's light).
    std::vector<Machine> floors;
    for (unsigned f=0;f<credits_floors_first;++f) {
        Machine m=power_on;
        m.memory().write(dungeon::game_mode,dungeon::dungeon_mode);
        m.memory().write(dungeon::floor,static_cast<std::uint8_t>(f));
        run_native(m,floor_load,0);
        const auto layout=t.layout(f);
        for (unsigned c=0;c<256;++c)
            check(m.peek(static_cast<std::uint16_t>(dungeon::layout+c))==layout[c],"the game's floor loader gives the decoded layout");
        floors.push_back(m);
    }
    std::mt19937_64 random(rom.version==RomVersion::us_rev_a ? 0x6AED : 0x6AEE);
    constexpr unsigned states=400;
    std::map<unsigned,unsigned> depths;   // last front index reached: 1, 4, 7 or 10
    unsigned mutations=0, sensitive=0, side_checks=0, side_sensitive=0;
    for (unsigned s=0;s<states;++s) {
        const unsigned f=s%credits_floors_first;
        const auto layout=t.layout(f);
        // A walkable cell to stand on (value 0 or 8) and a facing.
        std::uint8_t cell;
        do cell=static_cast<std::uint8_t>(random()); while (!dungeon::walkable(layout[cell]));
        const auto facing=static_cast<std::uint8_t>(random()%4);
        const auto facing_byte=static_cast<std::uint8_t>(facing|(random()&0xFC));
        const auto seen=game::view_cells(t,layout,cell,facing);
        // The random pattern is drawn for every state, so that a part sees the
        // same states as a whole run.
        std::array<std::uint8_t,256> noise{};
        for (unsigned c=0;c<256;++c)
            if (!seen.test(c)) noise[c]=static_cast<std::uint8_t>((random()%16)|(random()%2 ? 0x80 : 0));
        if (s<first || s>=last) continue;
        Machine start=floors[f];
        start.memory().write(dungeon::position,cell);
        start.memory().write(dungeon::facing,facing_byte);
        auto front=[&](unsigned k) { return static_cast<std::uint8_t>(cell+t.offset(facing,k)); };
        unsigned depth=1;
        for (const unsigned k:{4u,7u,10u}) if (seen.test(front(k))) depth=k;
        ++depths[depth];

        Machine base=start;
        run_native(base,view_drawing,0);
        // Every unseen cell changed at once, in four patterns: the drawing must
        // leave the whole machine (video and colour memory, the view buffer,
        // registers and cycles included) as before, apart from the layout.
        for (unsigned pattern=0;pattern<4;++pattern) {
            Machine changed=start;
            for (unsigned c=0;c<256;++c) {
                if (seen.test(c)) continue;
                std::uint8_t v=0;
                switch (pattern) {
                case 0: v=0; break;
                case 1: v=1; break;
                case 2: v=0x82; break;
                default: v=noise[c]; break;
                }
                changed.memory().write(static_cast<std::uint16_t>(dungeon::layout+c),v);
            }
            run_native(changed,view_drawing,0);
            for (unsigned c=0;c<256;++c) changed.memory().write(static_cast<std::uint16_t>(dungeon::layout+c),layout[c]);
            std::string fields;
            for (const auto& d:base.differences(changed)) fields+=" "+d.field;
            check(fields.empty(),"floor "+std::to_string(f)+", cell "+std::to_string(cell)+", facing "+std::to_string(facing)+
                  ": changing unseen cells (pattern "+std::to_string(pattern)+") changed the drawing:"+fields);
            ++mutations;
        }
        // Sensitivity: flipping the walkability of a seen cell changes the
        // picture: the front cells (1, and 4, 7, 10 when reached) in every
        // state, the seen side cells in every second state (time).
        auto flipped=[&](std::uint8_t c) {
            Machine changed=start;
            changed.memory().write(static_cast<std::uint16_t>(dungeon::layout+c),dungeon::walkable(layout[c]) ? 1 : 0);
            run_native(changed,view_drawing,0);
            return picture_differs(base,changed);
        };
        for (const unsigned k:{1u,4u,7u,10u}) {
            if (k>depth) break;
            check(flipped(front(k)),"flipping the walkability of seen front cell "+std::to_string(k)+" changes the picture (floor "+
                  std::to_string(f)+", cell "+std::to_string(cell)+", facing "+std::to_string(facing)+")");
            ++sensitive;
        }
        for (const unsigned k:{2u,3u,5u,6u,8u,9u}) {
            if (s%2 || !seen.test(front(k)) || k>depth) continue;
            ++side_checks;
            side_sensitive+=flipped(front(k));
        }
    }
    const unsigned least=(last-first)*20/states;
    check(depths[1]>=least && depths[4]>=least && depths[7]>=least && depths[10]>=least,"every cut depth of the cone is exercised");
    check(side_sensitive==side_checks,"flipping a seen side cell changes the picture");
    std::cout<<"Mutation of 01:6AED: states "<<first<<"-"<<last-1<<" of "<<states<<" on floors 0-58 (cut at 1/4/7/10: "<<depths[1]<<"/"<<depths[4]<<"/"
             <<depths[7]<<"/"<<depths[10]<<"), "<<mutations<<" unseen-cell mutations left the machine identical; "
             <<sensitive<<" front and "<<side_sensitive<<"/"<<side_checks<<" side flips changed the picture.\n";
}

// ---- Floors form stacks; the floor label counts inside a stack --------------------

// Stairs without the exit bit join floor f to f+1, always in the same cell, so
// the direction is the same from both sides and the floor number is the height.
// The floors joined that way are a "stack". Outside exits (leading to the world
// map) lie on several floors of some stacks, so the label (DungeonTables::
// floor_level) makes the lowest floor with an outside exit FLOOR 1; a stack
// with none starts at its lowest floor (docs/how-it-works/dungeons.md).
void floor_stacks(const game::DungeonTables& t) {
    std::vector<std::vector<unsigned>> ups(dungeon::floor_count), downs(dungeon::floor_count);
    std::vector<unsigned> exit_cells(dungeon::floor_count,0), without_record(dungeon::floor_count,0);
    for (unsigned f=0;f<dungeon::floor_count;++f) {
        const auto layout=t.layout(f);
        for (unsigned c=0;c<256;++c) {
            const auto v=layout[c];
            if (dungeon::object_cell(v)) continue;
            const auto kind=v&7;
            if ((kind==2 || kind==3) && !dungeon::exit_bit(v)) (kind==2 ? ups : downs)[f].push_back(c);
            if (!dungeon::exit_bit(v) || kind<2 || kind>6) continue;
            const auto e=t.exit_at(static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(c));
            if (e && e->kind>=dungeon::world_map_kinds_end) continue;   // a scene, an interaction or an event
            ++exit_cells[f];
            if (!e) ++without_record[f];
        }
    }
    check(downs[0].empty() && ups[dungeon::floor_count-1].empty(),"no stairs lead below floor 0 or above floor 60");
    for (unsigned f=0;f+1<dungeon::floor_count;++f)
        check(ups[f]==downs[f+1],"every stairs cell has the opposite stairs in the same cell of the next floor");
    unsigned stacks=0, several=0, none=0, none_floors=0, no_record_floors_most=0, no_record_stacks=0;
    for (unsigned first=0;first<dungeon::floor_count;) {
        unsigned last=first;
        while (last+1<dungeon::floor_count && !ups[last].empty()) ++last;
        unsigned exit_floors=0, record_free_floors=0;
        for (unsigned f=first;f<=last;++f) { exit_floors+=exit_cells[f]!=0; record_free_floors+=without_record[f]!=0; }
        ++stacks; several+=exit_floors>1; if (!exit_floors) { ++none; none_floors+=last-first+1; }
        no_record_floors_most=std::max(no_record_floors_most,record_free_floors); no_record_stacks+=record_free_floors!=0;
        first=last+1;
    }
    check(stacks==21 && several==3 && none==3 && none_floors==4 && no_record_floors_most==1 && no_record_stacks==12,
          "21 stacks: 3 with outside exits on several floors, 3 with none; 12 with a no-record exit, never on two floors");
    // The labels: consecutive inside a stack, never 0, FLOOR 1 on the lowest floor with an outside exit.
    std::string table;
    for (unsigned first=0;first<dungeon::floor_count;) {
        unsigned last=first, base=first;
        while (last+1<dungeon::floor_count && !ups[last].empty()) ++last;
        for (unsigned f=last+1;f-->first;) if (exit_cells[f]) base=f;
        for (unsigned f=first;f<=last;++f) {
            const int level=t.floor_level(f);
            check(level!=0 && level==(f>=base ? static_cast<int>(f-base)+1 : static_cast<int>(f)-static_cast<int>(base)),
                  "floor label counts from the lowest floor with an outside exit");
            if (f>first) check(level==t.floor_level(f-1)+1 || (level==1 && t.floor_level(f-1)==-1),"labels are consecutive inside a stack");
        }
        table+=std::to_string(first)+"-"+std::to_string(last)+":"+std::to_string(t.floor_level(first))+".."+std::to_string(t.floor_level(last))+" ";
        first=last+1;
    }
    check(t.floor_level(3)==-1 && t.floor_level(4)==1 && t.floor_level(15)==12 && t.floor_level(16)==-3 && t.floor_level(19)==1 &&
          t.floor_level(47)==1 && t.floor_level(52)==6 && t.floor_level(31)==1 && t.floor_level(59)==1 && t.floor_level(60)==2 &&
          t.floor_level(61)==0,"labels of chosen floors");
    std::cout<<"Floor labels (stack first-last: first..last): "<<table<<std::endl;
    std::cout<<"Floor stacks: "<<stacks<<" joined by stairs, "<<several<<" with outside exits on several floors, "<<none<<" ("<<none_floors
             <<" floors) with no outside exit, "<<no_record_stacks<<" with a record-less exit on one floor each.\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc!=3 && argc!=4) throw std::runtime_error("Expected <rom> <us|br> [tables|mutation-0|mutation-1].");
        const std::string part=argc==4 ? argv[3] : "";
        if (!part.empty() && part!="tables" && part!="mutation-0" && part!="mutation-1") throw std::runtime_error("Unknown part: "+part);
        const auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        const game::DungeonTables tables(*rom->image);
        if (part.empty() || part=="tables") {
            offsets(tables);
            exit_cells(tables);
            discoveries(tables);
            floor_stacks(tables);
        }
        if (part.empty()) mutation(*rom,tables,0,400);
        else if (part=="mutation-0") mutation(*rom,tables,0,200);
        else if (part=="mutation-1") mutation(*rom,tables,200,400);
        std::cout<<"Automap view checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
