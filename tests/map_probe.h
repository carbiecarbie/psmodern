// SPDX-License-Identifier: GPL-3.0-or-later
// Probes for the milestone 11 map routines, on top of tests/probe.h.
//
// Command line of every map ROM test:
//   <rom> <us|br> [<recordings folder> [<reference folder>]]
// A missing ROM makes load() return nothing (the test exits 77). The
// recordings folder is the repository's recordings/<version>; the reference
// folder is PSMODERN_REFERENCE_DIR, which holds the start image
// (cart-ram.bin) of each recording that begins from a saved game. A test that
// needs a start image and has none skips that part and says so; it never
// fails for a missing external folder.
//
// Three ways to reach a map entry state:
//   1. State probes from a power-on machine: probe::Harness::prepare (probe.h)
//      with the RAM the path reads poked in, then Harness::run_cpp, which
//      dispatches once with the per-call verifier.
//   2. State probes from a real map frame: Scenario plays a recording (or a
//      constructed start image) to a map frame; prepare_at() copies that
//      machine, stops it at an entry with a setup, and Harness::run_cpp
//      dispatches it the same way.
//   3. Scenario probes: Scenario runs frames with routines on (every C++ call
//      checked by the per-call verifier) and a routines-off copy, compares the
//      two machines' manifests every frame, and drives scripted input; poke()
//      edits both copies between frames.
//
// Constructed start images. The game saves the block C300h-C6FFh as a raw
// copy into cartridge RAM (slot k at k x 400h, used flag at 200h + k, no
// checksum; 00:1D17 and 00:06AC). edited_start_image() takes a game-made
// image (a recording's cart-ram.bin from the reference folder) and edits
// saved fields by their work-RAM address, for example the transport byte
// C30Eh. external_start_image(name) reads <name>.bin from the folder named by
// the environment variable PSMODERN_MAP_START_IMAGES, for images made outside
// the repository; without the variable or the file it returns nothing.
#pragma once
#include "game/map.h"
#include "probe.h"
#include "replay/checkpoint.h"
#include "replay/recording.h"
#include "replay/verification.h"
#include "rom/sha1.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace psmodern::map_probe {
using probe::check;
namespace map=game::map;

struct Inputs {
    probe::Rom rom;
    std::string version;                       // us or br
    std::filesystem::path recordings, reference;   // empty when not given
    std::filesystem::path recording(const std::string& stem) const { return recordings/(stem+".psrec"); }
};
// Parses the command line above (argv[1] onwards). Throws on a malformed
// line or a wrong ROM; returns nothing when the ROM is absent.
inline std::optional<Inputs> load(int argc, char** argv) {
    check(argc>=3 && argc<=5,"Expected <rom> <us|br> [<recordings folder> [<reference folder>]].");
    auto rom=probe::load(argv[1],argv[2]);
    if (!rom) return std::nullopt;
    Inputs inputs{std::move(*rom),argv[2],argc>3 ? argv[3] : "",argc>4 ? argv[4] : ""};
    return inputs;
}

// ---- Start images ------------------------------------------------------------------
using StartImage=Memory::CartridgeRam;
inline std::optional<StartImage> read_image(const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) return std::nullopt;
    std::ifstream file(path,std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    check(bytes.size()==StartImage{}.size(),"start image "+path.string()+" has the wrong size");
    StartImage image{};
    std::copy(bytes.begin(),bytes.end(),reinterpret_cast<char*>(image.data()));
    return image;
}
// The start image of a repository recording: an empty cartridge when the
// recording starts from one, else <reference>/<version>/<stem>/cart-ram.bin,
// checked against the recording's digest. Nothing when it is not available.
inline std::optional<StartImage> start_image(const Inputs& inputs, const std::string& stem, const replay::Recording& recording) {
    StartImage empty{};
    if (sha1(empty)==recording.cart_ram_sha1) return empty;
    if (inputs.reference.empty()) return std::nullopt;
    auto image=read_image(inputs.reference/inputs.version/stem/"cart-ram.bin");
    if (image) check(sha1(*image)==recording.cart_ram_sha1,"start image of "+stem+" does not match its recording");
    return image;
}
// Saved fields edited in one slot of a game-made image: {work-RAM address
// C300h-C6FFh, value}. The slot must be in use in the image.
inline StartImage edited_start_image(StartImage image, unsigned slot, std::initializer_list<std::pair<std::uint16_t,std::uint8_t>> fields) {
    check(slot>=1 && slot<=5,"save slots are 1-5");
    check(image.at(0x200+slot)!=0,"the edited save slot is not in use");
    for (const auto& [address,value]:fields) {
        check(address>=0xC300 && address<0xC700,"an edited saved field lies outside C300h-C6FFh");
        image.at(slot*0x400u+(address-0xC300u))=value;
    }
    return image;
}
inline std::optional<StartImage> external_start_image(const std::string& name) {
    const char* folder=std::getenv("PSMODERN_MAP_START_IMAGES");
    if (!folder || !*folder) return std::nullopt;
    return read_image(std::filesystem::path(folder)/(name+".bin"));
}

