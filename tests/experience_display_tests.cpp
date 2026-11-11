// SPDX-License-Identifier: GPL-3.0-or-later
// A reward of five digits through the original text code, in each version.
// The recording of the deep dungeon contains a completed award. The test finds
// the frame in which the award starts (00:170D), replays the recording to the
// start of that frame, replaces the stored reward by a chosen value, lets the
// game run natively for a few hundred frames (the award text is drawn by the
// original text code) and checks that the run completes without a guard
// tripping and that every character gained what the message shows. Each
// version is run twice: with the experience rate at 400% from a stored
// reward of 12,500 (it shows 50,000), and in original mode from a stored
// reward of five digits. The recorded input belongs to another game state, so
// after the message is on screen the test presses the first button regularly
// to dismiss the text. With an output folder it also writes the picture of
// chosen frames as PNG files. Arguments: <rom> <us|br> <recording>
// <cartridge-ram image> [output folder]. Exit 77 when an input is missing.
#include "probe.h"
#include "replay/recording.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>

using namespace psmodern;
using probe::check;
namespace {
std::uint32_t crc32(const std::vector<std::uint8_t>& bytes, std::uint32_t crc=0) {
    crc=~crc;
    for (const auto byte:bytes) {
        crc^=byte;
        for (int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    return ~crc;
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift=24;shift>=0;shift-=8) out.push_back(static_cast<std::uint8_t>(value>>shift));
}
void chunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
    put32(png,static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type,type+4);
    body.insert(body.end(),data.begin(),data.end());
    png.insert(png.end(),body.begin(),body.end());
    put32(png,crc32(body));
}
// An uncompressed PNG of the picture, each pixel scaled to a 2x2 block. The
// picture holds the six-bit colors of the console (two bits per channel).
void write_png(const std::filesystem::path& path, const Vdp::Picture& picture) {
    constexpr unsigned width=256, height=192, scale=2;
    std::vector<std::uint8_t> raw;
    for (unsigned y=0;y<height*scale;++y) {
        raw.push_back(0);
        for (unsigned x=0;x<width*scale;++x) {
            const auto color=picture[(y/scale)*width+x/scale];
            for (const auto channel:{color&3,(color>>2)&3,(color>>4)&3}) raw.push_back(static_cast<std::uint8_t>(channel*85));
        }
    }
    std::vector<std::uint8_t> zlib{0x78,0x01};
    for (std::size_t at=0;at<raw.size();at+=65535) {
        const auto size=std::min<std::size_t>(65535,raw.size()-at);
        zlib.push_back(at+size>=raw.size() ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(size)); zlib.push_back(static_cast<std::uint8_t>(size>>8));
        zlib.push_back(static_cast<std::uint8_t>(~size)); zlib.push_back(static_cast<std::uint8_t>(~size>>8));
        zlib.insert(zlib.end(),raw.begin()+static_cast<std::ptrdiff_t>(at),raw.begin()+static_cast<std::ptrdiff_t>(at+size));
    }
    std::uint32_t a=1, b=0;
    for (const auto byte:raw) { a=(a+byte)%65521; b=(b+a)%65521; }
    put32(zlib,(b<<16)|a);
    std::vector<std::uint8_t> png{0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a}, header;
    put32(header,width*scale); put32(header,height*scale);
    header.insert(header.end(),{8,2,0,0,0});
    chunk(png,"IHDR",header); chunk(png,"IDAT",zlib); chunk(png,"IEND",{});
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));
    check(static_cast<bool>(file),"cannot write the picture");
}

constexpr RoutineEntry award{0,0x170D};
std::uint16_t operand(const probe::Rom& rom, std::uint16_t address) {
    return static_cast<std::uint16_t>(rom.byte(0,address)|(rom.byte(0,static_cast<std::uint16_t>(address+1))<<8));
}
std::uint16_t peek_word(const Machine& m, std::uint16_t address) {
    return static_cast<std::uint16_t>(m.memory().read(address)|(m.memory().read(static_cast<std::uint16_t>(address+1))<<8));
}
unsigned digits(unsigned value) { unsigned n=1; while (value>=10) { value/=10; ++n; } return n; }

struct Run {
    const probe::Rom& rom;
    std::shared_ptr<const Memory::Rom> image;
    Memory::CartridgeRam cart;
    replay::Recording recording;
    std::filesystem::path out;
    std::string tag;
    // The machine at the start of the frame in which the award starts, and its number.
    std::optional<Machine> before;
    std::uint64_t frame=0;

