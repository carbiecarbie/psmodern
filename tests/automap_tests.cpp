// SPDX-License-Identifier: GPL-3.0-or-later
// The automap model without a ROM (milestone 7, part M1): seen, found and
// display rules on synthetic layouts and tables, and the unknown-cell property.
// The tables are built in an authored image: geometric view offsets and object
// and exit records named by each check; no ROM byte is used.
#include "game/automap.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psmodern;
using game::MapKind;
namespace dungeon=game::dungeon;
namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }

// ---- Synthetic tables ------------------------------------------------------------

struct Object { std::uint8_t floor, cell; std::uint16_t flag; std::uint8_t type; };
struct Exit { std::uint8_t floor, cell, kind; };

// Ahead, left and right for each facing (0 up, 1 right, 2 down, 3 left).
constexpr int ahead_of[4]{-16,1,16,-1}, left_of[4]{-1,-16,1,16}, right_of[4]{1,16,-1,-16};
// The geometric view offsets: 1 ahead, 2/3 its left and right, 4 two ahead,
// 5/6 its sides, 7 three ahead, 8/9 its sides, 10 four ahead, 11 behind, 12
// left, 13 right.
int geometric_offset(unsigned facing, unsigned index) {
    const int a=ahead_of[facing], l=left_of[facing], r=right_of[facing];
    const int offsets[16]{0,a,a+l,a+r,2*a,2*a+l,2*a+r,3*a,3*a+l,3*a+r,4*a,-a,l,r,0,0};
    return offsets[index];
}
game::DungeonTables tables(const std::vector<Object>& objects, const std::vector<Exit>& exits) {
    std::vector<std::uint8_t> image(512*1024,0xFF);
    const auto offsets=dungeon::image_offset(dungeon::view_offsets_table);
    for (unsigned f=0;f<4;++f)
        for (unsigned k=0;k<16;++k) image[offsets+16*f+k]=static_cast<std::uint8_t>(geometric_offset(f,k));
    auto at=dungeon::image_offset(dungeon::objects_table);
    for (const auto& o:objects) {
        const std::uint8_t record[7]{o.floor,o.cell,static_cast<std::uint8_t>(o.flag),static_cast<std::uint8_t>(o.flag>>8),o.type,0,0};
        for (const auto b:record) image[at++]=b;
    }
    at=dungeon::image_offset(dungeon::exits_table);
    for (const auto& e:exits) {
        const std::uint8_t record[5]{e.floor,e.cell,e.kind,0,0};
        for (const auto b:record) image[at++]=b;
    }
    return game::DungeonTables(image);
}
std::uint8_t cell_at(unsigned cell, unsigned facing, unsigned index) {
    return static_cast<std::uint8_t>(static_cast<int>(cell)+geometric_offset(facing,index));
}
game::DungeonLayout walls() { game::DungeonLayout layout; layout.fill(1); return layout; }
game::CellSet cells(std::initializer_list<unsigned> list) { game::CellSet set; for (const auto c:list) set.set(c); return set; }
game::CellSet cone(unsigned cell, unsigned facing, unsigned last) {
    game::CellSet set; set.set(cell);
    for (unsigned k=1;k<=last;++k) set.set(cell_at(cell,facing,k));
    return set;
}
using Block=std::array<std::uint8_t,dungeon::game_block_bytes>;

// ---- Seen rule ------------------------------------------------------------------