// The machine dispatch's own stack test (src/machine/machine.cpp), predicted
// from a machine's stack pointer and a segment's stack permissions, to decide
// whether a probe must run in C++ or fall back.
inline bool admitted(const Machine& m, const SegmentSpec& spec) {
    const auto sp=m.cpu().sp.uint16_value;
    auto unmapped=[&](std::uint16_t address) { return m.memory().mapping(address).kind==Memory::Kind::unmapped; };
    auto byte=[&](unsigned offset) {
        const auto address=static_cast<std::uint16_t>(sp+offset);
        return unmapped(address) || (spec.scratch_stack && address>=0xfffc);
    };
    bool bad=byte(0) || byte(1);
    for (unsigned i=1;i<=spec.scratch_stack;++i) bad=bad || byte(0x10000-i);
    if (spec.inherited_stack)
        for (unsigned i=0;i<spec.inherited_stack+2;++i) {
            const auto address=static_cast<std::uint16_t>(sp+i);
            bad=bad || unmapped(address) || address>=0xfffc;
        }
    return !bad;
}

// ---- Scenarios ----------------------------------------------------------------------
// Routines on (verified call by call) beside a routines-off copy, compared
// every frame on the seven manifest components.
class Scenario {
public:
    Scenario(const probe::Rom& rom, const StartImage& image, std::string name, const game::Options& options = {})
        : name_(std::move(name)), on_(rom.image,image), off_(rom.image,image), verifier_(name_) {
        check(game::register_routines(routines_,rom.sha1,options),"identified ROM registry");
        check(game::register_routines(disabled_,rom.sha1,options),"identified ROM registry");
        routines_.verifier=&verifier_;
        disabled_.enabled=false;
    }
    Scenario(const Scenario&)=delete;
    Scenario& operator=(const Scenario&)=delete;

