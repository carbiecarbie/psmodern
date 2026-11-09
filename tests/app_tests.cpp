// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "app/host.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
template<class F> void refuses(F f,const char* text) {
    bool refused=false; try { f(); } catch (const std::exception&) { refused=true; }
    check(refused,text);
}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("psmodern-app-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { check(std::filesystem::create_directory(path),"unique temporary folder"); }
    ~Temporary() { std::error_code ec; std::filesystem::remove_all(path,ec); }
};
auto program() { return std::make_shared<Memory::Rom>(512*1024,0); }
void change(Memory& m,std::uint8_t value) { m.write(0xfffc,8); m.write(0x8000,value); }
void colours() {
    for (unsigned r=0;r<4;++r) for (unsigned g=0;g<4;++g) for (unsigned b=0;b<4;++b)
        check(rgb(r|g<<2|b<<4)==(0xff000000u|r*85<<16|g*85<<8|b*85),"all 64 CRAM colours");
    check(rgb(0xff)==rgb(0x3f),"only six bits are colour");
}
void scaling() {
    check(viewport(768,576)==Viewport{0,0,768,576,3},"3x opening");
    check(viewport(1920,1080)==Viewport{320,60,1280,960,5},"desktop bars and integer scale");
    check(viewport(1000,700)==Viewport{116,62,768,576,3},"non-multiple size");
    check(viewport(257,193)==Viewport{0,0,256,192,1},"odd bars differ by one");
    check(viewport(100,100)==Viewport{-78,-46,256,192,1},"minimum 1 clips when output is too small");
    check(viewport(0,0).scale==1,"zero drawable size");
}
void inputs() {
    EnterKey enter;
    check(enter.event(true,true,false),"Alt+Enter toggles fullscreen");
    check(!enter.pause(true),"fullscreen Enter is consumed while Alt is held");
    check(!enter.event(true,false,true) && !enter.pause(true),"releasing Alt and repeating Enter cannot press Pause");
    check(!enter.event(true,true,true) && !enter.pause(true),"pressing Alt again cannot release consumed Enter");
    enter.event(false,false,false);
    check(!enter.pause(false),"Enter release clears shortcut latch");
    check(!enter.event(true,false,false) && enter.pause(true),"fresh Enter press pauses");
    for (unsigned i=0;i<8;++i) {
        Keys keys{}; keys[i]=true;
        auto f=combine(keyboard(keys),{});
        const auto p=f.pads[0];
        const std::array<bool,8> actual{p.up,p.down,p.left,p.right,p.one,p.two,f.pause,f.reset};
        check(actual==keys && f.pads[1]==Pad{},"every key maps to its own button; pad 2 released");
    }
    check(controller({},8000,-8000).pad==Pad{},"dead zone inclusive");
    check(controller({},8001,-8001).pad==Pad{true,false,false,true},"stick above dead zone");
    check(controller({},-32768,32767).pad==Pad{false,true,true,false},"stick extremes");
    Buttons d{{true,false,true,false,true,false},true,false};
    auto c=controller(d,20000,20000);
    auto f=combine({{false,true,false,true,false,true},false,true},c);
    check(f.pads[0]==Pad{true,true,true,true,true,true} && f.pause && f.reset && f.pads[1]==Pad{},
          "keyboard OR first controller, digital OR stick, opposing directions preserved");
    check(combine({},{}).pads[0]==Pad{},"hot-unplug releases controller state");
}
void saves() {
    Temporary t; SaveStore store(t.path,RomVersion::us_rev_a);
    check(store.load()==Memory::CartridgeRam{},"missing means zero");
    check(std::string(save_name(RomVersion::us_rev_a))!=save_name(RomVersion::brazilian),"versions have separate files");
    Memory memory(program());
    check(!store.flush(memory) && !std::filesystem::exists(store.path()),"clean image creates no save");
    change(memory,42); check(!store.flush(memory) && !memory.cartridge_ram_dirty() && store.writes()==1,"dirty flush");
    check(store.load()[0]==42 && std::filesystem::file_size(store.path())==8192,"load exact image");
    change(memory,64); check(!store.flush(memory) && store.load()[0]==64,"atomic replacement");
    for (const auto& item:std::filesystem::directory_iterator(t.path)) check(item.path()==store.path() || item.path().extension()==".lock","no temporary remains");
    change(memory,64); check(!store.flush(memory) && store.writes()==2,"same byte stays clean");
    for (unsigned size:{0u,8191u,8193u}) {
        auto folder=t.path/std::to_string(size); std::filesystem::create_directory(folder);
        SaveStore bad(folder,RomVersion::brazilian);
        { std::ofstream f(bad.path(),std::ios::binary); std::string data(size,'q'); f.write(data.data(),data.size()); }
        refuses([&] { bad.load(); },"wrong size refused");
        Memory dirty(program()); change(dirty,1);
        check(bad.flush(dirty).has_value() && std::filesystem::file_size(bad.path())==size,"malformed save never overwritten");
    }
    SaveStore directory(t.path/"directory",RomVersion::brazilian);
    std::filesystem::create_directories(directory.path());
    refuses([&] { directory.load(); },"directory is not a save");
#ifdef _WIN32
    auto lock=CreateFileW(store.path().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(lock!=INVALID_HANDLE_VALUE,"lock fixture");
    refuses([&] { store.load(); },"unreadable locked save refused");
    change(memory,65); auto error=store.flush(memory);
    check(error.has_value() && memory.cartridge_ram_dirty(),"locked replacement reports failure and retains dirty");
    CloseHandle(lock);
    check(store.load()[0]==64,"failed replacement keeps original bytes");
    for (const auto& item:std::filesystem::directory_iterator(t.path))
        check(item.path().extension().string().find(".tmp")==std::string::npos,"failed replacement cleans temporary");
    check(!store.flush(memory) && store.load()[0]==65,"next-frame retry succeeds");
#endif
    auto blocker=t.path/"blocked"; { std::ofstream f(blocker); f<<"file"; }
    SaveStore blocked(blocker,RomVersion::us_rev_a); Memory dirty(program()); change(dirty,11);
    check(blocked.flush(dirty).has_value(),"real folder creation failure");
    std::filesystem::remove(blocker);
    check(!blocked.flush(dirty) && blocked.load()[0]==11,"real I/O failure retried");
}
void backups() {
    Temporary t; SaveStore store(t.path,RomVersion::us_rev_a);
    Memory::CartridgeRam foreign{}; foreign.fill(0xa5);
    atomic_save(store.path(),foreign);
    auto original=store.load(); Memory memory(program(),original);
    auto backup=store.path(); backup += ".bak";
    { std::ofstream older(backup,std::ios::binary); older << "old"; }
    change(memory,42); check(!store.flush(memory),"save with existing foreign image");
    std::ifstream f(backup,std::ios::binary);
    Memory::CartridgeRam copy{};
    check(bool(f.read(reinterpret_cast<char*>(copy.data()),copy.size())) && copy==foreign,
          "first write preserves exact foreign image in backup");
    change(memory,43); check(!store.flush(memory),"second write succeeds");
    std::ifstream again(backup,std::ios::binary);again.read(reinterpret_cast<char*>(copy.data()),copy.size());
    check(copy==foreign,"later writes do not replace session backup");
    auto other=t.path/"failed-backup"; std::filesystem::create_directories(other);
    SaveStore blocked(other,RomVersion::us_rev_a);atomic_save(blocked.path(),foreign);
    auto blocked_backup=blocked.path();blocked_backup += ".bak";std::filesystem::create_directory(blocked_backup);
    Memory dirty(program(),blocked.load());change(dirty,42);
    check(blocked.flush(dirty).has_value() && dirty.cartridge_ram_dirty() && blocked.load()==foreign,
          "failed backup reports error and preserves save and dirty state");
    std::filesystem::remove(blocked_backup);
    check(!blocked.flush(dirty) && blocked.load()[0]==42,"backup failure is retried");
}
void driver() {
    Temporary t; unsigned attempts=0; bool fail=true;
    SaveStore store(t.path,RomVersion::us_rev_a,[&](const auto& p,const auto& image) {
        ++attempts; if (fail) throw std::runtime_error("injected disk failure"); atomic_save(p,image);
    });
    const auto rom=program();
    FrameDriver d(rom,std::move(store));
    for (int i=0;i<10;++i) {
        auto frame=d.run({}); check(!frame.save_error && !frame.samples.values.empty(),"whole frame and drain");
        check(d.machine().take_samples().values.empty() && d.machine().take_psg_writes().empty(),"both outputs drained");
    }
    check(attempts==0 && !d.flush(),"no save on unchanged frames or exit");
    change(d.machine().memory(),7);
    check(d.run({}).save_error.has_value() && attempts==1,"failed end-of-frame flush");
    check(d.power_cycle().has_value() && attempts==2 && d.machine().cycles()==0,"power cycle flushes first");
    check(d.machine().memory().cartridge_ram()[0]==7,"power cycle keeps current RAM despite failure");
    fail=false;
    check(!d.run({}).save_error && attempts==3 && d.store().load()[0]==7,"retry survives new machine's clean flag");
    check(!d.power_cycle() && attempts==3,"clean power cycle does not write");
    Machine fixed(rom,d.store().load()); check(d.machine().equivalent(fixed),"power cycle returns to fixed complete state");
    // The host owns the registry across power cycles. Both switch settings
    // execute the same authored program, with one call at power-on.
    for (bool enabled:{false,true}) {
        Temporary folder;
        auto code=program(); (*code)[0]=0xc9;
        FrameDriver routine_driver(code,SaveStore(folder.path,RomVersion::us_rev_a),enabled);
        auto prepare=[&] {
            auto& m=routine_driver.machine(); m.cpu().sp.uint16_value=0xc100;
            m.memory().write(0xc100,0);m.memory().write(0xc101,2);
        };
        routine_driver.routines().add({0,0},[](const Machine&){return 10;},[](RoutineContext& c){c.finish_return(0);});
        prepare(); auto original=routine_driver.machine();
        routine_driver.run({}); original.run_frame({}); original.take_psg_writes(); original.take_samples();
        check(original.equivalent(routine_driver.machine()),"frame driver passes registry and preserves the original program");
        const auto& counts=routine_driver.routines().find({0,0})->counters;
        check(counts.cpp_calls==enabled && counts.fallback_calls==!enabled,"frame driver respects routine switch");
        check(!routine_driver.power_cycle() && routine_driver.routines().enabled==enabled,"routine option survives power cycle");
        prepare(); routine_driver.run({});
        check(counts.cpp_calls==2*enabled && counts.fallback_calls==2*!enabled,"host registry counters survive power cycle");
    }
}
void resampling() {
    const auto n=static_cast<std::size_t>(Psg::cpu_hz/Psg::sample_cycles);
    std::vector<std::int16_t> dc(n,12345);
    for (int hz:{44100,48000,96000}) {
        Resampler r(hz); auto out=r.process(dc);
        check(std::abs(static_cast<int>(out.size())-hz)<=1,"one second length within one sample");
        check(std::all_of(out.begin(),out.end(),[](auto s) { return s==12345; }),"DC preserved");
    }
    std::vector<std::int16_t> wave(n);
    for (std::size_t i=0;i<n;++i) wave[i]=static_cast<std::int16_t>(12000*std::sin(i*0.37));
    Resampler whole(48000),chunked(48000); auto expected=whole.process(wave);
    std::vector<std::int16_t> joined;
    for (std::size_t i=0;i<n;i+=3733) {
        auto out=chunked.process(std::span(wave).subspan(i,std::min(n-i,std::size_t(3733))));
        joined.insert(joined.end(),out.begin(),out.end());
    }
    check(expected==joined,"frame splits preserve fractional integration and length");
    for (std::size_t i=0;i<n;++i) wave[i]=(i&1) ? -12000 : 12000;
    Resampler anti_alias(48000); auto attenuated=anti_alias.process(wave);
    check(std::all_of(attenuated.begin(),attenuated.end(),[](auto v) { return std::abs(v)<2600; }),"input Nyquist strongly attenuated by averaging");
    check(rate_adjustment(0)==0.995 && rate_adjustment(99)==1.005,"rate controller bounded to half a percent");
    check(std::abs(rate_adjustment(audio_target_seconds-frame_seconds/2)-1)<1e-12,"neutral queue level");
    for (double adjustment:{-9.0,0.995,1.0,1.005,99.0}) {
        Resampler r(48000); auto out=r.process(dc,adjustment);
        check(std::abs(static_cast<double>(out.size())-48000/Resampler::bounded_adjustment(adjustment))<2,"bounded ratio controls output length");
    }
    refuses([] { Resampler r(0); },"invalid device rate");
}
void pacing() {
    FrameClock c(10);
    check(c.due(10)==1 && c.due(10)==0,"deadline executes exactly once");
    check(c.due(10+frame_seconds/2)==0,"no display-rate emulation");
    check(c.due(10+2*frame_seconds+1e-9)==2,"whole-frame catch-up");
    check(c.due(30)==3 && c.deadline()>30 && c.discarded_seconds()>19,"lag caps catch-up and rebases wall time");
    FrameClock clock(0); std::uint64_t count=0;
    for (int ms=0;ms<180000;++ms) count+=clock.due(ms/1000.0);
    check(std::abs(count*frame_seconds-180)<frame_seconds && clock.discarded_seconds()==0,"three-minute machine clock has no growing drift");
    for (int refresh:{50,60,144}) {
        FrameClock vsync_clock(0); std::uint64_t emulated=0;
        for (int tick=0;tick<=180*refresh;++tick)
            emulated+=vsync_clock.due(static_cast<double>(tick)/refresh);
        check(std::abs(emulated*frame_seconds-180)<frame_seconds && vsync_clock.discarded_seconds()==0,
              "display refresh does not determine machine frame rate");
    }
    for (double device_error:{-0.002,0.002}) {
        double queue=audio_target_seconds;
        for (int frame=0;frame<11000;++frame) {
            queue+=frame_seconds/rate_adjustment(queue)-frame_seconds*(1+device_error);
            check(queue>0.02 && queue<0.12,"queue remains bounded with device clock mismatch");
        }
    }
}
}
int main() {
    try { colours(); scaling(); inputs(); saves(); backups(); driver(); resampling(); pacing();
        std::cout<<"App: 7 test groups passed (no SDL or ROM).\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
