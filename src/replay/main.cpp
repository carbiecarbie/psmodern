// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/option_flags.h"
#include "replay/recording.h"
#include "replay/verification.h"
#include "rom/sha1.h"
#include "game/routines.h"
#include "replay/census.h"
#include "oracle/lockstep.h"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace psmodern;
std::uint64_t frame_number(std::string_view s) {
    std::uint64_t n=0;auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(s.empty()||s[0]=='0'||ec!=std::errc{}||end!=s.data()+s.size())throw std::runtime_error("Invalid --dump frame number.");
    return n;
}
std::set<std::uint64_t> dumps(std::string_view spec,std::uint64_t total) {
    std::set<std::uint64_t> result;
    if(spec=="all") {for(std::uint64_t n=0;n<total;++n)result.insert(n+1);return result;}
    if(spec.empty())throw std::runtime_error("Empty --dump selection.");
    while(!spec.empty()) {
        auto comma=spec.find(',');auto item=spec.substr(0,comma);auto dash=item.find('-');
        auto first=frame_number(item.substr(0,dash));auto last=dash==std::string_view::npos?first:frame_number(item.substr(dash+1));
        if(first>last||last>total)throw std::runtime_error("--dump frame outside recording or reversed range.");
        for(auto n=first;;++n){result.insert(n);if(n==last)break;}
        if(comma==std::string_view::npos)break;spec=spec.substr(comma+1);
        if(spec.empty())throw std::runtime_error("Trailing comma in --dump.");
    }
    return result;
}
template<class Bytes>void binary(const std::filesystem::path& path,const Bytes& data) {
    std::ofstream f(path,std::ios::binary);if(!f||!f.write(reinterpret_cast<const char*>(data.data()),data.size()))
        throw std::runtime_error("Cannot write "+path.string());
    f.close();if(!f)throw std::runtime_error("Cannot close "+path.string());
}
std::string hexbyte(std::uint8_t b) {std::ostringstream s;s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);return s.str();}
int run(int argc,char** argv) {
    if(argc<7)throw std::runtime_error("Usage: psmodern_replay <rom> <recording> --dump <frames|ranges|all> --out <new-folder> [--cart-ram <file>] [--no-routines] [--lockstep] [--lockstep-strict] [--compare-recording] [--census <file>] "+game::OptionFlags::usage()+"\n--no-routines runs the original: the oracle machine on the pinned core, with no rewritten routine. --lockstep runs the oracle beside the game and compares them at every top-level segment boundary and every frame; --lockstep-strict at every instruction boundary (--lockstep-calls is the old name of --lockstep and is still accepted). --census writes the engine census (calls per exit, refusals, deliveries, maximum step and overshoot).\nThe rate flags change the game, so they cannot be combined with --no-routines, --lockstep or --compare-recording; a rate flag run writes manifests and the census without comparing to references.");
    if(std::string_view(argv[3])!="--dump"||std::string_view(argv[5])!="--out")throw std::runtime_error("Invalid replay arguments.");
    RoutineRegistry routines;
    std::optional<std::filesystem::path> cart_path;
    bool lockstep_calls=false, lockstep_strict=false, compare_recording=false, original_only=false;
    std::optional<std::filesystem::path> census_path;
    game::OptionFlags flags;
    for(int i=7;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(flags.take(argc,argv,i)) continue;
        if(arg=="--no-routines" && !original_only) original_only=true;
        else if((arg=="--lockstep-calls" || arg=="--lockstep") && !lockstep_calls) lockstep_calls=true;
        else if(arg=="--lockstep-strict" && !lockstep_strict) lockstep_strict=true;
        else if(arg=="--census" && !census_path && i+1<argc) census_path=argv[++i];
        else if(arg=="--compare-recording" && !compare_recording) compare_recording=true;
        else if(arg=="--cart-ram" && !cart_path && i+1<argc) cart_path=argv[++i];
        else throw std::runtime_error("Invalid replay arguments.");
    }
    if(original_only) flags.refuse_with("--no-routines","the options are carried out by the rewritten routines");
    if(lockstep_strict) lockstep_calls=true;
    if(lockstep_calls) flags.refuse_with("--lockstep","the game differs from the original by design, so calls cannot be compared");
    if(compare_recording) flags.refuse_with("--compare-recording","the game differs from the original by design, so it cannot match the recording");
    if(compare_recording && original_only) throw std::runtime_error("--compare-recording requires routines enabled.");
    // Review R3 L4: the original run has no rewritten code to compare or count.
    if(lockstep_calls && original_only) throw std::runtime_error("--lockstep requires routines enabled.");
    if(census_path && original_only) throw std::runtime_error("--census requires routines enabled.");
    const auto recording=replay::read(argv[2]);const auto selected=dumps(argv[4],recording.frames);
    std::ifstream rom_file(argv[1],std::ios::binary);
    if(!rom_file)throw std::runtime_error("Cannot open ROM.");
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(rom_file),std::istreambuf_iterator<char>());
    if(rom_file.bad()||rom->size()!=512*1024||sha1(*rom)!=recording.rom_sha1)throw std::runtime_error("ROM SHA-1 mismatch or invalid ROM size.");
    game::register_routines(routines,recording.rom_sha1,flags.options);
    Memory::CartridgeRam cart{};
    if(cart_path) {
        std::ifstream f(*cart_path,std::ios::binary);
        if(!f||!f.read(reinterpret_cast<char*>(cart.data()),cart.size())||f.peek()!=std::char_traits<char>::eof())
            throw std::runtime_error("Cartridge RAM image must be exactly 8192 bytes.");
    }
    if(sha1(cart)!=recording.cart_ram_sha1)throw std::runtime_error("Initial cartridge RAM SHA-1 mismatch; provide --cart-ram if nonzero.");
    const std::filesystem::path out=argv[6];
    if(std::filesystem::exists(out))throw std::runtime_error("Output folder already exists: "+out.string());
    if(!std::filesystem::create_directories(out))throw std::runtime_error("Cannot create output folder.");
    Machine machine(rom,cart);
    // A ROM the game does not identify registers no segment: the runner (a
    // test tool, never shipped) then runs it on the oracle's core through the
    // engine, as the tools' tests do with their synthetic ROMs.
    std::optional<oracle::Scaffold> scaffold;
    if(routines.empty()) scaffold.emplace(oracle::Scaffold::originals);
    // --no-routines: the original, the oracle's native machine (since
    // milestone 14 the engine itself no longer runs the core in the program).
    std::optional<oracle::NativeMachine> native_run;
    if(original_only) native_run.emplace(rom,cart);
    const auto label=std::filesystem::path(argv[2]).filename().string();
    std::optional<oracle::Lockstep> lockstep;
    if(lockstep_calls) lockstep.emplace(machine,label,lockstep_strict ? oracle::Mode::instructions : oracle::Mode::segments);
    // --compare-recording: the oracle's own frames against the game's
    // manifests (a lockstep run compares the whole machines anyway).
    std::optional<oracle::NativeMachine> original;
    if(compare_recording) original.emplace(rom,cart);
    replay::FrameWrites original_frame_writes;
    std::ofstream manifest(out/"manifest.txt",std::ios::binary),log(out/"psg-writes.txt",std::ios::binary);
    if(!manifest||!log)throw std::runtime_error("Cannot open replay outputs.");
    manifest<<"frame picture ram vram cram cart-ram cpu psg\n";
    std::size_t change=0;FrameInput input{}; replay::FrameWrites frame_writes;
    for(std::uint64_t n=0;n<recording.frames;++n) {
        if(change<recording.changes.size()&&recording.changes[change].frame==n)input=recording.changes[change++].input;
        // Everything is taken at the frame observer: the first instruction
        // boundary at or after the frame end, where the reference was sampled.
        bool sampled=false;
        auto at_frame=[&](const Console& m,const std::vector<PsgWrite>& writes) {
            sampled=true;
            auto psg=frame_writes.take(n+1,writes); auto cpu=replay::cpu_bytes(m.cpu());
            for(const auto& w:writes)log<<w.cycle<<' '<<hexbyte(w.byte)<<'\n';
            const auto components=replay::manifest(m,psg);
            if(original) {
                original->run_frame(input);
                auto original_writes=original->take_psg_writes();
                original->take_samples();
                replay::compare_manifests(label,n+1,
                    replay::manifest(*original,original_frame_writes.take(n+1,std::move(original_writes))),components);
            }
            manifest<<n+1;
            for(const auto& component:components) manifest<<' '<<component;
            manifest<<'\n';
            if(selected.contains(n+1)) {
                std::ostringstream prefix;prefix<<"frame-"<<std::setw(6)<<std::setfill('0')<<n+1<<'.';
                binary(out/(prefix.str()+"picture.bin"),m.picture());binary(out/(prefix.str()+"ram.bin"),m.memory().ram());
                binary(out/(prefix.str()+"vram.bin"),m.vdp().vram());binary(out/(prefix.str()+"cram.bin"),m.vdp().cram());
                binary(out/(prefix.str()+"cart-ram.bin"),m.memory().cartridge_ram());
                binary(out/(prefix.str()+"cpu.bin"),cpu); binary(out/(prefix.str()+"psg.bin"),psg);
            }
        };
        if(lockstep) lockstep->run_frame(machine,input,&routines,[&](const Machine& m,oracle::FrameSample& sample) {
            at_frame(m,sample.writes);
        });
        else if(native_run) {
            native_run->run_frame(input);
            const auto writes=native_run->take_psg_writes(); (void)native_run->take_samples();
            at_frame(*native_run,writes);
        } else machine.run_frame(input,routines,{},{},[&] {
            const auto writes=machine.take_psg_writes(); (void)machine.take_samples();
            at_frame(machine,writes);
        });
        if(!sampled)throw std::logic_error("The frame observer did not run.");
    }
    manifest.close();log.close();if(!manifest||!log)throw std::runtime_error("Cannot close replay outputs.");
    // The original run (--no-routines) ran no segment: it writes no census.
    if(!original_only) {
        std::ofstream census(out/"routine-census.tsv",std::ios::binary);
        replay::write_census(census,routines);
        census.close();if(!census)throw std::runtime_error("Cannot write routine census.");
    }
    if(census_path) {
        std::ofstream transition(*census_path,std::ios::binary);
        replay::write_transition_census(transition,routines);
        transition.close();if(!transition)throw std::runtime_error("Cannot write the transition census.");
    }
    std::cout<<"Replayed "<<recording.frames<<" frames into "<<out.string()<<'\n';return 0;
}
}
int main(int argc,char** argv) {try{return run(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