void seen_rule() {
    const auto t=tables({},{});
    unsigned checks=0;
    for (unsigned facing=0;facing<4;++facing) {
        check(t.offset(facing,dungeon::ahead)==ahead_of[facing],"the authored offsets are the geometric ones");
        for (const unsigned party:{0x88u,0x00u,0x0Fu,0xF0u,0xFFu,0x37u}) {   // the corners wrap at the border
            // Cut at the cell ahead, then two, three and four ahead.
            for (unsigned blocked:{1u,4u,7u,10u,0u}) {
                for (const std::uint8_t block_value:{1,2,3,4,5,6,7,0x84,0x0C,0x09}) {
                    auto layout=walls();
                    layout[party]=0;
                    for (unsigned k=1;k<=10;++k) layout[cell_at(party,facing,k)]=(k==2||k==5||k==8||k==3||k==6||k==9) ? 1 : 0;
                    // Walkable front values: floor, the object value, floor with bit 7.
                    for (const unsigned k:{1u,4u,7u,10u}) layout[cell_at(party,facing,k)]=static_cast<std::uint8_t>(k%3==1 ? 8 : 0x80);
                    if (blocked) layout[cell_at(party,facing,blocked)]=block_value;
                    const unsigned last=blocked==1 ? 1 : blocked==4 ? 4 : blocked==7 ? 7 : 10;
                    auto expected=cone(party,facing,last);
                    // A blocked cell ahead is seen alone; otherwise the sides
                    // of each depth reached are seen too.
                    check(game::view_cells(t,layout,static_cast<std::uint8_t>(party),static_cast<std::uint8_t>(facing))==expected,
                          "seen cone for facing "+std::to_string(facing)+", party "+std::to_string(party)+", blocked index "+std::to_string(blocked));
                    ++checks;
                    if (!blocked) break;
                }
            }
        }
    }
    // The side cells' values never extend the cone.
    auto layout=walls();
    for (unsigned k=0;k<=13;++k) layout[cell_at(0x88,1,k)]=0;
    layout[cell_at(0x88,1,4)]=1;
    check(game::view_cells(t,layout,0x88,1)==cone(0x88,1,4),"open sides do not extend the cone past a blocked front");
    check(!game::view_cells(t,layout,0x88,1).test(cell_at(0x88,1,dungeon::behind)) &&
          !game::view_cells(t,layout,0x88,1).test(cell_at(0x88,1,12)),"the cells behind and beside the party are not seen");
    // The tracker: the standing cell is always seen, cones add up, only the
    // facing's low two bits count.
    game::ExplorationTracker tracker(t);
    auto all_walls=walls();
    tracker.observe({7,0x88,0,all_walls});
    check(tracker.exploration().floors[7].seen==cells({0x88,0x78}),"walled in: the cell stood on and the wall ahead");
    tracker.observe({7,0x88,5,all_walls});   // facing 5 is facing 1
    check(tracker.exploration().floors[7].seen==cells({0x88,0x78,0x89}),"seen cells add up; facing & 3");
    std::cout<<checks<<" seen-cone checks over every facing, cut depth and border corner.\n";
}

// ---- Found rule ------------------------------------------------------------------

void fake_walls() {
    const auto t=tables({},{});
    // Facing right on row 5: the party at 55h, a fake wall at 56h, floor at 57h.
    auto layout=walls();
    for (unsigned c=0x51;c<=0x5E;++c) layout[c]=0;
    layout[0x56]=7;
    const game::DungeonSample before{3,0x55,1,layout}, after{3,0x57,1,layout};
    auto middle_found=[&](std::initializer_list<game::DungeonSample> samples) {
        game::ExplorationTracker tracker(t);
        for (const auto& s:samples) tracker.observe(s);
        return tracker.exploration().floors[3].found.test(0x56);
    };
    check(middle_found({before,after}),"a fake wall walked through is found");
    check(middle_found({before,before,before,after}),"repeated samples before the walk keep the previous sample");
    check(!middle_found({after,before}),"walking back the other way with the old facing is not a walk through");
    check(!middle_found({before,{4,0x55,1,layout},after}),"a sample on another floor between");
    check(!middle_found({before,{3,0x57,2,layout}}),"a changed facing");
    check(!middle_found({before,{3,0x55,0,layout},after}),"a turn between");
    check(!middle_found({before,{3,0x56,1,layout},after}),"a sample on the middle cell between");
    check(!middle_found({before,{3,0x58,1,layout}}),"three cells apart");
    check(middle_found({before,after,{9,0x55,1,layout},before}),"found stays found");
    for (const std::uint8_t v:{0,1,2,3,4,5,6,8,0x84,0x0C}) {
        auto other=layout; other[0x56]=v;
        check(!middle_found({{3,0x55,1,other},{3,0x57,1,other}}),"the middle cell must be a fake wall (value "+std::to_string(v)+")");
    }
    auto bit7=layout; bit7[0x56]=0x87;
    check(middle_found({{3,0x55,1,bit7},{3,0x57,1,bit7}}),"only the kind of the middle cell counts");
    // Sideways: facing up, the party moves two cells right over a fake wall.
    check(!middle_found({{3,0x55,0,layout},{3,0x57,0,layout}}),"a sideways two-cell move");
    // Every facing, and the 8-bit wrap of the position.
    for (unsigned facing=0;facing<4;++facing)
        for (const unsigned from:{0x88u,0x00u,0xFFu}) {
            auto l=walls();
            const auto mid=cell_at(from,facing,1), to=cell_at(from,facing,4);
            l[from]=0; l[mid]=7; l[to]=0;
            game::ExplorationTracker tracker(t);
            tracker.observe({12,static_cast<std::uint8_t>(from),static_cast<std::uint8_t>(facing),l});
            tracker.observe({12,to,static_cast<std::uint8_t>(facing),l});
            check(tracker.exploration().floors[12].found==cells({mid}),"fake wall in facing "+std::to_string(facing));
        }
    // Reset and replace forget the previous sample.
    {
        game::ExplorationTracker tracker(t);
        tracker.observe(before); tracker.reset(); tracker.observe(after);
        check(!tracker.exploration().floors[3].found.any(),"reset clears the previous sample");
        tracker.observe(before); tracker.replace(game::Exploration{}); tracker.observe(after);
        check(!tracker.exploration().floors[3].found.any(),"replace clears the previous sample");
        // Leaving the dungeon between two samples: no walk through.
        tracker.observe(before);
        check(tracker.leave() && !tracker.leave(),"leave forgets the previous sample once");
        tracker.observe(after);
        check(!tracker.exploration().floors[3].found.any(),"leaving and coming back is not a walk through a fake wall");
        tracker.observe(before); tracker.observe(after);
        check(tracker.exploration().floors[3].found.test(0x56),"consecutive samples after a return still find it");
    }
    // The display: wall until walked through, then ordinary floor.
    game::ExplorationTracker tracker(t);
    Block block{};
    tracker.observe(before);
    check(game::build_view(t,tracker.exploration().floors[3],3,layout,0x55,1,block).cells[0x56].kind==MapKind::wall,"a fake wall shows as wall");
    tracker.observe(after);
    check(game::build_view(t,tracker.exploration().floors[3],3,layout,0x57,1,block).cells[0x56].kind==MapKind::floor,
          "a fake wall walked through shows as floor");
    std::cout<<"Fake-wall rule: positive in every facing and across the wrap; every negative case refused.\n";
}

