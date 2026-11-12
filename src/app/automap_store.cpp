// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/automap_store.h"
#include "rom/sha256.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace psmodern::app {
namespace {
namespace dungeon=game::dungeon;
constexpr std::size_t set_bytes=dungeon::cells_per_floor/8;
constexpr std::string_view digits="0123456789abcdef";

bool valid_digest(std::string_view digest) {
    return digest.size()==64 && std::all_of(digest.begin(),digest.end(),[](char c) { return digits.find(c)!=std::string_view::npos; });
}
void hex_byte(std::string& out, unsigned value) { out+=digits[(value>>4)&15]; out+=digits[value&15]; }
void hex_set(std::string& out, const game::CellSet& cells) {
    for (std::size_t k=0;k<set_bytes;++k) {
        unsigned byte=0;
        for (unsigned bit=0;bit<8;++bit) if (cells.test(8*k+bit)) byte|=1u<<bit;
        hex_byte(out,byte);
    }
}

[[noreturn]] void refuse(std::size_t line, const std::string& why) {
    throw std::runtime_error("Automap file line "+std::to_string(line)+": "+why+".");
}
// The text split into lines, each ended by exactly one LF.
class Lines {
public:
    explicit Lines(std::string_view text) : text_(text) {}
    bool done() const { return at_==text_.size(); }
    std::size_t number() const { return number_; }
    std::string_view next() {
        ++number_;
        const auto end=text_.find('\n',at_);
        if (end==std::string_view::npos) refuse(number_,"the line does not end in a line feed");
        const auto line=text_.substr(at_,end-at_);
        at_=end+1;
        return line;
    }
private:
    std::string_view text_;
    std::size_t at_=0, number_=0;
};
// Space-separated fields; empty fields (two spaces, a leading or trailing
// space) are refused.
std::vector<std::string_view> fields(std::string_view line, std::size_t number) {
    std::vector<std::string_view> out;
    std::size_t at=0;
    for (;;) {
        const auto end=line.find(' ',at);
        const auto field=line.substr(at,end==std::string_view::npos ? std::string_view::npos : end-at);
        if (field.empty()) refuse(number,"empty field");
        out.push_back(field);
        if (end==std::string_view::npos) return out;
        at=end+1;
    }
}
int hex_value(char c) {
    const auto at=digits.find(c);
    return at==std::string_view::npos ? -1 : static_cast<int>(at);
}
unsigned parse_hex_byte(std::string_view text, std::size_t number) {
    const auto high=hex_value(text[0]), low=hex_value(text[1]);
    if (high<0 || low<0) refuse(number,"not lowercase hex");
    return static_cast<unsigned>(high*16+low);
}
game::CellSet parse_set(std::string_view text, std::size_t number) {
    if (text.size()!=2*set_bytes) refuse(number,"a cell set is not 64 hex digits");
    game::CellSet cells;
    for (std::size_t k=0;k<set_bytes;++k) {
        const auto byte=parse_hex_byte(text.substr(2*k,2),number);
        for (unsigned bit=0;bit<8;++bit) if (byte&(1u<<bit)) cells.set(8*k+bit);
    }
    return cells;
}
// A canonical decimal count from 0 to 61: no sign, no leading zero.
std::size_t parse_count(std::string_view text, std::size_t number) {
    if (text.size()>2 || (text.size()==2 && text[0]=='0') ||
        !std::all_of(text.begin(),text.end(),[](char c) { return c>='0' && c<='9'; }))
        refuse(number,"the floor count is not a decimal number without leading zeros");
    std::size_t value=0;
    for (const char c:text) value=value*10+static_cast<std::size_t>(c-'0');
    if (value>dungeon::floor_count) refuse(number,"more floors than the game has");
    return value;
}
}

const game::Exploration* AutomapImage::find(std::string_view digest) const {
    const auto found=records_.find(digest);
    return found==records_.end() ? nullptr : &found->second;
}
void AutomapImage::put(std::string digest, const game::Exploration& exploration) {
    if (!valid_digest(digest)) throw std::invalid_argument("An automap record needs a 64-digit lowercase hex digest.");
    records_[std::move(digest)]=exploration;
}
void AutomapImage::prune(const std::set<std::string>& keep) {
    if (keep.size()>automap_max_records) throw std::invalid_argument("An automap image can keep at most 10 records.");
    std::erase_if(records_,[&](const auto& record) { return !keep.contains(record.first); });
}