    void frame(const FrameInput& input) {
        ++frame_;
        on_.run_frame(input,routines_);
        const auto psg=on_writes_.take(frame_,on_.take_psg_writes()); on_.take_samples();
        off_.run_frame(input,disabled_);
        const auto native=off_writes_.take(frame_,off_.take_psg_writes()); off_.take_samples();
        replay::compare_manifests(name_,frame_,replay::manifest(off_,native),replay::manifest(on_,psg));
    }
    void frames(const FrameInput& input, unsigned count) { for (unsigned i=0;i<count;++i) frame(input); }
    // Plays the recording's inputs from its frame `from` until `stop` holds
    // after a frame (true) or the recording ends (false). Returns the next
    // recording frame through `from`.
    bool play(const replay::Recording& recording, std::uint64_t& from, const std::function<bool(const Machine&)>& stop) {
        for (;from<recording.frames;) {
            frame(recording.input_at(from++));
            if (stop(on_)) return true;
        }
        return false;
    }
    // Edits work RAM in both copies between frames.
    void poke(std::uint16_t address, std::uint8_t value) {
        on_.memory().write(address,value); off_.memory().write(address,value);
    }
    void poke_word(std::uint16_t address, std::uint16_t value) {
        poke(address,static_cast<std::uint8_t>(value)); poke(static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
    }
    std::uint8_t peek(std::uint16_t address) const { return on_.peek(address); }
    const Machine& machine() const { return on_; }
    std::uint64_t frame_count() const { return frame_; }
    RoutineRegistry& routines() { return routines_; }
    // The counters of a registered entry (zero when it is not registered).
    RoutineCounters counters(RoutineEntry entry) const {
        const auto* routine=routines_.find(entry);
        return routine ? routine->counters : RoutineCounters{};
    }
    // C++ and fallback calls summed over every registered entry of a part.
    std::pair<std::uint64_t,std::uint64_t> calls(map::Part part) const {
        std::uint64_t cpp=0, fallbacks=0;
        for (const auto& [entry,routine]:routines_.entries())
            for (const auto& range:map::part_ranges(part))
                if (entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last) {
                    cpp+=routine.counters.cpp_calls; fallbacks+=routine.counters.fallback_calls;
                }
        return {cpp,fallbacks};
    }
private:
    std::string name_;
    Machine on_, off_;
    RoutineRegistry routines_, disabled_;
    replay::CallVerifier verifier_;
    replay::FrameWrites on_writes_, off_writes_;
    std::uint64_t frame_=0;
};
// The mode byte says the party walks on the map or in a town.
inline bool on_map(const Machine& machine) { return machine.peek(map::game_mode)==map::map_mode; }

// ---- State probes from a real frame ----------------------------------------------------
// A copy of `base` (a machine at a frame end, for example Scenario::machine())
// stopped at `entry` with the setup's banks, stack, interrupt flags and
// refresh counter, and its unrelated registers randomized from the seed. Work
// RAM, VRAM and every other device keep the base's state, so the probe sees a
// real map; poke the inputs the path reads afterwards.
inline Machine prepare_at(const Machine& base, RoutineEntry entry, const probe::Setup& setup) {
    Machine machine=base;
    auto& memory=machine.memory();
    // Work RAM must be readable while the setup writes the stack.
    memory.set_memory_control(memory.memory_control()&~0x10);
    auto banks=setup.banks;
    if (entry.address>=0x0400) banks.at(entry.address>>14)=static_cast<std::uint8_t>(entry.bank);
    for (unsigned slot=0;slot<3;++slot) memory.write(static_cast<std::uint16_t>(0xfffd+slot),banks[slot]);
    memory.write(0xfffc,setup.cartridge_ram ? 0x08 : 0x00);
    auto& cpu=machine.cpu();
    probe::randomize(cpu,setup.seed);
    cpu.pc.uint16_value=entry.address;
    cpu.sp.uint16_value=setup.sp;
    cpu.iff1=setup.iff1; cpu.iff2=setup.iff2;
    cpu.r=setup.r;
    cpu.halt_line=0;
    if (setup.sp<0xfffc-1) {
        memory.write(setup.sp,static_cast<std::uint8_t>(setup.return_address));
        memory.write(static_cast<std::uint16_t>(setup.sp+1),static_cast<std::uint8_t>(setup.return_address>>8));
    }
    if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
    return machine;
}

// ---- Catalog checks shared by the part tests -----------------------------------------------
// Registers one part alone and checks it against its planned list: every
// registered entry is planned or lies in the part's ranges with a unit of the
// part, and the returned list says which planned entries are registered.
inline std::vector<RoutineEntry> registered_entries(map::Part part, void (*registration)(RoutineRegistry&)) {
    RoutineRegistry registry;
    registration(registry);
    const auto planned=map::planned_segments(part);
    std::vector<RoutineEntry> entries;
    for (const auto& [entry,routine]:registry.entries()) {
        bool listed=false, inside=false, own_unit=false;
        for (const auto& segment:planned) {
            listed=listed || (segment.entry==entry && segment.unit==routine.spec.unit);
            own_unit=own_unit || segment.unit==routine.spec.unit;
        }
        for (const auto& range:map::part_ranges(part))
            inside=inside || (entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last);
        check(listed || (inside && own_unit),std::string("map part ")+map::part_names[static_cast<unsigned>(part)]+
              " registers an entry outside its plan, ranges or units");
        entries.push_back(entry);
    }
    return entries;
}
}
