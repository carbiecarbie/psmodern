// SPDX-License-Identifier: GPL-3.0-or-later
// The automap file format without a ROM (milestone 7, part M1): round trip,
// canonical output, refusals, pruning, version handling and the largest file.
#include "app/automap_store.h"
#include "rom/sha256.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psmodern;
namespace dungeon=game::dungeon;
namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
template<class Error, class F> bool throws(F&& f) {
    try { f(); } catch (const Error&) { return true; }
    return false;
}
std::string digest(unsigned n) {
    std::string text(64,'0');
    const std::string hex="0123456789abcdef";
    for (unsigned i=0;i<8;++i) text[63-i]=hex[(n>>(4*i))&15];
    text[0]='a';
    return text;
}
game::Exploration random_exploration(std::mt19937_64& random) {
    game::Exploration e;
    for (auto& f:e.floors) {
        if (random()%3) continue;
        for (unsigned c=0;c<256;++c) { f.seen.set(c,random()%2); f.found.set(c,random()%7==0); }
    }
    return e;
}
game::Exploration full() {
    game::Exploration e;
    for (auto& f:e.floors) { f.seen.set(); f.found.set(); }
    return e;
}

void round_trip() {
    std::mt19937_64 random(0xA4705);
    for (unsigned run=0;run<200;++run) {
        app::AutomapImage image;
        const auto records=static_cast<unsigned>(random()%(app::automap_max_records+1));
        for (unsigned i=0;i<records;++i) image.put(digest(static_cast<unsigned>(random())),random_exploration(random));
        const auto text=app::write_automap(image);
        const auto back=app::parse_automap(text);
        check(back==image,"a written image reads back equal");
        check(app::write_automap(back)==text,"writing is canonical");
    }
    // An empty exploration is a record with no floor line; an empty image is
    // the header alone.
    app::AutomapImage empty_record;
    empty_record.put(digest(1),game::Exploration{});
    check(app::write_automap(empty_record)==std::string(app::automap_header)+"record "+digest(1)+" 0\n","an empty record");
    check(app::write_automap(app::AutomapImage{})==app::automap_header,"an empty image");
    check(app::parse_automap(app::automap_header)==app::AutomapImage{},"the header alone is an empty image");
    // The exact text: cell 0 in bit 0 of byte 0, cell 9 in bit 1 of byte 1,
    // cell 255 in bit 7 of byte 31; floor IDs in two lowercase hex digits.
    game::Exploration e;
    e.floors[0x3C].seen.set(0); e.floors[0x3C].seen.set(255); e.floors[0x0A].found.set(9);
    app::AutomapImage one;
    one.put(digest(0xBEEF),e);
    const std::string zeros(64,'0');
    std::string seen=zeros; seen[1]='1'; seen[62]='8';
    std::string found=zeros; found[3]='2';
    const auto expected=std::string("psmodern-automap 1\n")+"record "+digest(0xBEEF)+" 2\n"+
        "floor 0a "+zeros+" "+found+"\n"+"floor 3c "+seen+" "+zeros+"\n";
    check(app::write_automap(one)==expected,"the bit, byte and line layout");
    check(app::parse_automap(expected)==one,"the documented layout reads back");
    std::cout<<"Round trip: 200 random images read back equal and rewrite to the same text.\n";
}