void pits_and_chests() {
    // Floor 2: a pit at 44h (no record); two chests; a fight
    // and an event; a doubled cell whose first record is a fight; a doubled
    // cell whose first record is a chest; a dead chest record on plain floor.
    const auto t=tables({
        {2,0x46,0xC50A,0},   // item chest
        {2,0x48,0xC50B,1},   // money chest
        {2,0x4A,0xC50C,2},   // fixed fight
        {2,0x4C,0xC50D,3},   // event
        {2,0x4E,0xC50E,2}, {2,0x4E,0xC50F,0},   // fight shadows a chest
        {2,0x84,0xC510,1}, {2,0x84,0xC511,2},   // chest shadows a fight
        {2,0x86,0xC512,0},   // dead: the cell is plain floor
        {3,0x46,0xC513,0},   // another floor's record at the same cell
    },{});
    auto layout=walls();
    for (unsigned c=0x40;c<=0x9F;++c) layout[c]=0;
    for (const unsigned c:{0x44u,0x46u,0x48u,0x4Au,0x4Cu,0x4Eu,0x84u}) layout[c]=8;
    Block block{};
    auto found_after=[&](std::uint8_t floor, std::initializer_list<std::pair<unsigned,unsigned>> positions) {
        game::ExplorationTracker tracker(t);
        for (const auto& [cell,facing]:positions)
            tracker.observe({floor,static_cast<std::uint8_t>(cell),static_cast<std::uint8_t>(facing),layout});
        return tracker.exploration().floors[floor].found;
    };
    auto view_after=[&](const game::DungeonLayout& l, std::uint8_t floor, std::initializer_list<std::pair<unsigned,unsigned>> positions,
                        const Block& b) {
        game::ExplorationTracker tracker(t);
        for (const auto& [cell,facing]:positions)
            tracker.observe({floor,static_cast<std::uint8_t>(cell),static_cast<std::uint8_t>(facing),l});
        return game::build_view(t,tracker.exploration().floors[floor],floor,l,0,0,b);
    };
    // Pitfall: found when stood on, not when seen ahead; a record makes it no pit.
    check(found_after(2,{{0x45,3}})==game::CellSet{},"a pit ahead is not found");
    check(view_after(layout,2,{{0x45,3}},block).cells[0x44].kind==MapKind::floor,"a pit seen ahead shows as floor");
    check(found_after(2,{{0x44,0}})==cells({0x44}),"a pit stood on is found");
    check(view_after(layout,2,{{0x44,0}},block).cells[0x44].kind==MapKind::pitfall,"a pit stood on shows as a pitfall");
    check(!found_after(2,{{0x46,0}}).test(0x46),"a chest stood on is not a pit");
    check(found_after(3,{{0x44,0}})==cells({0x44}) && found_after(3,{{0x48,0}})==cells({0x48}) && found_after(3,{{0x46,0}}).none(),
          "records belong to their own floor: on floor 3 only 46h has one; 44h and 48h are pits there");
    // Chests carry no found state: a seen chest cell shows at once.
    check(found_after(2,{{0x47,3},{0x47,1},{0x85,3}}).none(),"chests are never added to the found set");
    // A chest at every index of the cone (each depth and each side cell) in
    // every facing, on an open floor; behind, beside the party and past the
    // cone it is not shown.
    auto open=walls();
    for (unsigned c=0;c<256;++c) open[c]=0;
    open[0x46]=8;
    unsigned shown=0;
    for (unsigned facing=0;facing<4;++facing) {
        for (unsigned k=1;k<=10;++k) {
            const auto from=static_cast<unsigned>(0x46-geometric_offset(facing,k));
            const auto view=view_after(open,2,{{from,facing}},block);
            check(view.cells[0x46].kind==MapKind::chest,"a chest at cone index "+std::to_string(k)+" in facing "+std::to_string(facing)+" shows");
            ++shown;
        }
        for (const unsigned k:{11u,12u,13u}) {
            const auto from=static_cast<unsigned>(0x46-geometric_offset(facing,k));
            check(view_after(open,2,{{from,facing}},block).cells[0x46].kind==MapKind::unknown,
                  "a chest behind or beside the party is not shown (index "+std::to_string(k)+")");
        }
        const auto far=static_cast<unsigned>(0x46-5*ahead_of[facing]);
        check(view_after(open,2,{{far,facing}},block).cells[0x46].kind==MapKind::unknown,"a chest five cells ahead is not shown");
    }
    // A blocked view hides a chest past the wall.
    auto blocked=open; blocked[0x47]=1;
    check(view_after(blocked,2,{{0x48,3}},block).cells[0x46].kind==MapKind::unknown,"a chest behind a wall is not shown");
    // Fights, events, shadowed, dead records and a money chest, all seen.
    const auto seen_all={std::pair<unsigned,unsigned>{0x44,0},{0x45,1},{0x47,1},{0x49,1},{0x4B,1},{0x4D,1},{0x85,3},{0x87,3}};
    auto view=view_after(layout,2,seen_all,block);
    check(view.cells[0x44].kind==MapKind::pitfall && view.cells[0x46].kind==MapKind::chest && view.cells[0x48].kind==MapKind::chest &&
          view.cells[0x84].kind==MapKind::chest,"the pit stood on and the seen chests (first match included) show as such");
    check(view.cells[0x4A].kind==MapKind::floor && view.cells[0x4C].kind==MapKind::floor && view.cells[0x4E].kind==MapKind::floor &&
          view.cells[0x86].kind==MapKind::floor,"fights, events, a chest shadowed by a fight and a dead record show as floor");
    for (const unsigned cell:{0x4Au,0x4Cu,0x4Eu})
        for (unsigned facing=0;facing<4;++facing) {
            const auto front=static_cast<unsigned>(cell-ahead_of[facing]), back=static_cast<unsigned>(cell+ahead_of[facing]);
            check(!found_after(2,{{front,facing},{back,facing},{cell,facing}}).test(cell) &&
                  view_after(layout,2,{{front,facing},{cell,facing}},block).cells[cell].kind==MapKind::floor,
                  "a fixed fight or event (or a chest shadowed by one) is never marked");
        }
    // Opened: the first record's done flag in the game block is FFh.
    block[0x20A]=0xFF; block[0x20B]=0x01; block[0x211]=0xFF;   // C50A, C50B, the shadowed record's C511
    view=view_after(layout,2,seen_all,block);
    check(view.cells[0x46].kind==MapKind::opened_chest && view.cells[0x48].kind==MapKind::chest && view.cells[0x84].kind==MapKind::chest,
          "opened only with the first record's flag at FFh");
    block[0x210]=0xFF;
    check(view_after(layout,2,seen_all,block).cells[0x84].kind==MapKind::opened_chest,"the first record's flag opens it");
    // A stored exploration: a found fight shows as floor, a found pit without
    // being seen shows as a pitfall.
    game::FloorExploration odd; odd.found.set(0x4A); odd.found.set(0x44);
    view=game::build_view(t,odd,2,layout,0,0,Block{});
    check(view.cells[0x4A].kind==MapKind::floor && view.cells[0x44].kind==MapKind::pitfall,"found cells show without being seen");
    (void)shown;
    std::cout<<"Pitfall and chest rules: pits when stood on, chests when seen at any cone index, first match, fights and events never marked, opened from the block.\n";
}