    void find_award() {
        Machine machine(image,cart);
        RoutineRegistry registry;
        game::register_routines(registry,rom.sha1);
        FrameInput input{};
        std::size_t change=0;
        for (std::uint64_t n=0;n<recording.frames && !before;++n) {
            if (change<recording.changes.size() && recording.changes[change].frame==n) input=recording.changes[change++].input;
            const auto& counters=registry.find(award)->counters;
            const auto calls=counters.cpp_calls+counters.fallback_calls;
            Machine start=machine;
            machine.run_frame(input,registry);
            machine.take_psg_writes(); machine.take_samples();
            if (counters.cpp_calls+counters.fallback_calls>calls) { before=start; frame=n; }
        }
        check(before.has_value(),"the recording contains no award");
    }
    // Replays from the start of the award's frame with the stored reward replaced.
    // Returns the displayed reward; checks every eligible character gained it.
    unsigned play(unsigned rate, unsigned stored, unsigned frames_after, const std::vector<unsigned>& dump) {
        Machine machine=*before;
        RoutineRegistry registry;
        game::register_routines(registry,rom.sha1,game::Options{100,rate});
        const auto reward=operand(rom,0x170E), displayed=operand(rom,0x1711);
        const std::array<std::uint16_t,4> records{operand(rom,0x171E),operand(rom,0x172C),operand(rom,0x173B),operand(rom,0x174A)};
        const auto status=rom.byte(0,0x1756), exp=rom.byte(0,0x176C);
        std::array<unsigned,4> eligible{}, experience{};
        for (unsigned k=0;k<4;++k) {
            eligible[k]=machine.memory().read(static_cast<std::uint16_t>(records[k]+status))&1;
            experience[k]=peek_word(machine,static_cast<std::uint16_t>(records[k]+exp));
        }
        const unsigned original=peek_word(machine,reward);
        check(original!=0,"the battle's reward is already stored when the award's frame starts");
        machine.memory().write(reward,static_cast<std::uint8_t>(stored));
        machine.memory().write(static_cast<std::uint16_t>(reward+1),static_cast<std::uint8_t>(stored>>8));
        unsigned shown=0;
        for (unsigned n=0;n<=frames_after;++n) {
            // The recorded input belongs to another game state, so after the message has
            // been on screen the test presses the first button briefly and regularly
            // to dismiss the text.
            FrameInput input=recording.input_at(frame+n);
            input.pads[0].one=n>=60 && n%30<4;
            machine.run_frame(input,registry);
            machine.take_psg_writes(); machine.take_samples();
            // The dialogue number is reused by later text, so it is read the first time it holds a reward.
            if (!shown && n<30) shown=peek_word(machine,displayed);
            if (std::find(dump.begin(),dump.end(),n)!=dump.end() && !out.empty())
                write_png(out/(tag+"-rate"+std::to_string(rate)+"-stored"+std::to_string(stored)+"-frame"+std::to_string(n)+".png"),machine.picture());
        }
        for (const auto& [entry,routine]:registry.entries()) check(routine.counters.forced_failed==0,"a forced call failed");
        // The number the message shows is the stored reward as the option scales it.
        const unsigned expected=rate==100 ? stored : std::min(65535u,stored*rate/100);
        check(shown==expected,"the displayed reward is the scaled stored reward");
        check(peek_word(machine,reward)==stored,"the stored reward is unchanged");
        for (unsigned k=0;k<4;++k)
            if (eligible[k]) {
                const unsigned now=peek_word(machine,static_cast<std::uint16_t>(records[k]+exp));
                check(now==std::min(65535u,experience[k]+expected),"an eligible character gained the displayed reward: "+tag+" "+std::to_string(k)+
                      " before "+std::to_string(experience[k])+" now "+std::to_string(now)+" shown "+std::to_string(expected));
            }
        std::cout<<tag<<": rate "<<rate<<"%, stored reward "<<stored<<" (was "<<original<<") shows "<<expected<<" ("<<digits(expected)
                 <<" digits); award frame "<<frame+1<<"; "<<frames_after<<" frames run; characters gained it.\n";
        return expected;
    }
};
}

int main(int argc, char** argv) { try {
    if (argc!=5 && argc!=6) throw std::runtime_error("Expected ROM, version, recording, cartridge RAM image and optional output folder.");
    const auto rom=probe::load(argv[1],argv[2]);
    if (!rom || !std::filesystem::exists(argv[3]) || !std::filesystem::exists(argv[4])) { std::cout<<"An input is missing; skipping.\n"; return 77; }
    Run run{*rom,rom->image,{},replay::read(argv[3]),argc==6 ? std::filesystem::path(argv[5]) : std::filesystem::path(),argv[2],{},0};
    check(run.recording.rom_sha1==rom->sha1,"recording and ROM differ");
    std::ifstream image(argv[4],std::ios::binary);
    check(static_cast<bool>(image.read(reinterpret_cast<char*>(run.cart.data()),static_cast<std::streamsize>(run.cart.size()))),"cartridge RAM image");
    if (!run.out.empty()) std::filesystem::create_directories(run.out);
    run.find_award();
    std::vector<unsigned> frames;
    for (unsigned n=0;n<=360;n+=10) frames.push_back(n);
    // Five digits from the option, then five digits stored in original mode.
    run.play(400,12500,2400,frames);
    run.play(100,12345,2400,frames);
    // And a four-digit reward for comparison.
    run.play(400,1500,2400,{});
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
