// SPDX-License-Identifier: GPL-3.0-or-later
// The static walk of the registry against the identified ROM: every place the
// machine can continue at after a segment ends must begin a segment, because
// cartridge code with no C++ version stops the game.
//   1. Every word of every code table read by a dispatcher (the three of the
//      game and the two of the sound driver) is the entry of a segment.
//   2. The final instruction of every declared exit is decoded from the ROM:
//      its jump or call target, the return point of a call and the next
//      instruction of a conditional or plain final each begin a segment. A
//      block-copy or port-output slice resumes at its own instruction.
//   3. The jumps through a register are exactly the known dispatchers, so a
//      new one cannot be registered without its tables being listed here.
// Milestone 19 (the re-verified stop sweep) makes the walk stricter:
//   4. A target in slot 2 (8000h-BFFFh) must begin a segment of the bank that
//      is really mapped there, not of any bank. Code in a paged bank keeps
//      its own bank mapped (the engine refuses a fetch after a remap). Code
//      in the fixed banks reaches slot 2 only through the three sound
//      bridges, and each one must write its bank to the slot-2 register
//      (FFFFh) in its own straight-line body before the jump; that bank is
//      the one the target must belong to.
//   5. The continuations the original pushes itself (an address loaded into a
//      register and pushed, so that a later RET goes there) must begin
//      segments; the push is checked in the ROM, so the list cannot go stale.
//   6. A block-copy or port-output slice must be that instruction, and the
//      instruction after it (where the slice completes) must begin a segment.
// Addresses only; the tables and instructions are read from the player's ROM.
#include "probe.h"
#include <map>
#include <set>
#include <sstream>

using namespace psmodern;
namespace {
struct Table { unsigned bank; std::uint16_t address; unsigned entries; };
// The code tables, by dispatcher. Each word is a handler address.
constexpr Table tables[]{
    // 00:00E6 (HL = table, A = selector)
    {0,0x00BE,20},{0,0x1912,5},{0,0x1A8A,18},{0,0x12C4,8},{0,0x1A66,18},{0,0x1C97,5},{0,0x1DFE,19},
    {0,0x21FB,3},{0,0x220A,64},{0,0x3C2A,20},{1,0x5827,22},{1,0x621F,5},{1,0x63B8,11},
    // 00:0131 (the frame handlers)
    {0,0x018F,12},
    // 01:4769 (the interactions)
    {1,0x4773,182},
    // The sound driver: requests (0C:830A) and score commands (0C:8519, 0C:851C)
    {12,0x82E5,5},{12,0x8530,18},
};
// The segments whose final instruction jumps through HL, with the exit address.
const std::set<std::pair<RoutineEntry,std::uint16_t>> dispatchers{
    {{0,0x00E6},0x00F0},{{0,0x0131},0x0142},{{1,0x4769},0x4772},
    {{12,0x830A},0x8318},{{12,0x8519},0x852B},{{12,0x851C},0x852B}};
// The jumps from a fixed bank into slot 2 (the sound driver's bridges), by
// the address of the jump.
const std::set<std::uint16_t> slot2_bridges{0x02EA,0x02F2,0x02FA};
// Continuations the original pushes itself. `site` is the instruction that
// loads the address into a register pair; the next instruction pushes that
// pair, or, when `push` is set, jumps there and its first instruction does.
struct Pushed { unsigned bank; std::uint16_t site, continuation; std::uint16_t push=0; };
constexpr Pushed pushed[]{
    {0,0x0A6D,0x0A8C},{0,0x0A85,0x0A8C},{0,0x0EEE,0x0EF5},
    {12,0x8416,0x841D},{12,0x8519,0x852C},
    {12,0x8723,0x8729,0x851C},              // jumps to 851C, whose first instruction pushes HL
};

std::string name(unsigned bank, std::uint16_t address) {
    std::ostringstream text;
    text<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(2)<<bank<<':'<<std::setw(4)<<address;
    return text.str();
}
// The length of an instruction that neither jumps, calls nor returns; 0 when
// this test does not know the opcode (extend it rather than guess).
unsigned plain_length(std::uint8_t op, std::uint8_t next) {
    if (op==0xED) return next==0x43 || next==0x4B || next==0x53 || next==0x5B || next==0x73 || next==0x7B ? 4 : 2;
    if (op==0xDD || op==0xFD) {
        if (next==0x21 || next==0x22 || next==0x2A || next==0x36 || next==0xCB) return 4;
        if ((next&0xC7)==0x46 || (next&0xF8)==0x70 || next==0x34 || next==0x35 || ((next&0xC7)==0x86 && next>=0x80)) return 3;
        return 2;
    }
    if (op==0x76) return 0;
    if (op>=0x40 && op<0xC0) return 1;
    switch (op) {
        case 0x01: case 0x11: case 0x21: case 0x31: case 0x22: case 0x2A: case 0x32: case 0x3A: return 3;
        case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
        case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        case 0xD3: case 0xDB: case 0xCB: return 2;
        default: break;
    }
    if (op<0x40) return 1;                       // the remaining one-byte loads, arithmetic and rotates
    switch (op) {
        case 0xC1: case 0xC5: case 0xD1: case 0xD5: case 0xD9: case 0xE1: case 0xE3: case 0xE5:
        case 0xEB: case 0xF1: case 0xF3: case 0xF5: case 0xF9: case 0xFB: return 1;
        default: return 0;
    }
}
// The bank a fixed-bank bridge maps into slot 2 before its jump at `jump`:
// its body, from the segment entry to the jump, must be straight-line loads
// and stores, the last store to FFFFh (the slot-2 register) writing a known
// constant. Anything else stops the test: extend it rather than guess.
unsigned slot2_written_before(const Memory::Rom& image, std::uint16_t entry, std::uint16_t jump, const std::string& from) {
    std::optional<std::uint16_t> hl;
    std::optional<std::uint8_t> a, slot2;
    auto word=[&](std::uint16_t at) { return static_cast<std::uint16_t>(image.at(at) | image.at(at+1u)<<8); };
    for (std::uint16_t pc=entry;pc!=jump;) {
        probe::check(pc<jump,"A bridge into slot 2 does not run straight to its jump"+from);
        const auto op=image.at(pc);
        if (op==0x21) { hl=word(pc+1); pc+=3; }                                   // LD HL,nn
        else if (op==0x3E) { a=image.at(pc+1); pc+=2; }                           // LD A,n
        else if (op==0x36) { if (hl==0xFFFF) slot2=image.at(pc+1); pc+=2; }       // LD (HL),n
        else if (op==0x32) { if (word(pc+1)==0xFFFF) slot2=a; pc+=3; }           // LD (nn),A
        else probe::check(false,"A bridge into slot 2 has an instruction this test does not follow"+from);
    }
    probe::check(slot2.has_value(),"A bridge into slot 2 does not write a constant bank to FFFFh before its jump"+from);
    return *slot2%32u;                                                            // a 512 KiB cartridge has 32 banks
}
}