// ---- Display rule -----------------------------------------------------------------

void display() {
    // Exit records on floor 1: none at 10h, kinds 00h, 7Fh, 80h, FCh-FFh.
    const auto t=tables({},{{1,0x11,0x00},{1,0x12,0x7F},{1,0x13,0x80},{1,0x14,0xFC},{1,0x15,0xFD},{1,0x16,0xFE},{1,0x17,0xFF},
                            {2,0x10,0xFF}});
    const MapKind by_kind[8]{MapKind::floor,MapKind::wall,MapKind::stairs_up,MapKind::stairs_down,MapKind::door,
                             MapKind::locked_door,MapKind::magic_door,MapKind::wall};
    unsigned cases=0;
    for (unsigned value=0;value<16;++value)
        for (const unsigned bit7:{0u,0x80u})
            for (unsigned state=0;state<4;++state)
                for (unsigned record=0;record<8;++record) {
                    const bool seen=state&1, found=state&2;
                    const auto cell=static_cast<std::uint8_t>(0x10+record);
                    auto layout=walls();
                    layout[cell]=static_cast<std::uint8_t>(value|bit7);
                    game::FloorExploration f;
                    f.seen.set(cell,seen); f.found.set(cell,found);
                    const auto view=game::build_view(t,f,1,layout,0x88,0,Block{});
                    const auto& out=view.cells[cell];
                    MapKind kind=MapKind::unknown;
                    if (seen || found) {
                        if (value==8) kind=found ? MapKind::pitfall : MapKind::floor;   // no object record on floor 1
                        else if ((value&7)==7) kind=found ? MapKind::floor : MapKind::wall;
                        else kind=by_kind[value&7];
                    }
                    const unsigned k=value&7;
                    // Exit: seen stairs or door with bit 3 whose first record is
                    // missing (10h) or below 80h (11h, 12h).
                    const bool exit=seen && (value&8) && k>=2 && k<=6 && record<=2;
                    check(out.kind==kind && out.exit==exit,"display of value "+std::to_string(value|bit7)+" (seen "+std::to_string(seen)+
                          ", found "+std::to_string(found)+", record "+std::to_string(record)+")");
                    ++cases;
                }
    // The exit lookup is keyed by floor: floor 2's FFh record at 10h does not
    // reach floor 1, and on floor 2 it hides the marker.
    auto layout=walls(); layout[0x10]=0x0A;
    game::FloorExploration f; f.seen.set(0x10);
    check(game::build_view(t,f,1,layout,0,0,Block{}).cells[0x10].exit && !game::build_view(t,f,2,layout,0,0,Block{}).cells[0x10].exit,
          "exit records are looked up on the shown floor");
    // Party: its cell and the arrow from the offsets.
    for (unsigned facing=0;facing<8;++facing) {
        const auto view=game::build_view(t,f,1,layout,0x5A,static_cast<std::uint8_t>(facing),Block{});
        check(view.party_cell==0x5A && view.party_ahead==ahead_of[facing&3],"party cell and arrow");
    }
    std::cout<<cases<<" display cases: every value with and without bit 7, seen or not, found or not, every exit record case.\n";
}

