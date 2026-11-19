// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 14 tooling (not a test): writes the registry of an identified ROM
// as TSV, one line per declared exit: entry, unit, registering source file,
// exact, single instruction, observed entry, entry guard, exit kind, exit
// address, and the exit's flags. Addresses only; no ROM data. Used by the
// private conversion sheets and the per-file census.
//   psmodern_registry_dump <rom> <out.tsv>
#include "game/routines.h"
#include "rom/sha1.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>

using namespace psmodern;
namespace {
std::string hex(unsigned value, int width) {
    std::ostringstream text; text<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(width)<<value; return text.str();
}
std::string name(RoutineEntry e) { return hex(e.bank,2)+":"+hex(e.address,4); }
const char* kind(RoutineExit k) {
    return k==RoutineExit::returned ? "returned" : k==RoutineExit::copy_repeat ? "copy_repeat" :
        k==RoutineExit::output_repeat ? "output_repeat" : "core_final";
}
}
int main(int argc, char** argv) {
    try {
        if (argc!=3) { std::cerr<<"usage: psmodern_registry_dump <rom> <out.tsv>\n"; return 2; }
        std::ifstream in(argv[1],std::ios::binary);
        const std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        RoutineRegistry registry;
        if (!game::register_routines(registry,sha1(image))) { std::cerr<<"not an identified game image\n"; return 1; }
        std::ofstream out(argv[2]);
        out<<"entry\tunit\torigin\texact\tsingle\tobserved\tguard\tkind\texit\tstack\tconditional\tafter\tflags\n";
        for (const auto& [entry,routine]:registry.entries()) {
            const auto& spec=routine.spec;
            auto row=[&](const SegmentExit* exit) {
                out<<name(entry)<<'\t'<<name(spec.unit)<<'\t'<<routine.origin<<'\t'<<routine.exact<<'\t'
                   <<spec.single_instruction<<'\t'<<spec.observe_entry<<'\t'
                   <<(routine.guard ? std::string(routine.guard->name) : std::string("-"))<<'\t';
                if (!exit) { out<<"any_return\t-\t0\t0\t0\t-\n"; return; }
                std::string flags;
                auto add=[&](bool on, const char* text) { if (on) flags+=(flags.empty() ? "" : ",")+std::string(text); };
                add(exit->interrupt_mask==InterruptMask::enable,"ei");
                add(exit->interrupt_mask==InterruptMask::disable,"di");
                add(exit->cartridge_control,"cartridge_control");
                add(exit->wram_seed.has_value(),"seed");
                add(exit->cartridge_flags.has_value(),"save_flag");
                add(exit->reset_stack,"reset_stack");
                add(exit->return_from_nmi,"retn");
                add(exit->native_frame_tail,"frame_tail");
                add(exit->window_bank_store,"window_bank_store");
                add(exit->sprite_outputs>0,"sprite_outputs");
                add(exit->mapper_immediate.has_value(),"mapper_immediate");
                out<<kind(exit->kind)<<'\t'<<hex(exit->address,4)<<'\t'<<static_cast<int>(exit->stack)<<'\t'
                   <<exit->conditional<<'\t'<<exit->after<<'\t'<<(flags.empty() ? "-" : flags)<<'\n';
            };
            if (spec.exits.empty()) row(nullptr);
            for (const auto& exit:spec.exits) row(&exit);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
