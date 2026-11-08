// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include "rom/sha256.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
using namespace psmodern;

namespace {
unsigned symbol(const std::filesystem::path& file,const std::string& name) {
    std::ifstream in(file);
    for (std::string line;std::getline(in,line);) {
        auto space=line.find(' ');
        if (space==std::string::npos || line.substr(space+1)!=name) continue;
        auto colon=line.find(':');
        if (colon==std::string::npos) break;
        return std::stoul(line.substr(colon+1,space-colon-1),nullptr,16);
    }
    throw std::runtime_error("Missing VDPTest symbol: "+name);
}
}
int main(int argc,char** argv) {
    if (argc!=2) return 1;
    const std::filesystem::path dir(argv[1]);
    const bool have_rom=!dir.empty() && std::filesystem::exists(dir/"VDPTest.sms");
    const bool have_symbols=!dir.empty() && std::filesystem::exists(dir/"VDPTest.sym");
    try {
        // Both artifacts identify the build documented in dependencies.md.
        auto verify=[&](const char* name,const char* expected) {
            std::ifstream input(dir/name,std::ios::binary);
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
            if (!input || input.bad() || sha256(bytes)!=expected)
                throw std::runtime_error(std::string("Incorrect pinned VDPTest SHA-256: ")+name);
        };
        if (have_rom) verify("VDPTest.sms","8fbacb3f4c150453cdfa2a4c270682245d6b29040e770fbad9b28e0d54d4e563");
        if (have_symbols) verify("VDPTest.sym","5ea322a0a3132cd305916e7da48f646ba80fe0dd68fd73325ee29d984add0782");
        if (!have_rom || !have_symbols) {
            std::cout<<"VDPTest cache empty; skipping.\n";return 77;
        }
        const auto last_test=symbol(dir/"VDPTest.sym","RunTest60");
        const auto results_page=symbol(dir/"VDPTest.sym","WaitForButton");
        std::ifstream in(dir/"VDPTest.sms",std::ios::binary);
        auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(in),
                                               std::istreambuf_iterator<char>());
        if (rom->size()!=32768) throw std::runtime_error("Expected the pinned 32 KiB VDPTest ROM");
        rom->resize(512*1024,0);
        Machine machine(rom);
        bool reached_last=false, complete=false;
        unsigned completed_frame=0;
        for (unsigned frame=0;frame<600 && !complete;++frame) {
            machine.set_pad(0,Pad{.one=frame%12==4});
            const auto target=(static_cast<std::uint64_t>(frame)+1)*Machine::frame_cycles;
            while (machine.cycles()<target || machine.cpu().resume==Z80_RESUME_XY) {
                const auto pc=machine.cpu().pc.uint16_value;
                if (pc==last_test) reached_last=true;
                if (reached_last && pc==results_page) {
                    complete=true;completed_frame=frame;break;
                }
                machine.step();
            }
            machine.take_psg_writes(); machine.take_samples();
        }
        if (!complete) throw std::runtime_error("VDPTest did not reach the sprite results page");
        unsigned passed=0,failed=0;
        for (unsigned test=1;test<=60;++test) {
            if (test==20 || (test>=37 && test<=40)) continue;
            const auto value=machine.memory().read(0xc8ff+test);
            const bool okay=value==(test==21?2:0);
            std::cout<<"VDPTest "<<test<<" "<<(okay?"OK":"ERROR")<<" ("
                     <<static_cast<unsigned>(value)<<")\n";
            okay?++passed:++failed;
        }
        std::cout<<"VDPTest frame "<<completed_frame<<": "<<passed<<" passed, "<<failed<<" failed.\n";
        // All 55 automated SMS1 results must pass.
        return (failed || passed!=55)?1:0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