void floor_keying() {
    const auto t=tables({{4,0x44,0xC50A,0}},{});
    auto layout=walls();
    layout[0x44]=8; layout[0x45]=0; layout[0x46]=8;
    game::ExplorationTracker tracker(t);
    check(tracker.exploration().empty(),"a new tracker is empty");
    tracker.observe({4,0x45,3,layout});   // the chest at 44h seen on floor 4
    tracker.observe({5,0x45,3,layout});   // the same cell on floor 5: a pit there, not a chest
    tracker.observe({5,0x46,3,layout});   // standing on the pit of floor 5
    tracker.observe({61,0x45,0,layout});
    tracker.observe({255,0x45,0,layout});
    const auto& e=tracker.exploration();
    check(e.floors[4].found.none() && e.floors[5].found==cells({0x46}),"found cells per floor ID");
    check(game::build_view(t,e.floors[4],4,layout,0x45,3,Block{}).cells[0x44].kind==MapKind::chest &&
          game::build_view(t,e.floors[5],5,layout,0x45,3,Block{}).cells[0x44].kind==MapKind::floor,
          "the same cell is a chest on floor 4 and plain on floor 5");
    check(e.floors[4].seen==game::view_cells(t,layout,0x45,3) &&
          e.floors[5].seen==(game::view_cells(t,layout,0x45,3)|game::view_cells(t,layout,0x46,3)),"seen cells per floor ID");
    for (unsigned f=0;f<dungeon::floor_count;++f)
        if (f!=4 && f!=5) check(e.floors[f]==game::FloorExploration{},"floors 61 and 255 are ignored; others untouched");
    const auto saved=e;
    tracker.reset();
    check(tracker.exploration().empty(),"reset empties the exploration");
    tracker.replace(saved);
    check(tracker.exploration()==saved && !saved.empty(),"replace restores a stored exploration");
    game::Exploration only_found; only_found.floors[60].found.set(0);
    check(!only_found.empty(),"a found cell alone is not empty");
    std::cout<<"Exploration is keyed by floor ID; floors of 61 and more are ignored.\n";
}

