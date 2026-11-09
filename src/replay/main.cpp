// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/recording.h"
#include "replay/verification.h"
#include "rom/sha1.h"
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
    if(argc<7)throw std::runtime_error("Usage: psmodern_replay <rom> <recording> --dump <frames|ranges|all> --out <new-folder> [--cart-ram <file>] [--no-routines] [--lockstep-calls] [--compare-recording]");
    if(std::string_view(argv[3])!="--dump"||std::string_view(argv[5])!="--out")throw std::runtime_error("Invalid replay arguments.");
    RoutineRegistry routines;
    std::optional<std::filesystem::path> cart_path;
    bool lockstep_calls=false, compare_recording=false;
    for(int i=7;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(arg=="--no-routines" && routines.enabled) routines.enabled=false;
        else if(arg=="--lockstep-calls" && !lockstep_calls) lockstep_calls=true;
        else if(arg=="--compare-recording" && !compare_recording) compare_recording=true;
        else if(arg=="--cart-ram" && !cart_path && i+1<argc) cart_path=argv[++i];
        else throw std::runtime_error("Invalid replay arguments.");
    }
    if(compare_recording && !routines.enabled) throw std::runtime_error("--compare-recording requires routines enabled.");
    const auto recording=replay::read(argv[2]);const auto selected=dumps(argv[4],recording.frames);
    std::ifstream rom_file(argv[1],std::ios::binary);
    if(!rom_file)throw std::runtime_error("Cannot open ROM.");
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(rom_file),std::istreambuf_iterator<char>());
    if(rom_file.bad()||rom->size()!=512*1024||sha1(*rom)!=recording.rom_sha1)throw std::runtime_error("ROM SHA-1 mismatch or invalid ROM size.");
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
    replay::CallVerifier verifier(std::filesystem::path(argv[2]).filename().string());
    if(lockstep_calls) routines.verifier=&verifier;
    std::optional<Machine> original;
    if(compare_recording) original.emplace(rom,cart);
    replay::FrameWrites original_frame_writes;
    std::ofstream manifest(out/"manifest.txt",std::ios::binary),log(out/"psg-writes.txt",std::ios::binary);
    if(!manifest||!log)throw std::runtime_error("Cannot open replay outputs.");
    manifest<<"frame picture ram vram cram cart-ram cpu psg\n";
    std::size_t change=0;FrameInput input{}; replay::FrameWrites frame_writes;
    for(std::uint64_t n=0;n<recording.frames;++n) {
        if(change<recording.changes.size()&&recording.changes[change].frame==n)input=recording.changes[change++].input;
        machine.run_frame(input,routines);
        auto writes=machine.take_psg_writes();auto samples=machine.take_samples();(void)samples;
        auto psg=frame_writes.take(n+1,writes); auto cpu=replay::cpu_bytes(machine.cpu());
        for(const auto& w:writes)log<<w.cycle<<' '<<hexbyte(w.byte)<<'\n';
        const auto& picture=machine.picture();const auto& ram=machine.memory().ram();
        const auto& vram=machine.vdp().vram();const auto& cram=machine.vdp().cram();
        const auto& cart_ram=machine.memory().cartridge_ram();
        const auto components=replay::manifest(machine,psg);
        if(original) {
            original->run_frame(input);
            auto original_writes=original->take_psg_writes();
            original->take_samples();
            replay::compare_manifests(std::filesystem::path(argv[2]).filename().string(),n+1,
                replay::manifest(*original,original_frame_writes.take(n+1,std::move(original_writes))),components);
        }
        manifest<<n+1;
        for(const auto& component:components) manifest<<' '<<component;
        manifest<<'\n';
        if(selected.contains(n+1)) {
            std::ostringstream prefix;prefix<<"frame-"<<std::setw(6)<<std::setfill('0')<<n+1<<'.';
            binary(out/(prefix.str()+"picture.bin"),picture);binary(out/(prefix.str()+"ram.bin"),ram);
            binary(out/(prefix.str()+"vram.bin"),vram);binary(out/(prefix.str()+"cram.bin"),cram);
            binary(out/(prefix.str()+"cart-ram.bin"),cart_ram);
            binary(out/(prefix.str()+"cpu.bin"),cpu); binary(out/(prefix.str()+"psg.bin"),psg);
        }
    }
    manifest.close();log.close();if(!manifest||!log)throw std::runtime_error("Cannot close replay outputs.");
    std::cout<<"Replayed "<<recording.frames<<" frames into "<<out.string()<<'\n';return 0;
}
}
int main(int argc,char** argv) {try{return run(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