void lookups_and_pruning() {
    app::AutomapImage image;
    std::mt19937_64 random(7);
    const auto a=random_exploration(random), b=random_exploration(random);
    image.put(digest(1),a); image.put(digest(2),b);
    check(image.find(digest(1)) && *image.find(digest(1))==a && image.find(digest(3))==nullptr && image.find("x")==nullptr,
          "find gives the record or nothing (an empty map)");
    image.put(digest(1),b);
    check(image.size()==2 && *image.find(digest(1))==b,"put replaces a record of the same digest");
    check(throws<std::invalid_argument>([&] { image.put("ABC",a); }) &&
          throws<std::invalid_argument>([&] { image.put(std::string(64,'A'),a); }) &&
          throws<std::invalid_argument>([&] { image.put(std::string(63,'0'),a); }),"put refuses a malformed digest");
    // Pruning keeps the union of used slots on disk and in the image being
    // written; it never keeps more than 10.
    for (unsigned i=3;i<=12;++i) image.put(digest(i),a);
    check(image.size()==12,"an image may hold any number of records before pruning");
    check(throws<std::length_error>([&] { (void)app::write_automap(image); }),"the writer refuses more than 10 records");
    std::set<std::string> on_disk{digest(1),digest(2),digest(3),digest(4),digest(5)}, written{digest(5),digest(6),digest(7),digest(40)};
    std::set<std::string> keep=on_disk; keep.insert(written.begin(),written.end());
    image.prune(keep);
    check(image.size()==7 && image.find(digest(1)) && image.find(digest(7)) && !image.find(digest(8)) && !image.find(digest(40)),
          "prune keeps the records of the union only");
    std::set<std::string> eleven;
    for (unsigned i=1;i<=11;++i) eleven.insert(digest(i));
    const auto before=image;
    check(throws<std::invalid_argument>([&] { image.prune(eleven); }) && image==before,"prune refuses more than 10 digests, changing nothing");
    image.prune({});
    check(image.size()==0,"no used slot: nothing kept");
    std::cout<<"Lookups and pruning checked.\n";
}

void largest_file() {
    app::AutomapImage image;
    for (unsigned i=0;i<app::automap_max_records;++i) image.put(digest(i),full());
    const auto text=app::write_automap(image);
    check(text.size()==app::automap_file_max && text.size()==85559,"10 records of 61 full floors: 85,559 bytes");
    check(app::parse_automap(text)==image,"the largest file reads back equal");
    std::cout<<"Largest file: "<<text.size()<<" bytes (reader limit "<<app::automap_max_bytes<<") written and read back.\n";
}

void refusals() {
    app::AutomapImage image;
    std::mt19937_64 random(11);
    image.put(digest(1),random_exploration(random));
    game::Exploration e; e.floors[5].seen.set(3);
    image.put(digest(2),e);
    const auto good=app::write_automap(image);
    const std::string head(app::automap_header);
    const std::string zeros(64,'0');
    const std::string ones="01"+std::string(62,'0');
    auto record=[&](unsigned n, const std::string& count) { return "record "+digest(n)+" "+count+"\n"; };
    auto floor=[&](const std::string& id) { return "floor "+id+" "+ones+" "+zeros+"\n"; };
    const std::vector<std::pair<const char*,std::string>> bad{
        {"empty text",""},
        {"unknown header","psmodern-automat 1\n"},
        {"newer version","psmodern-automap 2\n"},
        {"version with two digits","psmodern-automap 10\n"},
        {"header without version","psmodern-automap\n"},
        {"header without line feed","psmodern-automap 1"},
        {"carriage return",std::string("psmodern-automap 1\r\n")},
        {"carriage return in a record",head+"record "+digest(1)+" 0\r\n"},
        {"missing final line feed",good.substr(0,good.size()-1)},
        {"blank line",head+"\n"},
        {"duplicate digest",head+record(1,"0")+record(1,"0")},
        {"digests out of order",head+record(2,"0")+record(1,"0")},
        {"uppercase digest",head+"record "+std::string(64,'A')+" 0\n"},
        {"short digest",head+"record "+digest(1).substr(1)+" 0\n"},
        {"floor 61",head+record(1,"1")+floor("3d")},
        {"floor ff",head+record(1,"1")+floor("ff")},
        {"floor id one digit",head+record(1,"1")+floor("5")},
        {"floor id uppercase",head+record(1,"1")+floor("0A")},
        {"floors out of order",head+record(1,"2")+floor("05")+floor("04")},
        {"repeated floor",head+record(1,"2")+floor("05")+floor("05")},
        {"floor line without a cell",head+record(1,"1")+"floor 05 "+zeros+" "+zeros+"\n"},
        {"odd hex",head+record(1,"1")+"floor 05 "+ones.substr(1)+" "+zeros+"\n"},
        {"uppercase cell set",head+record(1,"1")+"floor 05 0A"+std::string(62,'0')+" "+zeros+"\n"},
        {"extra field",head+record(1,"1")+"floor 05 "+ones+" "+zeros+" 00\n"},
        {"missing field",head+record(1,"1")+"floor 05 "+ones+"\n"},
        {"extra record field",head+"record "+digest(1)+" 0 0\n"},
        {"double space",head+"record  "+digest(1)+" 0\n"},
        {"trailing space",head+"record "+digest(1)+" 0 \n"},
        {"count above lines",head+record(1,"2")+floor("05")},
        {"count below lines",head+record(1,"0")+floor("05")},
        {"count with a leading zero",head+record(1,"01")+floor("05")},
        {"count 62",head+record(1,"62")},
        {"negative count",head+record(1,"-1")},
        {"hex count",head+record(1,"a")},
        {"unknown line",head+"note hello\n"},
    };
    for (const auto& [name,text]:bad)
        check(throws<std::runtime_error>([&] { (void)app::parse_automap(text); }),std::string("refused: ")+name);
    // Version: the reader names an unsupported version distinctly.
    try { (void)app::parse_automap("psmodern-automap 2\n"); } catch (const std::runtime_error& error) {
        check(std::string(error.what()).find("unsupported automap version")!=std::string::npos,"an unsupported version is named");
    }
    // Over 10 records, each valid.
    std::string eleven=head;
    for (unsigned i=0;i<11;++i) eleven+=record(i,"0");
    check(throws<std::runtime_error>([&] { (void)app::parse_automap(eleven); }),"refused: 11 records");
    // Over 98,304 bytes: a valid file padded past the limit is refused by size.
    std::string large=app::write_automap([] { app::AutomapImage i; for (unsigned n=0;n<10;++n) i.put(digest(n),full()); return i; }());
    large.resize(app::automap_max_bytes+1,'\n');
    check(throws<std::runtime_error>([&] { (void)app::parse_automap(large); }),"refused: more than 98,304 bytes");
    // A refused text never yields a partial image: the good prefix of a
    // corrupt file is not returned.
    check(throws<std::runtime_error>([&] { (void)app::parse_automap(good+"garbage\n"); }),"refused whole: a good file with a bad tail");
    check(app::parse_automap(good)==image,"the good file itself reads");
    std::cout<<bad.size()+3<<" malformed variants refused whole.\n";
}