// ---- Property: unknown cells never change the view ------------------------------------

void unknown_cells_property() {
    std::mt19937_64 random(0xA770);
    std::vector<Object> objects;
    std::vector<Exit> exits;
    for (unsigned i=0;i<60;++i)
        objects.push_back({static_cast<std::uint8_t>(random()%4),static_cast<std::uint8_t>(random()),
                           static_cast<std::uint16_t>(0xC50A+i),static_cast<std::uint8_t>(random()%5)});
    for (unsigned i=0;i<40;++i)
        exits.push_back({static_cast<std::uint8_t>(random()%4),static_cast<std::uint8_t>(random()),static_cast<std::uint8_t>(random())});
    const auto t=tables(objects,exits);
    unsigned runs=0;
    for (unsigned run=0;run<3000;++run) {
        const auto floor=static_cast<std::uint8_t>(random()%4);
        game::DungeonLayout layout;
        for (auto& v:layout) v=static_cast<std::uint8_t>((random()%16)|(random()%4==0 ? 0x80 : 0));
        game::FloorExploration f;
        const unsigned density=static_cast<unsigned>(random()%100);
        for (unsigned c=0;c<256;++c) { f.seen.set(c,random()%100<density); f.found.set(c,random()%100<density/3); }
        Block block;
        for (auto& b:block) b=static_cast<std::uint8_t>(random()%3==0 ? 0xFF : random());
        const auto cell=static_cast<std::uint8_t>(random()), facing=static_cast<std::uint8_t>(random());
        const auto view=game::build_view(t,f,floor,layout,cell,facing,block);
        auto mutated=layout;
        for (unsigned c=0;c<256;++c) {
            const bool known=f.seen.test(c) || f.found.test(c);
            check(known || view.cells[c]==game::MapCell{},"an unknown cell shows unknown");
            if (!known) mutated[c]=static_cast<std::uint8_t>(random());
        }
        check(game::build_view(t,f,floor,mutated,cell,facing,block)==view,"changing an unknown cell changed the view");
        ++runs;
    }
    std::cout<<runs<<" random layouts and explorations: changing unknown cells never changes the view.\n";
}
}

int main() {
    try {
        if (!game::Exploration{}.empty()) throw std::runtime_error("A new exploration is not empty.");
        seen_rule();
        fake_walls();
        pits_and_chests();
        display();
        floor_keying();
        unknown_cells_property();
        {   // Floors without stairs are stacks of one: FLOOR 1; the numbers outside the table have no label.
            const auto plain=tables({},{});
            check(plain.floor_level(0)==1 && plain.floor_level(60)==1 && plain.floor_level(61)==0,"floors without stairs are FLOOR 1");
            check(game::build_view(plain,{},5,walls(),0,0,std::array<std::uint8_t,dungeon::game_block_bytes>{}).floor_level==1,"the view carries the floor's level");
        }
        std::cout<<"Automap model checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
