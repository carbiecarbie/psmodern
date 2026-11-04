// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom/rom.h"
#include "machine/machine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void frame(psmodern::Machine& m,unsigned n,bool& wrote_r0,
           unsigned& total,unsigned& first_irq,unsigned& bad_frames) {
    const auto target=static_cast<std::uint64_t>(n)*psmodern::Machine::frame_cycles;
    const auto event=target-psmodern::Machine::frame_cycles+43986;
    unsigned after_event=0;
    bool enabled_at_event=false;
    while (m.cycles()<target || m.cpu().resume==Z80_RESUME_XY) {
        const auto start=m.cycles();
        const auto before=m.cpu().pc.uint16_value;
        const auto elapsed=m.step();
        if (start<=event && m.cycles()>event)
            enabled_at_event=m.cpu().iff1 && (m.vdp().registers()[1]&0x20) && m.vdp().irq();
        if (!wrote_r0 && m.vdp().registers()[0]!=0x36) {
            wrote_r0=true;
            check(total==0,"IRQ accepted before first R0 write");
        }
        if (m.cpu().pc.uint16_value==0x38 && elapsed==13 && before!=0x38) {
            check(wrote_r0,"IRQ accepted before first R0 write");
            if (m.cycles()>event) ++after_event;
            ++total;
            if (!first_irq) first_irq=n;
        }
    }
    if (enabled_at_event && after_event!=1) ++bad_frames;
}
}
int main(int argc,char** argv) {
    if (argc!=3) return 1;
    if (*argv[1]=='\0' || !std::filesystem::exists(argv[1])) { std::cout << "ROM path missing; skipping.\n"; return 77; }
    try {
        auto expected=std::string_view(argv[2])=="us" ? psmodern::RomVersion::us_rev_a : psmodern::RomVersion::brazilian;
        if (psmodern::identify_rom(argv[1])!=expected) throw std::runtime_error("Wrong ROM version.");
        std::ifstream f(argv[1],std::ios::binary);
        auto rom=std::make_shared<psmodern::Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
        psmodern::Machine first(rom), second(rom);
        bool wrote=false; unsigned total=0,first_irq=0,bad=0;
        for (unsigned n=1;n<=600;++n) frame(first,n,wrote,total,first_irq,bad);
        auto fork=first;
        for (unsigned n=601;n<=1200;++n) frame(first,n,wrote,total,first_irq,bad);
        bool fork_wrote=true; unsigned fork_total=0,fork_first=0,fork_bad=0;
        for (unsigned n=601;n<=1200;++n) frame(fork,n,fork_wrote,fork_total,fork_first,fork_bad);
        check(first.equivalent(fork),"Copy at frame 600 differs at 1200");
        bool second_wrote=false; unsigned second_total=0,second_first=0,second_bad=0;
        for (unsigned n=1;n<=1200;++n) frame(second,n,second_wrote,second_total,second_first,second_bad);
        check(first.equivalent(second),"Second run differs at 1200");
        check(first_irq!=0,"No IRQ accepted");
        check(bad==0,"A frame with IRQ enabled did not accept exactly one new IRQ");
        bool vram_written=false; for (auto v:first.vdp().vram()) vram_written|=v!=0;
        check(vram_written,"VRAM unchanged");
        std::cout << "Headless: 1200 frames twice and copy from 600; first IRQ frame " << first_irq << ", total " << total << ".\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
