// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/cpu.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace psmodern;
namespace {
struct Bus : CpuBus {
    std::array<std::uint8_t,65536> mem{};
    bool protect_rom = false;
    bool woodmass = false;
    std::uint8_t opcode(std::uint16_t a) override { return woodmass && a==0x1601 ? 0xc9 : mem[a]; }
    std::uint8_t read(std::uint16_t a) override { return mem[a]; }
    void write(std::uint16_t a,std::uint8_t v) override { if (!protect_rom || a >= 0x4000) mem[a]=v; }
    std::uint8_t input(std::uint16_t p,std::uint64_t) override { return p&1 ? 0xff : 0xbf; }
    void output(std::uint16_t,std::uint8_t,std::uint64_t) override {}
};
std::vector<std::uint8_t> load(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read suite file");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void load_tap(Bus& bus,const std::vector<std::uint8_t>& data) {
    std::size_t pos=0; unsigned addr=0, count=0; bool header=false;
    while (pos+2<=data.size()) {
        unsigned len=data[pos] | (data[pos+1]<<8); pos+=2;
        if (pos+len>data.size() || len<2) throw std::runtime_error("Invalid TAP block");
        if (len==19 && data[pos]==0 && data[pos+1]==3) {
            count=data[pos+12] | (data[pos+13]<<8); addr=data[pos+14] | (data[pos+15]<<8); header=true;
        } else if (header && data[pos]==0xff) {
            if (count+2!=len || addr+count>65536) throw std::runtime_error("Invalid TAP code length");
            std::copy_n(data.begin()+pos+1,count,bus.mem.begin()+addr); return;
        }
        pos+=len;
    }
    throw std::runtime_error("No TAP code block");
}
void ret(Bus& bus,Z80& cpu) {
    auto sp=cpu.sp.uint16_value; cpu.pc.uint16_value=bus.mem[sp] | (bus.mem[(sp+1)&65535]<<8);
    cpu.sp.uint16_value+=2;
}
int run(const std::filesystem::path& path, const std::string& kind, const std::filesystem::path& firmware) {
    const bool cpm=kind=="cpm";
    const bool woody=kind=="wood-flags" || kind=="wood-memptr";
    Bus bus; Cpu wrapper(bus); auto& cpu=wrapper.raw(); auto data=load(path);
    if (woody) {
        bus.protect_rom=true; bus.woodmass=true;
        auto rom=load(firmware);
        if (rom.size()!=16384 || data.size()!=5573) throw std::runtime_error("Wrong Woodmass test or firmware size");
        std::copy(rom.begin(),rom.end(),bus.mem.begin());
        std::copy_n(data.begin()+120,5452,bus.mem.begin()+0x8000);
        cpu.pc.uint16_value=kind=="wood-flags" ? 0x8057 : 0x8049;
        cpu.sp.uint16_value=0x7fe8; cpu.af.uint16_value=0x3222; cpu.i=0x3f;
        bus.mem[0x10]=0xc3; bus.mem[0x11]=0xf2; bus.mem[0x12]=0x70;
        bus.mem[0x70f2]=0xc9; bus.mem[0x0d6b]=0xc9;
    } else if (cpm) {
        if (data.size()>0xff00) throw std::runtime_error("COM too large");
        std::copy(data.begin(),data.end(),bus.mem.begin()+0x100); cpu.pc.uint16_value=0x100;
        cpu.sp.uint16_value=0xfffe; bus.mem[0xfffe]=bus.mem[0xffff]=0;
    } else {
        bus.protect_rom=true; load_tap(bus,data); cpu.pc.uint16_value=0x8000; cpu.sp.uint16_value=0xffee;
        cpu.i=0x3f; bus.mem[0xffee]=bus.mem[0xffef]=0; bus.mem[0x10]=0xc9; bus.mem[0x1601]=0xc9;
    }
    std::string text; std::uint64_t cycles=0;
    while (cycles < 100000000000ULL) {
        const auto pc=cpu.pc.uint16_value;
        if ((woody ? pc==0x80e6 : pc==0) && cpu.resume==0) break;
        if (cpm && pc==5 && cpu.resume==0) {
            auto fn=cpu.bc.uint16_value&255;
            if (fn==2) text.push_back(cpu.de.uint16_value&255);
            if (fn==9) { auto p=cpu.de.uint16_value; for (unsigned i=0;i<65536 && bus.mem[p]!='$';++i,++p) text.push_back(bus.mem[p]); }
            ret(bus,cpu); continue;
        }
        if (!cpm && pc==(woody ? 0x70f2 : 0x10) && cpu.resume==0) text.push_back(cpu.af.uint16_value>>8);
        cycles+=wrapper.step(cycles);
    }
    std::cout << text << "\ncycles: " << cycles << '\n';
    if (woody) {
        std::size_t count=0, pos=0;
        while ((pos=text.find("passed",pos))!=std::string::npos) { ++count; pos+=6; }
        return cpu.pc.uint16_value==0x80e6 && text.find("All tests finished")!=std::string::npos &&
            text.find("failed")==std::string::npos && count==(kind=="wood-flags" ? 47u : 58u) ? 0 : 1;
    }
    if (cpm) {
        std::size_t count=0, pos=0;
        while ((pos=text.find("OK",pos))!=std::string::npos) { ++count; pos+=2; }
        return text.find("ERROR")==std::string::npos && text.find("Tests complete")!=std::string::npos && count==67 ? 0 : 1;
    }
    return text.find("all tests passed")!=std::string::npos && text.find("FAILED")==std::string::npos ? 0 : 1;
}
}
int main(int argc,char** argv) {
    if (argc!=3 && argc!=4) return 1;
    if (!*argv[1] || !std::filesystem::exists(argv[1])) { std::cout << "External Z80 test missing; skipping.\n"; return 77; }
    if (argc==4 && (!*argv[3] || !std::filesystem::exists(argv[3]))) { std::cout << "External ZX Spectrum firmware missing; skipping.\n"; return 77; }
    try { return run(argv[1],argv[2],argc==4 ? argv[3] : ""); }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