int main(int argc,char** argv) {
    try {
        if (argc!=3) return 77;
        const auto rom=probe::load(argv[1],argv[2]); if (!rom) return 77;
        RoutineRegistry registry;
        probe::check(game::register_routines(registry,rom->sha1),"The ROM is not an identified game image");
        const auto& image=*rom->image;
        auto offset=[](unsigned bank, std::uint16_t address) -> std::size_t {
            return address<0x8000 ? address : bank*0x4000u+(address-0x8000u);
        };
        auto byte=[&](unsigned bank, std::uint16_t address) { return image.at(offset(bank,address)); };
        auto word=[&](unsigned bank, std::uint16_t address) {
            return static_cast<std::uint16_t>(byte(bank,address) | byte(bank,address+1)<<8);
        };
        // Whether `address` begins a segment when `slot2` is the bank mapped at
        // 8000h. Below 8000h the bank is fixed; a fixed bank in slot 2 (0 or 1)
        // means the caller knows of no paged bank there.
        auto begins_segment=[&](unsigned slot2, std::uint16_t address) {
            if (address>=0xC000) return false;
            if (address<0x8000) return registry.find({address<0x4000 ? 0u : 1u,address})!=nullptr;
            return slot2>=2 && registry.find({slot2,address})!=nullptr;
        };
        std::vector<std::string> missing;
        auto need=[&](unsigned bank, std::uint16_t address, const std::string& what) {
            if (!begins_segment(bank,address))
                missing.push_back(name(address<0x4000 ? 0 : address<0x8000 ? 1 : bank,address)+": "+what);
        };

        unsigned targets=0;
        for (const auto& table:tables) for (unsigned i=0;i<table.entries;++i, ++targets)
            need(table.bank,word(table.bank,static_cast<std::uint16_t>(table.address+2*i)),
                 "entry "+std::to_string(i)+" of the code table at "+name(table.bank,table.address));

        // 5. The pushed continuations: the load and the push are checked in the ROM.
        for (const auto& p:pushed) {
            const auto site=name(p.bank,p.site);
            const auto load=byte(p.bank,p.site);
            probe::check((load==0x01 || load==0x11 || load==0x21) && word(p.bank,static_cast<std::uint16_t>(p.site+1))==p.continuation,
                         "No longer loads its pushed continuation: "+site);
            const std::uint8_t push=static_cast<std::uint8_t>(load+0xC4);   // PUSH BC, DE or HL for LD BC, DE or HL
            const auto next=static_cast<std::uint16_t>(p.site+3);
            if (!p.push) probe::check(byte(p.bank,next)==push,"The pushed continuation's push moved: "+site);
            else probe::check(byte(p.bank,next)==0xC3 && word(p.bank,static_cast<std::uint16_t>(next+1))==p.push &&
                              byte(p.bank,p.push)==push,"The pushed continuation's push moved: "+site);
            need(p.bank,p.continuation,"the continuation pushed at "+site);
        }

        unsigned exits=0;
        std::set<std::uint16_t> bridges;
        std::set<std::pair<RoutineEntry,std::uint16_t>> through_register;
        for (const auto& [entry,routine]:registry.entries()) for (const auto& exit:routine.spec.exits) {
            const auto at=exit.address;
            const auto bank=at>=0x8000 ? entry.bank : at<0x4000 ? 0u : 1u;
            const auto from=" of the segment "+name(entry.bank,entry.address)+" (exit "+name(bank,at)+")";
            probe::check(at<0x8000 || entry.bank>=2,"An exit in a paged bank belongs to a fixed-bank segment:"+from);
            ++exits;
            if (exit.kind==RoutineExit::returned) continue;
            const auto op=byte(bank,at), next=byte(bank,static_cast<std::uint16_t>(at+1));
            if (exit.kind!=RoutineExit::core_final) {
                // 6. A slice stops inside LDIR or OTIR and resumes at it; the
                // instruction after it is where the whole block completes.
                const bool copy=exit.kind==RoutineExit::copy_repeat;
                probe::check(op==0xED && next==(copy ? 0xB0 : 0xB3),
                             std::string("A ")+(copy ? "block-copy" : "port-output")+" slice is not at that instruction"+from);
                need(bank,at,"the resumption of a slice"+from);
                need(bank,static_cast<std::uint16_t>(at+2),"the completion of a slice"+from);
                continue;
            }
            const auto operand=word(bank,static_cast<std::uint16_t>(at+1));
            const auto relative=static_cast<std::uint16_t>(at+2+static_cast<std::int8_t>(next));
            auto after=[&](unsigned length) { return static_cast<std::uint16_t>(at+length); };
            // 4. The bank in slot 2 when an absolute JP or CALL takes this exit.
            auto slot2_at_transfer=[&]() -> unsigned {
                if (entry.bank>=2 || operand<0x8000 || operand>=0xC000) return entry.bank;
                probe::check(slot2_bridges.contains(at),"A fixed-bank jump or call into slot 2 that is not a known bridge"+from);
                bridges.insert(at);
                return slot2_written_before(image,entry.address,at,from);
            };
            if (op==0xC3) need(slot2_at_transfer(),operand,"the jump target"+from);
            else if ((op&0xC7)==0xC2) { need(slot2_at_transfer(),operand,"the jump target"+from); need(bank,after(3),"the instruction after a conditional jump"+from); }
            else if (op==0xCD || (op&0xC7)==0xC4) { need(slot2_at_transfer(),operand,"the call target"+from); need(bank,after(3),"the return point of the call"+from); }
            else if (op==0x18) need(bank,relative,"the jump target"+from);
            else if (op==0x10 || op==0x20 || op==0x28 || op==0x30 || op==0x38) {
                need(bank,relative,"the jump target"+from); need(bank,after(2),"the instruction after a conditional jump"+from);
            }
            else if (op==0xC9 || (op==0xED && (next==0x45 || next==0x4D))) continue;
            else if ((op&0xC7)==0xC0) need(bank,after(1),"the instruction after a conditional return"+from);
            else if ((op&0xC7)==0xC7) { need(bank,op&0x38,"the restart target"+from); need(bank,after(1),"the return point of the restart"+from); }
            else if (op==0xE9 || ((op==0xDD || op==0xFD) && next==0xE9)) through_register.insert({entry,at});
            else {
                const auto length=plain_length(op,next);
                probe::check(length,"The static walk does not know the final instruction"+from);
                need(bank,after(length),"the instruction after the final one"+from);
            }
        }
        probe::check(bridges==slot2_bridges,"A known fixed-bank bridge into slot 2 is no longer a declared exit");
        probe::check(through_register==dispatchers,
            "The jumps through a register differ from the known dispatchers: list the new one's code tables in this test");

        if (!missing.empty()) {
            std::cerr<<missing.size()<<" places the game can reach begin no segment:\n";
            for (const auto& line:missing) std::cerr<<"  "<<line<<'\n';
            return 1;
        }
        std::cout<<registry.entries().size()<<" segments, "<<exits<<" declared exits, "<<targets
                 <<" code-table targets, "<<std::size(pushed)<<" pushed continuations and "<<bridges.size()
                 <<" slot-2 bridges (exact bank): every one continues in a segment.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
