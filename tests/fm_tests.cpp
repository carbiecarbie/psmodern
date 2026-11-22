// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/fm.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>

using psmodern::Fm;
using psmodern::FmWrite;
using psmodern::Psg;
using psmodern::PsgSamples;

namespace {
void check(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Exception,class Operation>
void check_throws(Operation operation,const char* message) {
    try { operation(); }
    catch (const Exception&) { return; }
    throw std::runtime_error(message);
}

void write_register(Fm& fm,std::uint8_t address,std::uint8_t value) {
    const auto cycle = fm.cycle();
    fm.write(0xf0,address,cycle);
    fm.write(0xf1,value,cycle+12);
    fm.sync(cycle+96);
}

void sine_patch(Fm& fm) {
    // Both operators use multiplier 1. Near-silent modulator, quick attack
    // and release, and a sustained envelope at the top of the attack.
    constexpr std::array<std::uint8_t,8> patch{0x21,0x21,0x3f,0,0xf0,0xf0,0x0f,0x0f};
    for (std::uint8_t reg=0;reg<patch.size();++reg) write_register(fm,reg,patch[reg]);
}

void play_note(Fm& fm,std::uint8_t channel,std::uint8_t block,std::uint8_t attenuation) {
    write_register(fm,0x30+channel,attenuation); // User patch, volume in the low nibble.
    write_register(fm,0x10+channel,0x22); // Nine-bit F-number 0x122.
    write_register(fm,0x20+channel,0x10|(block<<1)|1);
}

std::vector<std::int16_t> render(Fm& fm,std::size_t count) {
    fm.sync(fm.cycle()+count*Fm::sample_cycles);
    return fm.take_samples().values;
}

double rms(std::span<const std::int16_t> samples) {
    double sum = 0;
    for (double value:samples) sum += value*value;
    return std::sqrt(sum/static_cast<double>(samples.size()));
}

double frequency(std::span<const std::int16_t> samples) {
    std::vector<std::size_t> rising;
    for (std::size_t index=1;index<samples.size();++index)
        if (samples[index-1]<=0 && samples[index]>0) rising.push_back(index);
    check(rising.size()>2,"tone has repeated rising zero crossings");
    return static_cast<double>(Fm::cpu_hz)*(rising.size()-1)/
        (Fm::sample_cycles*(rising.back()-rising.front()));
}

void phase_and_write_log() {
    Fm fm(true);
    check(fm.cycle()==0 && fm.writes().empty(),"FM power-on clock and write log");
    fm.sync(71);
    check(fm.take_samples()==PsgSamples{72,{}},"no sample before cycle 72");
    fm.sync(72);
    check(fm.take_samples()==PsgSamples{72,{0}},"first sample occurs at cycle 72 and is silent");
    fm.sync(77); // Start the first write between chip slots.
    sine_patch(fm);
    play_note(fm,0,4,0);
    const auto writes = fm.writes();
    check(writes.size()==22 && writes[0]==FmWrite{77,0xf0,0} &&
          writes[1]==FmWrite{89,0xf1,0x21},"write log keeps port, value and exact bus cycle");
    constexpr auto end = 20*Psg::frame_cycles/4+31;
    fm.sync(end);
    const auto original = fm.take_samples();
    check(original.first_cycle==144 && original.values.size()==end/72-1,
          "samples stay on the absolute grid past frame boundaries");

    Fm replay;
    replay.sync(72);
    replay.take_samples();
    std::vector<std::int16_t> combined;
    for (const auto& event:writes) {
        replay.sync(event.cycle-1);
        auto chunk = replay.take_samples();
        check(chunk.first_cycle==original.first_cycle+combined.size()*72,"sample drain preserves phase");
        combined.insert(combined.end(),chunk.values.begin(),chunk.values.end());
        replay.write(event.port,event.value,event.cycle);
    }
    replay.sync(end);
    auto chunk = replay.take_samples();
    combined.insert(combined.end(),chunk.values.begin(),chunk.values.end());
    check(combined==original.values && replay.writes().empty(),
          "chunked replay is exact with logging disabled");
    fm.clear_writes();
    check(fm.writes().empty() && fm.cycle()==end,"clearing the log keeps the chip clock");
    check(render(fm,100)==render(replay,100),"clearing the log keeps FM playback and phase");
}

void melodic() {
    Fm fm;
    sine_patch(fm);
    play_note(fm,0,4,0);
    render(fm,6000);
    const auto steady = render(fm,6000);
    // Fs * F-number * 2^block / 2^19 gives about 440 Hz for these registers.
    check(std::abs(frequency(steady)-440)<1,"F-number and block set the expected pitch");
    check(rms(steady)>2500,"held note has a sustained envelope");
    write_register(fm,0x20,0x1b); // One octave up, still keyed on.
    render(fm,1000);
    check(std::abs(frequency(render(fm,6000))-880)<2,"raising the block doubles the pitch");
    write_register(fm,0x20,0x0b); // Same pitch, key off.
    const auto release = render(fm,6000);
    check(rms(std::span(release).first(16))>500 && rms(release)<rms(steady)/8,
          "key-off starts an envelope release instead of cutting the note");
    check(rms(render(fm,6000))==0,"released note returns to silence");

    Fm muted;
    sine_patch(muted);
    play_note(muted,0,4,15);
    render(muted,6000);
    check(rms(render(muted,6000))<rms(steady)/30,"maximum attenuation is near silence");
}

void percussion() {
    Fm fm;
    write_register(fm,0x16,0x89);
    write_register(fm,0x17,0x92);
    write_register(fm,0x18,0x91);
    write_register(fm,0x26,0x05);
    write_register(fm,0x27,0x05);
    write_register(fm,0x28,0x01);
    for (std::uint8_t reg=0x36;reg<=0x38;++reg) write_register(fm,reg,0);
    write_register(fm,0x0e,0x20);
    check(rms(render(fm,4000))==0,"rhythm mode is silent before the drum keys");
    write_register(fm,0x0e,0x3f);
    check(rms(render(fm,4000))>1000,"rhythm output is audible when the drum keys are set");
    write_register(fm,0x0e,0x20);
    render(fm,12000);
    check(rms(render(fm,4000))==0,"drum keys off release back to silence");
}

void clipping() {
    Fm fm;
    sine_patch(fm);
    for (std::uint8_t channel=0;channel<9;++channel) play_note(fm,channel,2,0);
    const auto samples = render(fm,6000);
    check(std::count(samples.begin(),samples.end(),std::numeric_limits<std::int16_t>::max())>50,
          "in-phase full-volume channels saturate positive peaks");
    check(std::count(samples.begin(),samples.end(),std::numeric_limits<std::int16_t>::min())>50,
          "in-phase full-volume channels saturate negative peaks");
}

void long_run_and_bounds() {
    Fm fm(true);
    std::uint64_t sample_count = 0;
    for (std::uint64_t frame=1;frame<=1200;++frame) {
        const auto target = frame*Psg::frame_cycles;
        fm.sync(target);
        const auto samples = fm.take_samples();
        check(samples.first_cycle==(sample_count+1)*72,"long run has no sample time gaps");
        sample_count += samples.values.size();
        check(sample_count==target/72,"long run has no missing or early samples");
    }
    const auto then = fm.cycle();
    check_throws<std::invalid_argument>([&]{ fm.write(0x7f,0,then+1); },"PSG port rejected");
    check_throws<std::invalid_argument>([&]{ fm.write(0xf0,0,then-1); },"backdated write rejected");
    check_throws<std::invalid_argument>([&]{ fm.sync(then-1); },"backdated sync rejected");
    check_throws<std::overflow_error>([&]{ fm.sync(then+72*(Fm::max_samples+1)); },
                                      "oversize pending sample queue rejected");
    check(fm.cycle()==then && fm.writes().empty(),"rejected accesses leave FM clock and log intact");
    for (std::size_t i=0;i<Fm::max_writes;++i) fm.write(0xf0,0,then);
    check_throws<std::overflow_error>([&]{ fm.write(0xf0,0,then+72); },"full optional write log rejected");
    check(fm.cycle()==then && fm.writes().size()==Fm::max_writes,"overflow retains accepted writes");
    fm.clear_writes();
    fm.write(0xf0,0,then);
    check(fm.writes().size()==1 && render(fm,1).size()==1,"capture can resume after a drain");
}
}

int main() {
    try {
        phase_and_write_log();
        melodic();
        percussion();
        clipping();
        long_run_and_bounds();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