void slots() {
    std::vector<std::uint8_t> ram(8192,0);
    for (unsigned slot=1;slot<=5;++slot)
        for (unsigned i=0;i<dungeon::game_block_bytes;++i)
            ram[dungeon::slot_offset(slot)+i]=static_cast<std::uint8_t>(slot*31+i*7);
    check(app::used_slot_digests(ram).empty(),"no used flag: no digest");
    ram[dungeon::slot_used_flag(2)]=1; ram[dungeon::slot_used_flag(5)]=0x80;
    const auto digests=app::used_slot_digests(ram);
    const auto block=[&](unsigned slot) {
        return std::span<const std::uint8_t,dungeon::game_block_bytes>(ram.data()+dungeon::slot_offset(slot),dungeon::game_block_bytes);
    };
    check(digests==std::set<std::string>{sha256(block(2)),sha256(block(5))},"used slots 2 and 5 give their blocks' digests");
    check(app::automap_digest(block(2))==sha256(block(2)) && app::automap_digest(block(2)).size()==64,"the record key is the block's SHA-256");
    // Two slots with identical blocks share one digest (accepted).
    for (unsigned i=0;i<dungeon::game_block_bytes;++i) ram[dungeon::slot_offset(3)+i]=ram[dungeon::slot_offset(2)+i];
    ram[dungeon::slot_used_flag(3)]=1;
    check(app::used_slot_digests(ram).size()==2,"identical blocks share one record");
    check(throws<std::invalid_argument>([&] { (void)app::used_slot_digests(std::span(ram).first(0x17FF)); }),
          "a cartridge image smaller than the slots is refused");
    std::cout<<"Used-slot digests checked.\n";
}
}

int main() {
    try {
        if (app::AutomapImage{}.find("0")!=nullptr) throw std::runtime_error("An empty image has a record.");
        round_trip();
        lookups_and_pruning();
        largest_file();
        refusals();
        slots();
        std::cout<<"Automap file checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