AutomapImage parse_automap(std::string_view text) {
    if (text.size()>automap_max_bytes) throw std::runtime_error("The automap file is larger than 98,304 bytes.");
    Lines lines(text);
    if (lines.done()) refuse(1,"the file is empty");
    const auto header=lines.next();
    if (header!=automap_header.substr(0,automap_header.size()-1)) {
        if (header.starts_with("psmodern-automap ")) refuse(1,"unsupported automap version");
        refuse(1,"not an automap file");
    }
    AutomapImage image;
    std::string previous_digest;
    while (!lines.done()) {
        const auto line=lines.next();
        const auto number=lines.number();
        const auto record=fields(line,number);
        if (record.size()!=3 || record[0]!="record") refuse(number,"expected a record line");
        if (!valid_digest(record[1])) refuse(number,"the digest is not 64 lowercase hex digits");
        if (image.size()==automap_max_records) refuse(number,"more than 10 records");
        if (!previous_digest.empty() && record[1]<=previous_digest)
            refuse(number,record[1]==previous_digest ? "duplicate digest" : "records out of order");
        const auto count=parse_count(record[2],number);
        game::Exploration exploration;
        int previous_floor=-1;
        for (std::size_t i=0;i<count;++i) {
            if (lines.done()) refuse(number,"the record has fewer floor lines than it counts");
            const auto floor_line=lines.next();
            const auto at=lines.number();
            const auto f=fields(floor_line,at);
            if (f.size()!=4 || f[0]!="floor") refuse(at,"expected a floor line");
            if (f[1].size()!=2) refuse(at,"the floor id is not two hex digits");
            const auto id=parse_hex_byte(f[1],at);
            if (id>=dungeon::floor_count) refuse(at,"floor id above 60");
            if (static_cast<int>(id)<=previous_floor) refuse(at,"floors out of order or repeated");
            previous_floor=static_cast<int>(id);
            auto& floor=exploration.floors[id];
            floor.seen=parse_set(f[2],at);
            floor.found=parse_set(f[3],at);
            if (floor.seen.none() && floor.found.none()) refuse(at,"a floor line without any cell");
        }
        image.put(std::string(record[1]),exploration);
        previous_digest=std::string(record[1]);
    }
    return image;
}

std::string write_automap(const AutomapImage& image) {
    if (image.size()>automap_max_records) throw std::length_error("An automap file holds at most 10 records; prune first.");
    std::string out(automap_header);
    for (const auto& [digest,exploration]:image.records_) {
        std::size_t count=0;
        for (const auto& floor:exploration.floors) count+=floor.seen.any() || floor.found.any();
        out+="record "+digest+" "+std::to_string(count)+"\n";
        for (std::size_t id=0;id<exploration.floors.size();++id) {
            const auto& floor=exploration.floors[id];
            if (floor.seen.none() && floor.found.none()) continue;
            out+="floor ";
            hex_byte(out,static_cast<unsigned>(id));
            out+=' '; hex_set(out,floor.seen);
            out+=' '; hex_set(out,floor.found);
            out+='\n';
        }
    }
    // Never produce a file the reader would refuse (unreachable by the limits
    // above; checked all the same).
    if (out.size()>automap_file_max) throw std::logic_error("The automap writer exceeded its derived size limit.");
    AutomapImage back;
    try { back=parse_automap(out); } catch (const std::runtime_error& error) {
        throw std::logic_error(std::string("The automap writer produced a file its reader refuses: ")+error.what());
    }
    if (!(back==image)) throw std::logic_error("The automap writer's output does not read back equal.");
    return out;
}

std::string automap_digest(std::span<const std::uint8_t,dungeon::game_block_bytes> game_block) {
    return sha256(game_block);
}
std::set<std::string> used_slot_digests(std::span<const std::uint8_t> cartridge_ram) {
    if (cartridge_ram.size()<dungeon::slot_offset(dungeon::save_slots)+dungeon::game_block_bytes)
        throw std::invalid_argument("The cartridge RAM image is smaller than the five save slots.");
    std::set<std::string> digests;
    for (unsigned slot=1;slot<=dungeon::save_slots;++slot)
        if (cartridge_ram[dungeon::slot_used_flag(slot)]!=0)
            digests.insert(sha256(cartridge_ram.subspan(dungeon::slot_offset(slot),dungeon::game_block_bytes)));
    return digests;
}
}
