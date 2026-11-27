// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "scaffold.h"
#include "app/automap_store.h"
#include "app/host.h"
#include "app/localization.h"
#include "game/dungeon_kit.h"
#include "game/routines.h"
#include "oracle/native_machine.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
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
    ~Temporary() { test::remove_temporary_folder(path); }
};
auto program() { return std::make_shared<Memory::Rom>(512*1024,0); }
void change(Memory& m,std::uint8_t value) { m.write(0xfffc,8); m.write(0x8000,value); }
void colours() {
    for (unsigned r=0;r<4;++r) for (unsigned g=0;g<4;++g) for (unsigned b=0;b<4;++b)
        check(rgb(r|g<<2|b<<4)==(0xff000000u|r*85<<16|g*85<<8|b*85),"all 64 CRAM colours");
    check(rgb(0xff)==rgb(0x3f),"only six bits are colour");
}
void scaling() {
    // The narrow canvas keeps 0.4.0's viewport exactly.
    check(viewport(768,576,256)==Viewport{0,0,768,576,3},"3x opening");
    check(viewport(1920,1080,256)==Viewport{320,60,1280,960,5},"desktop bars and integer scale");
    check(viewport(1000,700,256)==Viewport{116,62,768,576,3},"non-multiple size");
    check(viewport(257,193,256)==Viewport{0,0,256,192,1},"odd bars differ by one");
    check(viewport(100,100,256)==Viewport{-78,-46,256,192,1},"minimum 1 clips when output is too small");
    check(viewport(0,0,256).scale==1,"zero drawable size");
    // The wide canvas (milestone 10): 344 x 192, the same integer scaling.
    check(canvas_width(false)==256 && canvas_width(true)==344 && canvas_centre_x(false)==0 && canvas_centre_x(true)==44,"canvas widths");
    check(viewport(1032,576,344)==Viewport{0,0,1032,576,3},"wide 3x window");
    check(viewport(768,576,344)==Viewport{40,96,688,384,2},"the narrow 3x window shows the wide canvas at 2x");
    check(viewport(1920,1080,344)==Viewport{100,60,1720,960,5},"wide desktop at 5x");
    check(viewport(1366,768,256)==Viewport{171,0,1024,768,4},"1366 x 768 fullscreen narrow at 4x");
    check(viewport(1366,768,344)==Viewport{167,96,1032,576,3},"1366 x 768 fullscreen wide drops to 3x");
    check(viewport(345,193,344)==Viewport{0,0,344,192,1},"wide odd bars");
    check(viewport(100,100,344)==Viewport{-122,-46,344,192,1},"wide minimum 1 clips");
    refuses([] { viewport(768,576,300); },"other canvas widths");
    check(fill_viewport({0,0,1920,1080})==WindowRect{0,4,1920,1072},"FILL 1920 x 1080");
    check(fill_viewport({95,0,2245,1080})==WindowRect{250,0,1935,1080},"FILL 2340 x 1080 with a left cutout");
    check(fill_viewport({0,0,2340,1080})==WindowRect{202,0,1935,1080},"FILL leaves the odd column at right");
    check(fill_viewport({0,0,2560,1440})==WindowRect{0,5,2560,1429},"FILL 2560 x 1440 leaves the odd row at bottom");
    check(fill_viewport({0,0,1366,768})==WindowRect{0,3,1366,762},"FILL 1366 x 768");
    check(fill_viewport({0,0,1024,768})==WindowRect{0,98,1024,572},"FILL 1024 x 768");
    check(fill_viewport({9,12,1032,576})==WindowRect{9,12,1032,576},"FILL exact 3x with offset");
    check(fill_viewport({0,0,100,100})==WindowRect{0,22,100,56},"FILL fits below 1x");
    check(fill_viewport({0,0,1,4})==WindowRect{0,1,1,1},"FILL rounds to at least a pixel");
    check(fill_viewport({3,7,0,480})==WindowRect{} && fill_viewport({3,7,640,0})==WindowRect{},"FILL empty drawable");
    check(fill_viewport({0,0,2'000'000'000,1'000'000'000})==WindowRect{104'166'666,0,1'791'666'667,1'000'000'000},
          "FILL cross-products fit in 64 bits");
}
// Applying a canvas width to a normal window (milestone 10): sizes in output
// pixels, converted to window units, lowered to fit the usable bounds.
void canvas_windows() {
    const WindowBorders frame{31,8,8,8};
    const WindowRect desktop{0,0,1920,1040};
    // 1:1 without bounds: the 3x narrow window becomes the 3x wide one around the same centre.
    check(canvas_window({100,100,768,576},768,576,344,3,std::nullopt)==CanvasWindow{{-32,100,1032,576},3},"wide at 1:1, same centre");
    check(canvas_window({-32,100,1032,576},1032,576,256,3,std::nullopt)==CanvasWindow{{100,100,768,576},3},"and back");
    // Usable bounds move it inside, borders included.
    check(canvas_window({100,100,768,576},768,576,344,3,desktop,frame)==CanvasWindow{{8,100,1032,576},3},"kept inside the usable bounds");
    check(canvas_window({1200,500,768,576},768,576,344,3,desktop,frame)==CanvasWindow{{880,456,1032,576},3},"right and bottom edges");
    check(canvas_window({2020,100,768,576},768,576,344,3,WindowRect{1920,0,1920,1040},frame)==CanvasWindow{{1928,100,1032,576},3},
          "a second display's bounds");
    // HiDPI: output pixels twice the window units.
    check(canvas_window({100,100,768,576},1536,1152,344,6,desktop,frame)==CanvasWindow{{8,100,1032,576},6},"HiDPI 2:1");
    // 125%: 1032 output pixels need 825.6 units, rounded up so the output keeps the scale.
    const auto quarter=canvas_window({100,100,768,576},960,720,344,3,desktop,frame);
    check(quarter==CanvasWindow{{71,157,826,461},3},"HiDPI 5:4 rounds window units up");
    check(viewport(826*5/4,461*5/4,344).scale==3,"the rounded-up window keeps the scale");
    check(fill_viewport({0,0,quarter.rect.width*5/4,quarter.rect.height*5/4})==WindowRect{0,0,1032,576},
          "FILL uses the snapped HiDPI drawable pixels");
    // 2:1 at 11x: 1892 x 1056 units (3784 x 2112 output pixels) do not fit 1040 rows, 10x does.
    check(canvas_window({0,0,1408,1056},2816,2112,344,11,desktop,frame)==CanvasWindow{{8,48,1720,960},10},"HiDPI lowers the scale to fit");
    // 1366 x 768 with a 40-pixel taskbar: 4x wide (1376) does not fit, 3x does.
    const WindowRect laptop{0,0,1366,728};
    check(canvas_window({171,0,1024,768},1024,768,344,4,laptop,frame)==CanvasWindow{{167,96,1032,576},3},"1366 x 768 drops 4x to 3x");
    check(canvas_window({299,76,768,576},768,576,344,3,laptop,frame)==CanvasWindow{{167,76,1032,576},3},"1366 x 768 keeps 3x");
    check(canvas_window({299,76,768,576},768,576,256,3,laptop,frame)==CanvasWindow{{299,76,768,576},3},"same width, same window");
    // Nothing fits: 1x, at the bounds' top left.
    check(canvas_window({0,0,256,192},256,192,344,1,WindowRect{0,0,300,200},frame)==CanvasWindow{{8,31,344,192},1},"too small a display keeps 1x");
    check(canvas_window({0,0,768,576},0,0,344,3,std::nullopt)==CanvasWindow{{-132,0,1032,576},3},"unknown output size counts 1:1");
}
// The centre and the bands of a synthetic wide picture (milestone 10).
void compositing() {
    Vdp::Picture picture{};
    WidePicture wide{};
    for(int i=0;i<256*192;++i)picture[i]=static_cast<std::uint8_t>((i*5+i/256)&0x3f);
    for(int i=0;i<wide_width*wide_height;++i)wide[i]=static_cast<std::uint8_t>((i*7+i/wide_width*3)&0x3f);
    std::vector<std::uint32_t> centre(256*192,0x12345678);
    centre_pixels(centre,picture,nullptr);
    for(int i=0;i<256*192;++i)check(centre[i]==rgb(picture[i]),"pillarbox centre is the VDP picture");
    centre_pixels(centre,picture,&wide);
    for(int y=0;y<192;++y)for(int x=0;x<256;++x)check(centre[y*256+x]==rgb(wide[y*344+44+x]),"wide centre is the wide picture's x 44-299");
    std::vector<std::uint32_t> canvas(344*192,0x12345678);
    band_pixels(canvas,nullptr);
    for(int y=0;y<192;++y)for(int x=0;x<344;++x)
        check(canvas[y*344+x]==(x<44 || x>=300 ? 0xff000000u : 0x12345678u),"pillarbox bands black, centre untouched");
    band_pixels(canvas,&wide);
    for(int y=0;y<192;++y)for(int x=0;x<344;++x)
        check(canvas[y*344+x]==(x<44 || x>=300 ? rgb(wide[y*344+x]) : 0x12345678u),"wide bands, centre untouched");
    // Bands and centre together are the whole wide picture.
    for(int y=0;y<192;++y)std::copy_n(centre.begin()+y*256,256,canvas.begin()+y*344+44);
    for(int i=0;i<344*192;++i)check(canvas[i]==rgb(wide[i]),"bands and centre compose the wide picture");
    std::vector<std::uint32_t> small(100);
    refuses([&] { centre_pixels(small,picture,nullptr); },"a centre of the wrong size");
    refuses([&] { band_pixels(small,nullptr); },"a canvas of the wrong size");
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
    for (const auto& item:std::filesystem::directory_iterator(t.path)) check(item.path().filename().string().find(".tmp-")==std::string::npos,"no temporary remains");
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
    std::ifstream again(backup,std::ios::binary);copy={};
    check(bool(again.read(reinterpret_cast<char*>(copy.data()),copy.size())) && copy==foreign,
          "later writes do not replace session backup");
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
    // The host owns the registry across power cycles: the authored program's
    // RET at power-on is a segment, called once per power-on. (Milestone 14:
    // dispatch can no longer be switched off; the NOPs after the return run
    // on the test scaffolding.)
    {
        Temporary folder;
        auto code=program(); (*code)[0]=0xc9;
        FrameDriver routine_driver(code,SaveStore(folder.path,RomVersion::us_rev_a));
        auto prepare=[&] {
            auto& m=routine_driver.machine(); m.cpu().sp.uint16_value=0xc100;
            m.memory().write(0xc100,0);m.memory().write(0xc101,2);
        };
        game::kit::add_segment(routine_driver.routines(),{0,0},game::kit::segment({0,0},{game::kit::returned_at(0)}),10,[](auto& z) { return z.final_return(0); });
        // The original: the oracle's native machine (a Machine without a
        // registry is no longer one since milestone 14).
        prepare(); oracle::NativeMachine original(routine_driver.machine());
        routine_driver.run({}); original.run_frame({}); original.take_psg_writes(); original.take_samples();
        check(original.equivalent(routine_driver.machine()),"frame driver passes registry and preserves the original program");
        const auto& counts=routine_driver.routines().find({0,0})->counters;
        check(counts.cpp_calls==1 && counts.fallback_calls==0,"frame driver runs its registry");
        check(!routine_driver.power_cycle(),"power cycle");
        prepare(); routine_driver.run({});
        check(counts.cpp_calls==2 && counts.fallback_calls==0,"host registry counters survive power cycle");
    }
}
// The host-observed instructions stay native: activating routines fails at
// once when any of them is a registered entry or a declared exit address.
void guard() {
    check(host_observed_instructions.size()==13,"thirteen observed instructions");
    for (const auto address:{0x00af,0x0055,0x0085,0x0b14,0x0f48,0x2d93,0x02ed,0x02f5,0x1d2d,0x06b4,0x05f4})
        check(std::count(host_observed_instructions.begin(),host_observed_instructions.end(),RoutineEntry{0,static_cast<std::uint16_t>(address)})==1,"bank 0 instruction observed");
    for (const auto address:{0x8043,0x82ef})
        check(std::count(host_observed_instructions.begin(),host_observed_instructions.end(),RoutineEntry{12,static_cast<std::uint16_t>(address)})==1,"tempo audio instruction observed");
    const auto bound=[](const Machine&){return 10u;};
    const auto body=[](RoutineContext& c){scaffold::native_return(c,0);};
    auto returning=[](RoutineEntry entry) { return SegmentSpec{entry,{SegmentExit{RoutineExit::returned,entry.address}}}; };
    RoutineRegistry clean; clean.add({0,0x1000},returning({0,0x1000}),bound,body);
    clean.add({1,0x6000},SegmentSpec{{1,0x6000},{SegmentExit{RoutineExit::core_final,0x6010}}},bound,body);
    guard_observed_instructions(clean);
    // Every watched address as the observed single-instruction segment the
    // game registers there, except `skip` (review R3 L1: the game's whole
    // registry needs every watched address to begin an observed segment).
    auto observed=[&](RoutineRegistry& r, std::optional<RoutineEntry> skip={}) {
        for (const auto watched:host_observed_instructions) {
            if (skip && *skip==watched) continue;
            SegmentSpec spec{watched,{SegmentExit{RoutineExit::core_final,watched.address}}};
            spec.single_instruction=true; spec.observe_entry=true;
            r.add(watched,spec,bound,body);
        }
    };
    RoutineRegistry whole; observed(whole); require_observed_entries(whole);
    for (const auto watched:host_observed_instructions) {
        RoutineRegistry entry; entry.add(watched,returning(watched),bound,body);
        bool refused=false;
        try { guard_observed_instructions(entry); } catch (const std::logic_error& e) {
            refused=std::string(e.what()).find("registered entry")!=std::string::npos;
        }
        check(refused,"an observed instruction registered as an entry is refused");
        RoutineRegistry missing; observed(missing,watched);
        guard_observed_instructions(missing);
        refused=false;
        try { require_observed_entries(missing); } catch (const std::logic_error& e) {
            refused=std::string(e.what()).find("requires an observed segment")!=std::string::npos;
        }
        check(refused,"a watched address with no observed segment is refused for the whole game");
        for (const auto kind:{RoutineExit::core_final,RoutineExit::returned}) {
            RoutineRegistry exit; exit.add({1,0x7000},SegmentSpec{{1,0x7000},{SegmentExit{kind,watched.address}}},bound,body);
            refused=false;
            try { guard_observed_instructions(exit); } catch (const std::logic_error& e) {
                // The synthetic segment's name, built from its parts.
                refused=std::string(e.what()).find("declared exit of "+std::string("01")+":"+"7000")!=std::string::npos;
            }
            check(refused,"an observed instruction declared as a segment exit is refused");
        }
    }
    // Every registry the game registers (both ROMs share their code) passes,
    // with every milestone 7 dungeon segment registered: entries and every
    // declared exit address (conditional and loop exits included).
    for (const auto& options:{game::Options{},game::Options{0,400}}) {
        RoutineRegistry real;
        check(game::register_routines(real,"07fcf297be4f4c9d92cd3f119a7ac48467e06838",options),"identified registry");
        auto check_segments=[&](const auto& segments) {
            for (const auto& segment:segments) {
                const auto* routine=real.find(segment.entry);
                check(routine && routine->spec.unit==segment.unit && !routine->spec.exits.empty(),
                      "the dungeon segment retains its game unit and exits");
            }
        };
        check_segments(game::dungeon::cell_segments);
        check_segments(game::dungeon::movement_segments);
        check_segments(game::dungeon::object_segments);
        require_observed_entries(real);
    }
}
// Milestone 14: with no Z80 in the program, the host's hook runs only at the
// entries of segments flagged observe_entry (and, during the transition, at
// the native steps that remain). Every watched address must begin such a
// segment, or a C++ body would run through it without the host seeing it.
void observed_segments() {
    const std::set<RoutineEntry> awaiting{}; // none since Part 0b
    // Both identified ROMs (review R3 L1).
    for (const char* digest:{"07fcf297be4f4c9d92cd3f119a7ac48467e06838","fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd"})
    for (const auto& options:{game::Options{},game::Options{0,400}}) {
        RoutineRegistry real;
        check(game::register_routines(real,digest,options),"identified registry");
        std::set<RoutineEntry> missing;
        std::string names;
        for (const auto watched:host_observed_instructions) {
            const auto* routine=real.find(watched);
            if (routine && routine->spec.observe_entry) continue;
            missing.insert(watched);
            char name[16];std::snprintf(name,sizeof name," %02X:%04X",watched.bank,watched.address);names+=name;
        }
        if (missing!=awaiting)
            throw std::runtime_error("host-watched addresses without an observed segment:"+names);
    }
}
// Authored programs for the frame driver (no ROM): a body at 00:0100 that
// increments A forty times, stores A at (HL) and returns to a JR $ at 00:0200.
namespace authored {
namespace k=psmodern::game::kit;
constexpr std::uint16_t body=0x0100,body_store=0x0128,body_return=0x0129,caller=0x0200;
std::shared_ptr<Memory::Rom> rom() {
    auto image=program();
    for (std::uint16_t at=body;at<body_store;++at) (*image)[at]=0x3c;                 // INC A
    (*image)[body_store]=0x77;(*image)[body_return]=0xc9;                             // LD (HL),A; RET
    (*image)[caller]=0x18;(*image)[caller+1]=0xfe;                                    // JR $
    return image;
}
void register_body(RoutineRegistry& registry) {
    k::add_segment(registry,{0,body},k::segment({0,body},{k::returned_at(body_return)}),1000,[](auto& z) {
        for (std::uint16_t at=body;at<body_store;++at) z.increment(at,z.r.a);
        z.store_register_to_hl(body_store,z.r.a);
        return z.final_return(body_return);
    });
}
// The machine at the body's entry, `before` cycles before the next frame end
// (or with the frame interrupt due `irq_after` cycles into the body).
void enter(Machine& m,std::uint16_t hl,bool interrupts) {
    auto& cpu=m.cpu();
    cpu.im=1;cpu.iff1=cpu.iff2=interrupts;cpu.pc.uint16_value=body;cpu.sp.uint16_value=0xc1f0;cpu.hl.uint16_value=hl;
    cpu.af.uint16_value=0x0000;
    m.memory().write(0xc1f0,caller&0xff);m.memory().write(0xc1f1,caller>>8);
    (void)m.read_port(0xbf,m.cycles());m.advance(1); // acknowledge the pending frame interrupt
}
}
// Milestone 14 (review of Part 0a, L3): the save holds the cartridge RAM as
// it is at the frame end. A body that crosses the frame end and writes the
// cartridge RAM after it belongs to the next frame's save, as when frames
// ended exactly at the frame end; it was written up to a segment late.
void frame_end_save() {
    Temporary t;
    FrameDriver d(authored::rom(),SaveStore(t.path,RomVersion::us_rev_a));
    authored::register_body(d.routines());
    auto& m=d.machine();
    m.memory().write(0xfffc,8);
    authored::enter(m,0x8000,false);
    const auto target=(m.cycles()/Machine::frame_cycles+1)*Machine::frame_cycles;
    m.advance(target-m.cycles()-40);  // ten increments before the frame end
    std::uint64_t observed=0;bool dirty_at_end=true;
    d.set_frame_probe([&](const Machine& x) { observed=x.cycles();dirty_at_end=x.memory().cartridge_ram_dirty(); });
    const auto first=d.run({});
    check(!first.save_error && observed>=target && m.cycles()>observed,"the frame ended inside the body");
    check(m.memory().cartridge_ram()[0]==40 && m.cpu().pc.uint16_value==authored::caller,"the body stored A after the frame end");
    check(!dirty_at_end && d.store().writes()==0,"a cartridge RAM write after the frame end is not this frame's save");
    check(m.memory().cartridge_ram_dirty(),"the write after the frame end waits for the next frame");
    const auto second=d.run({});
    check(!second.save_error && d.store().writes()==1 && d.store().load()[0]==40,"the next frame end saves it");
    check(!m.memory().cartridge_ram_dirty(),"the save cleared the flag");
}
// Milestone 14 (planning decision 6): an error of the engine stops the game.
// Here a body whose in-body guard does not hold (the error the engine gives
// for a state a body does not handle; code with no C++ body gives its own once
// the core is gone, through the same path). Below the message box: the error
// leaves run(), the session refuses to go on, the save keeps the cartridge RAM
// of the last frame end, the message names the segment, the status is 1.
void game_stop() {
    namespace k=psmodern::game::kit;
    const auto stopping=[](RoutineRegistry& registry) {
        k::add_segment(registry,{0,authored::body},k::segment({0,authored::body},{k::returned_at(authored::body_return)}),
            [](const Machine&) { return std::uint64_t{1000}; },[](auto& z) {
                for (std::uint16_t at=authored::body;at<authored::body_store;++at) z.increment(at,z.r.a);
                z.store_register_to_hl(authored::body_store,z.r.a);
                z.guard(false,"the authored stop");
                return z.final_return(authored::body_return);
            });
    };
    auto expect_stop=[](FrameDriver& d) {
        std::string what;
        try { d.run({}); } catch (const GuardFailure& e) { what=e.what(); }
        check(what=="Unhandled machine state in the segment at 00:0100 (unit 00:0100): the authored stop","the guard's error leaves run()");
        check(d.stopped() && *d.stopped()==what,"the driver keeps the reason it stopped");
        bool refused=false;
        try { d.run({}); } catch (const std::logic_error&) { refused=true; }
        check(refused,"no frame runs after a stop");
        refused=false;
        try { (void)d.power_cycle(); } catch (const std::logic_error&) { refused=true; }
        check(refused,"no power cycle after a stop");
        return what;
    };
    {
        // The stop comes after this frame's end: the save is the cartridge
        // RAM at the frame end (7), not the store the body made after it (40).
        Temporary t;
        FrameDriver d(authored::rom(),SaveStore(t.path,RomVersion::us_rev_a));
        stopping(d.routines());
        auto& m=d.machine();
        m.memory().write(0xfffc,8);m.memory().write(0x8000,7);
        authored::enter(m,0x8000,false);
        const auto target=(m.cycles()/Machine::frame_cycles+1)*Machine::frame_cycles;
        m.advance(target-m.cycles()-40);
        bool observed=false;
        d.set_frame_probe([&](const Machine&) { observed=true; });
        const auto what=expect_stop(d);
        check(observed && m.memory().cartridge_ram()[0]==40,"the body stored after the frame end, then stopped");
        check(d.store().writes()==0,"nothing written before the session ends");
        check(!d.flush() && d.store().writes()==1 && d.store().load()[0]==7,"the exit writes the frame end's cartridge RAM");
        check(!d.flush() && d.store().writes()==1,"once");
        const auto english=game_stopped_message(Language::english,what),portuguese=game_stopped_message(Language::portuguese,what);
        check(english==std::string(ui_text(UiText::game_stopped,Language::english))+"\n\n"+what &&
              english.starts_with("The game stopped: it reached a part of the cartridge that PSModern cannot run."),"the English message");
        check(portuguese==std::string(ui_text(UiText::game_stopped,Language::portuguese))+"\n\n"+what &&
              portuguese.starts_with("O jogo parou: "),"the Portuguese message, details in English");
        check(session_status(false,true)==1 && session_status(true,true)==1,"a stopped session exits with status 1");
        check(session_status(false,false)==0 && session_status(true,false)==1,"other sessions as before");
    }
    {
        // A save at an earlier frame end (3) stays; the stopped frame's own
        // cartridge RAM writes (5, then 40) never reach the file.
        Temporary t;
        FrameDriver d(authored::rom(),SaveStore(t.path,RomVersion::us_rev_a));
        stopping(d.routines());
        auto& m=d.machine();
        m.memory().write(0xfffc,8);m.memory().write(0x8000,3);
        auto& cpu=m.cpu();cpu.iff1=cpu.iff2=0;cpu.pc.uint16_value=authored::caller;
        const auto first=d.run({});
        check(!first.save_error && d.store().writes()==1 && d.store().load()[0]==3,"the frame end saved 3");
        m.memory().write(0x8000,5);
        authored::enter(m,0x8000,false);
        bool observed=false;
        d.set_frame_probe([&](const Machine&) { observed=true; });
        (void)expect_stop(d);
        check(!observed && m.memory().cartridge_ram()[0]==40,"the stop came before the frame end");
        check(!d.flush() && d.store().writes()==1 && d.store().load()[0]==3,"the exit leaves the last frame end's save");
    }
}
// Milestone 14 (review of Part 0a, L3): a hook that changes the game (here
// the menu cancel at 00:2D93, which releases the pad) must never run inside a
// nested interrupt delivery, where Machine does not see RAM or pad changes.
// The frame driver refuses it; in the game these addresses are main-program
// code, so this only guards against a future body or handler reaching them.
void hooks_at_depth() {
    Temporary t;
    auto image=authored::rom();
    (*image)[0x0038]=0xdb;(*image)[0x0039]=0xbf;                                      // IN A,(BF)
    (*image)[0x003a]=0xc3;(*image)[0x003b]=0x93;(*image)[0x003c]=0x2d;                 // JP 2D93
    (*image)[0x0055]=0x18;(*image)[0x0056]=0xfe;                                      // JR $ (the field wait)
    (*image)[0x1be1]=0x18;(*image)[0x1be2]=0xfe;                                      // JR $ (the field menu)
    FrameDriver d(image,SaveStore(t.path,RomVersion::us_rev_a));
    auto& m=d.machine();
    // The field wait at top level, returning to 00:0B14 in mode 9: the host
    // may request its menu, which opens at the return.
    auto& cpu=m.cpu();cpu.iff1=cpu.iff2=0;cpu.pc.uint16_value=0x0055;cpu.sp.uint16_value=0xc1e0;
    m.memory().write(0xc1e0,0x14);m.memory().write(0xc1e1,0x0b);m.memory().write(0xc202,9);
    d.run({});
    check(d.request_menu(),"the authored field wait admits the menu request");
    cpu.pc.uint16_value=0x0b14;cpu.sp.uint16_value=0xc1e2;
    d.run({});
    check(d.menu_active() && cpu.pc.uint16_value==0x1be1,"the menu opened at the wait's return");
    d.close_menu();
    // Now an interrupt inside the body runs a handler that reaches 00:2D93
    // with the cancel bit set: the cancel hook would release the pad there.
    authored::register_body(d.routines());
    authored::enter(m,0xc100,true);
    m.memory().write(0xc205,0x10);
    const auto irq=m.next_irq();
    check(irq.has_value() && *irq>m.cycles()+20,"frame interrupt ahead");
    m.advance(*irq-m.cycles()-20);
    bool refused=false;
    try { d.run({}); } catch (const std::logic_error& e) {
        refused=std::string(e.what()).find("menu cancel was reached inside a nested interrupt delivery")!=std::string::npos;
    }
    check(refused,"the menu cancel hook ran inside a nested delivery");
}
// <save>.automap beside the save: write order, pruning, backup and every
// failure case, with real temporary files and no ROM.
void automap_files() {
    using game::Exploration;
    auto slot=[](const Memory::CartridgeRam& image,unsigned k) {
        return automap_digest(std::span<const std::uint8_t,game::dungeon::game_block_bytes>(image.data()+k*0x400,game::dungeon::game_block_bytes));
    };
    auto read=[](const std::filesystem::path& p) { std::ifstream f(p,std::ios::binary); return std::string(std::istreambuf_iterator<char>(f),{}); };
    Exploration explored{}; explored.floors[3].seen.set(17); explored.floors[3].found.set(18); explored.floors[60].seen.set(255);
    Exploration later=explored; later.floors[4].seen.set(1);
    Temporary t; const auto sav=t.path/save_name(RomVersion::us_rev_a);
    auto map_path=sav; map_path+=".automap"; auto map_backup=map_path; map_backup+=".bak";
    Memory::CartridgeRam disk{}; disk[0x201]=1; disk[0x400]=11;   // slot 1 used
    atomic_save(sav,disk);
    std::vector<std::string> order; bool fail_map=false,fail_backup=false; unsigned backups=0;
    auto map_writer=[&](const std::filesystem::path& p,const std::string& text) {
        order.push_back(p.filename().string()); if (p.extension()==".bak") ++backups;
        if (fail_map && p.extension()==".automap") throw std::runtime_error("injected automap failure");
        if (fail_backup && p.extension()==".bak") throw std::runtime_error("injected backup failure");
        atomic_text(p,text);
    };
    auto raw_writer=[&](const std::filesystem::path& p,const Memory::CartridgeRam& image) { order.push_back(p.filename().string()); atomic_save(p,image); };
    const auto map_name=map_path.filename().string(),sav_name=sav.filename().string();
    // A save into slot 2: the image being written has slots 1 and 2 used.
    auto image=disk; image[0x202]=1; image[0x800]=22;
    {
        SaveStore store(t.path,RomVersion::us_rev_a,raw_writer,atomic_settings,map_writer);
        store.load_automap();
        check(!store.automap_warning() && !store.automap_record(slot(disk,1)) && !std::filesystem::exists(map_path),"missing file is an empty image");
        Memory memory(program(),image); change(memory,5);
        check(!store.flush(memory) && order==std::vector<std::string>{sav_name} && !std::filesystem::exists(map_path),"no staged record, no automap file");
        order.clear(); change(memory,6);
        store.stage_automap(slot(image,2),explored);
        store.stage_automap(std::string(64,'a'),later);   // no used slot holds it: pruned on purpose
        check(store.automap_pending() && store.automap_record(slot(image,2)) && *store.automap_record(slot(image,2))==explored,"a staged record is visible at once");
        check(!store.flush(memory) && order==std::vector<std::string>{map_name,sav_name},"the automap is written before the cartridge image");
        check(!store.automap_pending() && store.automap_writes()==1 && !std::filesystem::exists(map_backup) && backups==0,"no backup without an earlier file");
        auto parsed=parse_automap(read(map_path));
        check(parsed.size()==1 && parsed.find(slot(image,2)) && *parsed.find(slot(image,2))==explored,"file holds the staged record of a used slot");
        check(!store.automap_record(std::string(64,'a')) && !store.automap_record(slot(image,1)),"unknown digest gives an empty map");
        // A failed write is reported once, never blocks the cartridge write,
        // and is retried with a backoff even with a clean cartridge image:
        // after 60 flushes, then 120; at once for a newly staged record or an
        // explicit flush (exit, power cycle).
        order.clear(); fail_map=true; change(memory,7); store.stage_automap(slot(image,2),later);
        const auto error=store.flush(memory);
        check(error && error->find("Automap write failed")!=std::string::npos && order==std::vector<std::string>{map_name,sav_name} &&
              !memory.cartridge_ram_dirty() && store.load()[0]==7,"automap failure reported, cartridge still written");
        check(store.automap_pending() && parse_automap(read(map_path))==parsed,"failed write keeps the old file and the staged record");
        order.clear();
        for (unsigned n=0;n<60;++n) check(!store.flush(memory),"silent during the backoff");
        check(order.empty(),"no retry at every frame");
        check(!store.flush(memory) && order==std::vector<std::string>{map_name},"retried after 60 flushes with a clean cartridge, reported only once");
        order.clear();
        for (unsigned n=0;n<120;++n) check(!store.flush(memory),"silent during the second backoff");
        check(order.empty(),"the backoff doubles after a second failure");
        check(!store.flush(memory) && order==std::vector<std::string>{map_name},"retried after 120 more flushes");
        order.clear(); change(memory,70);
        check(!store.flush(memory) && order==std::vector<std::string>{sav_name} && store.load()[0]==70,"the cartridge is written during the backoff, the map waits");
        order.clear(); store.stage_automap(slot(image,2),later);
        check(!store.flush(memory) && order==std::vector<std::string>{map_name} && store.automap_pending(),"a newly staged record is tried at once");
        order.clear();
        check(!store.flush(memory) && order.empty(),"and the backoff starts again");
        fail_map=false;
        check(!store.flush(memory,true) && order==std::vector<std::string>{map_name} && !store.automap_pending() && store.automap_writes()==2,
              "an explicit flush retries at once; the retry succeeds");
        check(*parse_automap(read(map_path)).find(slot(image,2))==later,"retried record written");
        check(!store.flush(memory) && order.size()==1,"nothing pending, nothing written");
    }
    // Next session: one backup of the previous file, then pruning to the
    // used slots of the image on disk and of the image being written.
    {
        order.clear(); backups=0;
        const auto before=read(map_path);
        SaveStore store(t.path,RomVersion::us_rev_a,raw_writer,atomic_settings,map_writer);
        auto next=store.load(); next[0x800]=33;   // slot 2 saved again: a new digest
        Memory memory(program(),next); change(memory,8);
        store.stage_automap(slot(next,2),explored);
        check(!store.flush(memory) && order==std::vector<std::string>{map_path.filename().string()+".bak",map_name,sav_name},"backup, automap, cartridge");
        check(read(map_backup)==before && backups==1,"backup holds the previous file");
        auto parsed=parse_automap(read(map_path));
        check(parsed.size()==2 && parsed.find(slot(image,2)) && parsed.find(slot(next,2)),"union of the old and new used slots kept");
        order.clear(); change(memory,9); store.stage_automap(slot(next,2),later);
        check(!store.flush(memory) && backups==1 && order==std::vector<std::string>{map_name,sav_name},"one backup per session");
        parsed=parse_automap(read(map_path));
        check(parsed.size()==1 && parsed.find(slot(next,2)) && *parsed.find(slot(next,2))==later,"the overwritten slot's record is dropped once the disk no longer holds it");
        // A destination that stops being a regular file: write error, retried.
        std::filesystem::remove(map_path); std::filesystem::create_directory(map_path);
        store.stage_automap(slot(next,2),explored);
        check(store.flush(memory).has_value() && store.automap_pending(),"directory destination is a write error");
        std::filesystem::remove(map_path);
        check(!store.flush(memory,true) && !store.automap_pending() && *parse_automap(read(map_path)).find(slot(next,2))==explored,"retried after the obstacle is removed");
    }
    // An unusable file (newer version, corrupt, oversized): warning and an
    // empty map; at the next save it is kept byte for byte as .automap.bak
    // before the new file is written; the cartridge always saves.
    const auto bak_name=map_name+".bak";
    for (const auto& bad:{std::string("psmodern-automap 2\n"),std::string("garbage"),std::string(automap_max_bytes+1,'x')}) {
        { std::ofstream f(map_path,std::ios::binary); f<<bad; }
        {
            order.clear(); backups=0;
            SaveStore store(t.path,RomVersion::us_rev_a,raw_writer,atomic_settings,map_writer);
            store.load_automap();
            check(store.automap_warning() && store.automap_warning()->find(".automap.bak at the next save")!=std::string::npos,"unusable file warns");
            auto current=store.load(); Memory memory(program(),current); change(memory,current[0]+1);
            check(!store.automap_record(slot(current,1)) && !store.automap_record(slot(current,2)),"unusable file gives an empty map");
            check(!store.flush(memory) && order==std::vector<std::string>{sav_name} && read(map_path)==bad,"no save of the map, no change to the file");
            // The backup fails: nothing is written over the file; reported, retried.
            order.clear(); fail_backup=true; change(memory,current[0]+2); store.stage_automap(slot(current,2),explored);
            const auto error=store.flush(memory);
            check(error && order==std::vector<std::string>{bak_name,sav_name} && read(map_path)==bad && store.automap_pending() &&
                  !memory.cartridge_ram_dirty(),"a failed backup never replaces the unusable file nor blocks the save");
            order.clear(); fail_backup=false;
            check(!store.flush(memory,true) && order==std::vector<std::string>{bak_name,map_name} && read(map_backup)==bad,"retried: the unusable file kept as .automap.bak first");
            check(*parse_automap(read(map_path)).find(slot(current,2))==explored,"then the new file is written");
            order.clear(); store.stage_automap(slot(current,2),later);change(memory,current[0]+3);
            check(!store.flush(memory) && order==std::vector<std::string>{map_name,sav_name} && read(map_backup)==bad,"one backup per session");
        }
        SaveStore again(t.path,RomVersion::us_rev_a);again.load_automap();
        check(!again.automap_warning() && again.automap_record(slot(again.load(),2)),"the next session reads the new file");
    }
    // A folder in place of the file cannot be preserved: never replaced, the
    // write fails (reported once, retried) and the cartridge still saves.
    std::filesystem::remove(map_path); std::filesystem::create_directory(map_path);
    {
        order.clear();
        SaveStore store(t.path,RomVersion::us_rev_a,raw_writer,atomic_settings,map_writer);
        store.load_automap(); check(store.automap_warning().has_value(),"a folder in place of the file is unusable");
        auto current=store.load(); Memory memory(program(),current); change(memory,current[0]+1);
        store.stage_automap(slot(current,2),explored);
        const auto error=store.flush(memory);
        check(error && error->find("cannot preserve")!=std::string::npos && order==std::vector<std::string>{sav_name} &&
              std::filesystem::is_directory(map_path) && store.automap_pending(),"an unpreservable destination is never replaced");
        check(!store.flush(memory,true) && std::filesystem::is_directory(map_path),"retried silently");
        std::filesystem::remove(map_path); order.clear();
        check(!store.flush(memory,true) && order==std::vector<std::string>{map_name} && !store.automap_pending(),"written once the folder is gone");
    }
}
void unavailable_fm() {
    Temporary tmp;
    const auto image=program();
    FrameDriver driver(image,SaveStore(tmp.path,RomVersion::us_rev_a),{},false,program());
    driver.choose_fm(true);
    check(!driver.fm_available() && !driver.fm_selected(),"invalid sound image enabled FM");
    const auto first=driver.run({});
    check(first.audio_error && !first.playback && !driver.stopped(),"bad private driver stopped gameplay or suppressed original sound");
    check(!driver.run({}).audio_error,"failed private driver retried within the same power cycle");
    check(!driver.power_cycle() && driver.run({}).audio_error.has_value(),"power cycle did not retry the sound source");
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
    // Two FM samples span exactly nine PSG samples. The host's partial
    // output period must survive changes of source in either direction.
    Resampler mixed_rates(48000),always_psg(48000);
    std::vector<std::int16_t> mixed,reference;
    for(int i=0;i<1000;++i) {
        const bool fm=(i%3)!=0;
        const std::int16_t level=i%2 ? 5712 : -9000;
        auto part=mixed_rates.process(std::vector<std::int16_t>(fm ? 2 : 9,level),1,1,fm ? Fm::sample_cycles : Psg::sample_cycles);
        auto original=always_psg.process(std::vector<std::int16_t>(9,level));
        mixed.insert(mixed.end(),part.begin(),part.end());
        reference.insert(reference.end(),original.begin(),original.end());
    }
    check(mixed==reference,"switching FM/PSG lost the device sample phase or changed its gain");
    for (std::size_t i=0;i<n;++i) wave[i]=(i&1) ? -12000 : 12000;
    Resampler anti_alias(48000); auto attenuated=anti_alias.process(wave);
    check(std::all_of(attenuated.begin(),attenuated.end(),[](auto v) { return std::abs(v)<2600; }),"input Nyquist strongly attenuated by averaging");
    check(rate_adjustment(0)==0.995 && rate_adjustment(99)==1.005,"rate controller bounded to half a percent");
    check(std::abs(rate_adjustment(audio_target_seconds-frame_seconds/2)-1)<1e-12,"neutral queue level");
    for (double adjustment:{-9.0,0.995,1.0,1.005,99.0}) {
        Resampler r(48000); auto out=r.process(dc,adjustment);
        check(std::abs(static_cast<double>(out.size())-48000/Resampler::bounded_adjustment(adjustment))<2,"bounded ratio controls output length");
    }
    for (unsigned speed:{2u,4u}) {
        Resampler r(48000); auto out=r.process(dc,1,speed);
        check(std::abs(static_cast<double>(out.size())-48000.0/speed)<2,"accelerated audio length");
        check(std::all_of(out.begin(),out.end(),[](auto v){return v==12345;}),"accelerated audio preserves DC");
        Resampler whole_speed(48000),split_speed(48000);
        auto all=whole_speed.process(wave,1,speed); std::vector<std::int16_t> parts;
        for(std::size_t i=0;i<wave.size();i+=3733) {
            auto part=split_speed.process(std::span(wave).subspan(i,std::min(wave.size()-i,std::size_t(3733))),1,speed);
            parts.insert(parts.end(),part.begin(),part.end());
        }
        check(all==parts,"accelerated resampling preserves frame splits");
    }
    std::vector<std::int16_t> tone(n);
    for(std::size_t i=0;i<n;++i) tone[i]=static_cast<std::int16_t>(12000*std::sin(2*3.141592653589793*440*i/n));
    for(unsigned speed:{1u,2u,4u}) {
        Resampler r(48000);const auto out=r.process(tone,1,speed);
        unsigned crossings=0;for(std::size_t i=1;i<out.size();++i)if(out[i-1]<=0 && out[i]>0)++crossings;
        check(crossings>=438 && crossings<=442,"accelerated audio retains tone cycles over shortened duration");
    }
    refuses([] { Resampler r(0); },"invalid device rate");
}
// Issue 7: the clock before the fast-forward rule, kept to prove 1x unchanged
// and to compare the accelerated counts with what the old rule gave.
struct OldClock {
    double deadline_, discarded_=0; unsigned speed_=1;
    void speed(unsigned value,double now) { if (value!=speed_) { speed_=value; deadline_=now+frame_seconds/speed_; } }
    unsigned due(double now) {
        unsigned count=0;
        while (now>=deadline_ && count<3*speed_) { ++count; deadline_+=frame_seconds/speed_; }
        if (now>=deadline_) { discarded_+=now+frame_seconds/speed_-deadline_; deadline_=now+frame_seconds/speed_; }
        return count;
    }
};
struct Paced { std::vector<double> times; std::vector<unsigned> counts; std::uint64_t frames=0;
    double rate(unsigned speed) const { return frames*frame_seconds/speed/times.back()-1; }
    // Calls after the first second whose count differs from the expected one.
    unsigned off(unsigned expected) const { unsigned n=0; for (std::size_t i=0;i<times.size();++i) n+=times[i]>1 && counts[i]!=expected; return n; }
    void add(double t,unsigned n) { times.push_back(t); counts.push_back(n); frames+=n; }
};
// One call per refresh, as when presentation blocks on vsync, with uniform
// jitter from a fixed seed; late delays one call by that many seconds.
template<class Clock> Paced refreshes(Clock& clock,double hz,double seconds,double jitter,std::size_t late_call=0,double late=0) {
    std::mt19937 random(7); std::uniform_real_distribution<double> spread(-jitter,jitter); Paced result;
    for (std::size_t k=1;k<seconds*hz;++k) {
        const double t=k/hz+0.0003+spread(random)+(k==late_call ? late : 0);
        result.add(t,clock.due(t));
    }
    return result;
}
// The play() loop: a pass that ran frames presents and waits for the next
// refresh; then every pass sleeps 1 ms while the next deadline is ahead.
Paced host_loop(FrameClock& clock,double hz,double seconds) {
    Paced result;
    for (double t=0;t<seconds;) {
        t+=0.0002; const auto n=clock.due(t); result.add(t,n);
        if (n) t=(std::floor(t*hz)+1)/hz+0.0003;
        if (clock.deadline()>t) t+=0.001;
    }
    return result;
}
FrameClock accelerated_clock(unsigned speed) { FrameClock c(0); c.speed(speed,0); return c; }
OldClock old_accelerated_clock(unsigned speed) { OldClock c{0}; c.speed(speed,0); return c; }
void screen_pacing() {
    // Speed 1: call by call as before, on random gaps with stalls, rebases and
    // excursions to 2x and 4x (compared only while at 1x).
    {
        std::mt19937 random(11); FrameClock now_clock(0); OldClock old{0}; double t=0; bool same=true;
        for (int i=0;i<200000;++i) {
            const auto r=random()%1000;
            t+=r<3 ? 0.5 : r<4 ? 30 : (random()%25000)*1e-6;
            if (r>=990) { now_clock.rebase(t); old.deadline_=t; }
            const unsigned speed=i%20000<15000 ? 1 : i%20000<17500 ? 2 : 4;
            now_clock.speed(speed,t); old.speed(speed,t);
            const auto a=now_clock.due(t), b=old.due(t);
            if (speed==1) same=same && a==b && now_clock.deadline()==old.deadline_;
        }
        check(same,"1x due and deadline identical to the old clock on any call sequence");
        FrameClock plain(0); OldClock old_plain{0}; t=0;
        for (int i=0;i<100000;++i) { t+=(random()%40000)*1e-6+(i%9973==0 ? 2 : 0); same=same && plain.due(t)==old_plain.due(t); }
        check(same && plain.discarded_seconds()==old_plain.discarded_,"1x lag rebase identical to the old clock");
    }
    // The audio queue settles where rate_adjustment equals the game-rate
    // error. The peak adds, at 4x, the rise from the mean to the last of the
    // picture's four back-to-back measurements (1.5 frames of 4x audio) and
    // half a 512-sample device buffer at 48 kHz; it must stay 5 ms below the
    // high water.
    const double burst=1.5*frame_seconds/4, half_buffer=256.0/48000;
    check(Resampler::bounded_adjustment(1+fast_forward_slew)==1+fast_forward_slew &&
          rate_adjustment(audio_high_water_seconds-burst-half_buffer-0.005)>1+fast_forward_slew,
          "full slew keeps the 4x audio queue peak at least 5 ms below the high water");
    for (unsigned speed:{2u,4u}) {
        // Screens within the slew of nominal (lock range 59.80..60.04 Hz).
        for (double hz:{60.0,59.85,60.03}) {
            auto c=accelerated_clock(speed); const auto run=refreshes(c,hz,60,0.0005);
            check(run.off(speed)==0,"locked screen: every picture carries the speed after the first second");
            // The slew alone keeps +-0.5 ms; +-1 ms at 4x needs both holds.
            auto wide=accelerated_clock(speed);
            check(refreshes(wide,hz,60,0.001).off(speed)==0,"locked screen: the hold absorbs +-1 ms of jitter");
            check(std::abs(run.rate(speed)-(hz*frame_seconds-1))<0.0005,
                  "locked screen: the game follows the screen rate");
        }
        {
            auto c=accelerated_clock(speed); const auto run=refreshes(c,120,60,0.0005);
            check(run.off(speed/2)==0,"120 Hz: half the speed per picture");
        }
        // Outside the range the clock keeps its own rate within the slew,
        // with no more uneven pictures than the old rule.
        {
            auto c=accelerated_clock(speed); auto o=old_accelerated_clock(speed);
            const auto run=refreshes(c,60.2,60,0.0005), old=refreshes(o,60.2,60,0.0005);
            check(std::abs(run.rate(speed))<fast_forward_slew+0.0001 && run.off(speed)<=old.off(speed),
                  "60.2 Hz: rate within the slew, no more uneven pictures than before");
        }
        for (double hz:{90.0,50.0}) {
            auto c=accelerated_clock(speed); const auto run=refreshes(c,hz,60,0.0005); bool steady=true;
            for (std::size_t i=1;i<run.times.size();++i)
                steady=steady && run.counts[i]<=3*speed && (run.times[i]<1 || (run.counts[i]+1>=run.counts[i-1] && run.counts[i]<=run.counts[i-1]+1));
            check(steady && std::abs(run.rate(speed))<fast_forward_slew,"90 and 50 Hz: counts change by at most one, rate within the slew");
        }
        // A caller that polls is not pulled to the edge of the slew.
        {
            auto c=accelerated_clock(speed); Paced run;
            for (int ms=1;ms<=60000;++ms) run.add(ms/1000.0,c.due(ms/1000.0));
            check(std::abs(run.rate(speed))<0.0005,"1 ms polling loop within 0.05% of nominal");
        }
        // The play() loop at 144 Hz may follow the screen's rhythm, within the slew.
        {
            auto c=accelerated_clock(speed); const auto run=host_loop(c,144,60);
            check(std::abs(run.rate(speed))<fast_forward_slew,"144 Hz host loop within the slew");
            auto locked=accelerated_clock(speed); const auto at60=host_loop(locked,60,60); unsigned off=0;
            for (std::size_t i=0;i<at60.times.size();++i) off+=at60.times[i]>1 && at60.counts[i] && at60.counts[i]!=speed;
            check(off==0,"60 Hz host loop: every picture carries the speed");
        }
        // One pass 10 ms late carries the catch-up and the next one less; the
        // outlier count is not held, so the third pass is even again.
        {
            auto c=accelerated_clock(speed); const auto run=refreshes(c,60,30,0,600,0.010); bool back=true;
            for (std::size_t i=601;i<run.counts.size();++i) back=back && run.counts[i]==speed;
            check(back && run.off(speed)==2,"a late pass upsets only itself and the next pass");
        }
        // Stalls behave as before: capped catch-up and a rebased deadline.
        for (double stall:{0.5,30.0}) {
            auto c=accelerated_clock(speed); auto o=old_accelerated_clock(speed);
            refreshes(c,60,5,0); refreshes(o,60,5,0);
            const double t=5+stall, before=c.discarded_seconds();
            check(c.due(t)==3*speed && o.due(t)==3*speed && c.deadline()>t &&
                  std::abs(c.discarded_seconds()-before-o.discarded_)<frame_seconds/speed,"accelerated stall: capped catch-up and rebase as before");
            bool back=true; for (int k=1;k<120;++k) { const auto n=c.due(t+k/60.0+0.0003); back=back && n==speed; }
            check(back,"accelerated stall: constant count again from the next pass");
        }
        // Resume, a speed change and the lag rebase forget the previous count.
        {
            auto c=accelerated_clock(speed); refreshes(c,60,5,0); c.rebase(100);
            check(c.due(100)==1 && c.due(100)==0 && c.deadline()>100,"accelerated resume runs one frame at once");
        }
    }
    const double f2=frame_seconds/2,f4=frame_seconds/4;
    auto counting_one=[] { auto c=accelerated_clock(2); refreshes(c,120,5,0); return c; }; // last two calls returned 1
    { auto c=counting_one(); c.rebase(100); check(c.due(100+f2+1e-6)==2,"rebase forgets the previous count"); }
    { auto c=counting_one(); c.speed(4,100); check(c.due(100+2*f4+1e-6)==2,"speed change forgets the previous count"); }
    { auto c=counting_one(); c.due(100); check(c.due(100+2*f2+1e-6)==2,"lag rebase forgets the previous count"); }
}
void speed_indicator() {
    SpeedIndicator indicator;
    check(indicator.label(10).empty(),"no speed indicator at session start");
    indicator.press(2,10);
    check(indicator.label(10)=="2X","2X immediately after a press");
    check(indicator.label(11.999)=="2X","speed indicator lasts almost two seconds");
    check(indicator.label(12).empty(),"speed indicator expires at two seconds");
    check(indicator.label(13).empty(),"speed indicator stays expired");
    indicator.press(4,14);
    check(indicator.label(14)=="4X","4X immediately after a press");
    indicator.press(1,15);
    check(indicator.label(15)=="1X","1X immediately after a press");
    check(indicator.label(16)=="1X" && indicator.label(16.999)=="1X","another press restarts the timer");
    check(indicator.label(17).empty(),"speed indicator expires two seconds after the latest press");
    indicator.press(4,18);
    indicator.hide();
    check(indicator.label(18.1).empty(),"hiding clears an active speed indicator");
    indicator.press(2,18.2);
    check(indicator.label(18.2)=="2X","a new press shows the indicator after hiding it");
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
    for (unsigned speed:{2u,4u}) for (int refresh:{50,60,144}) {
        FrameClock accelerated(0); accelerated.speed(speed,0); std::uint64_t frames=0;
        for (int tick=0;tick<=60*refresh;++tick) frames+=accelerated.due(static_cast<double>(tick)/refresh);
        // Issue 7: 2x and 4x keep step with the screen, within the slew.
        check(std::abs(frames*frame_seconds/(60*speed)-1)<fast_forward_slew && accelerated.discarded_seconds()==0,
              "accelerated clock within the slew of nominal at any display refresh");
        check(accelerated.due(1000)==3*speed && accelerated.deadline()>1000,"accelerated catch-up bounded");
        accelerated.speed(1,1000); check(accelerated.due(1000)==0 && accelerated.due(1000+frame_seconds+1e-8)==1,"speed change rebases without burst");
    }
    SpeedShortcut quick;
    check(!quick.update(true,false,false,true),"arm quick shortcut");
    check(quick.keyboard_event(true,true,true) && !quick.keyboard_event(false,true,true),"quick keyboard tap is observed");
    check(!quick.update(true,false,false,true),"quick event is not counted again by snapshot");
    check(quick.shoulder_event(true,true,true) && !quick.shoulder_event(false,true,true),"quick shoulder tap is observed");
    check(!quick.update(true,false,false,true),"quick shoulder event is not counted again by snapshot");
    quick.focus_lost();check(!quick.keyboard_event(true,true,true),"held key after focus event remains disarmed");
    SpeedShortcut shortcut;
    check(!shortcut.update(true,false,false,true),"arm shortcuts on release");
    check(shortcut.update(true,true,false,true) && !shortcut.update(true,true,false,true),"keyboard press only once");
    check(shortcut.update(true,true,true,true),"independent shoulder press");
    check(!shortcut.update(false,false,false,true) && !shortcut.update(true,true,true,true),"focus regain held does not trigger");
    check(!shortcut.update(true,false,false,true) && shortcut.update(true,false,true,true),"released shoulder rearms");
    shortcut.controller_changed(); check(!shortcut.update(true,false,true,true),"reconnected held shoulder does not trigger");
    shortcut.update(true,false,false,true); shortcut.focus_lost();
    check(!shortcut.update(true,true,true,true),"queued focus loss and regain cannot arm held sources");
    check(!shortcut.update(true,false,false,false) && !shortcut.update(true,true,true,true),"disabled shortcut requires release");
    // Fake a full queue, let only the device clock advance until the
    // producer can append its next frame. No queued output is discarded.
    double depth=audio_high_water_seconds+frame_seconds;
    unsigned waited=0;
    while (const auto delay=AudioQueuePolicy::wait_ms(depth)) {
        waited+=delay;depth-=delay/1000.0;
        check(waited<100,"bounded wait while device drains");
    }
    check(waited>0 && depth>audio_target_seconds,"high water retains the existing stream");
    check(AudioQueuePolicy::underrun(0) && !AudioQueuePolicy::wait_ms(0),"empty queue accepts next real sample immediately");
    for (double device_error:{-0.002,0.002}) {
        double queue=audio_target_seconds;
        for (int frame=0;frame<11000;++frame) {
            queue+=frame_seconds/rate_adjustment(queue)-frame_seconds*(1+device_error);
            check(queue>0.02 && queue<audio_high_water_seconds,"queue remains bounded with device clock mismatch");
        }
    }
}
// Command-line flags: ranges, rejected forms, refused combinations, explicit 100.
void flags() {
    auto parse=[](std::vector<const char*> words) {
        words.insert(words.begin(),{"psmodern.exe","game.sms"});
        return parse_app_arguments(static_cast<int>(words.size()),words.data());
    };
    auto message=[&](std::vector<const char*> words) {
        try { parse(words); } catch (const std::runtime_error& e) { return std::string(e.what()); }
        return std::string();
    };
    auto rejects=[&](std::vector<const char*> words,const char* expected) {
        const auto text=message(words);
        check(!text.empty() && text.find(expected)!=std::string::npos,expected);
    };
    const auto original=parse({});
    check(original.mode==AppMode::play,"a ROM path means play mode");
    {
        const char* none[]={"psmodern.exe"};
        const auto launcher=parse_app_arguments(1,none);
        check(launcher.mode==AppMode::launcher && launcher.rom.empty(),"no arguments means the start screen");
        const char* version[]={"psmodern.exe","--version"};
        check(parse_app_arguments(2,version).mode==AppMode::version,"--version alone prints the version");
        const char* extra[]={"psmodern.exe","--version","game.sms"};
        const char* first[]={"psmodern.exe","game.sms","--version"};
        for (auto* words:{extra,first}) refuses([&] { parse_app_arguments(3,words); },"--version with anything else is refused");
        // utf8_arguments hands main UTF-8; the paths must reach the file system unchanged.
        const char* unicode[]={"psmodern.exe","C:/Jogos/\xc3\xa7\xc3\xa3o/\xe6\x97\xa5\xe6\x9c\xac/ps.sms","--record","\xc3\xa9.psrec"};
        const auto parsed=parse_app_arguments(4,unicode);
        check(parsed.rom==std::filesystem::path(u8"C:/Jogos/\u00e7\u00e3o/\u65e5\u672c/ps.sms") &&
              parsed.record_path==std::filesystem::path(u8"\u00e9.psrec"),"non-ASCII arguments are read as UTF-8");
        const auto back=parsed.rom.u8string();
        check(std::string(reinterpret_cast<const char*>(back.data()),back.size())==unicode[1],"non-ASCII argument round trip");
#ifdef _WIN32
        check(parsed.rom.native()==L"C:/Jogos/\u00e7\u00e3o/\u65e5\u672c/ps.sms","non-ASCII argument becomes the right wide path");
#endif
    }
    check(original.options==game::Options{} && !original.record_path && original.rom=="game.sms","no flags means original");
    for (const auto* value:{"0","25","50","100","200"}) check(parse({"--encounter-rate",value}).options.encounter_percent==std::stoul(value),"encounter values");
    for (const auto* value:{"100","200","400"}) check(parse({"--experience-rate",value}).options.experience_percent==std::stoul(value),"experience values");
    const auto both=parse({"--experience-rate","400","--encounter-rate","50"});
    check(both.options==game::Options{50,400},"both flags, any order");
    check(parse({"--encounter-rate","050"}).options.encounter_percent==50,"leading zeros are plain digits");
    for (const auto* flag:{"--encounter-rate","--experience-rate"}) {
        rejects({flag},"needs a value");
        for (const auto* bad:{"","-5","+5","5%","1.5"," 50","50 ","abc","0x10","--record"}) rejects({flag,bad},"whole number of percent");
        rejects({flag,"99999999999999999999999"},"percent, got");
        rejects({flag,"4294967296"},"percent, got");
        rejects({flag,"200",flag,"200"},"more than once");
        rejects({flag,"100",flag,"100"},"more than once");
    }
    // Values that used to be accepted (ranges) are refused, with the allowed values in the message.
    for (const auto* old:{"1","24","26","99","101","150","400","401"}) rejects({"--encounter-rate",old},"must be one of 0, 25, 50, 100 or 200 percent");
    for (const auto* old:{"0","25","50","99","101","300","1000","1001"}) rejects({"--experience-rate",old},"must be one of 100, 200 or 400 percent");
    rejects({"--bug-fixes","on"},"Usage:");
    rejects({"--bogus"},"Usage:");
    // Milestone 14: --no-routines (the game on the Z80 core) is gone; it is an
    // unknown argument now, and the usage no longer offers it.
    rejects({"--no-routines"},"Usage:");
    rejects({"--record","out.psrec","--no-routines"},"Usage:");
    check(app_usage().find("--no-routines")==std::string::npos,"the usage does not offer --no-routines");
    // Non-default values are refused with --record, each flag on its own.
    for (const auto* flag:{"--encounter-rate","--experience-rate"}) {
        const char* value=std::string_view(flag)=="--encounter-rate" ? "50" : "400";
        rejects({"--record","out.psrec",flag,value},"cannot be combined with --record");
        rejects({flag,value,"--record","out.psrec"},"cannot be combined with --record");
        // Explicit 100 is original mode: accepted wherever no flag is, and identical to no flag.
        const auto same=parse({"--record","out.psrec",flag,"100"});
        check(same.options==game::Options{} && same.record_path==std::filesystem::path("out.psrec"),"explicit 100 with refused combinations");
    }
    check(parse({"--encounter-rate","100","--experience-rate","100"}).options==original.options,"explicit 100 equals no flags");
    // Options reach registration: the driver passes them on, so a value outside its range is refused there.
    Temporary folder;
    refuses([&] { FrameDriver(program(),SaveStore(folder.path,RomVersion::us_rev_a),game::Options{100,0}); },"driver validates options");
    for (const auto& options:{game::Options{},game::Options{50,400}}) {
        FrameDriver driver(program(),SaveStore(folder.path,RomVersion::us_rev_a),options);
        check(driver.routines().empty(),"unidentified image keeps an empty registry");
    }
    // Same registry contents with explicit 100 as without flags (identified image).
    RoutineRegistry plain,explicit_100;
    const auto digest="07fcf297be4f4c9d92cd3f119a7ac48467e06838";
    check(game::register_routines(plain,digest) && game::register_routines(explicit_100,digest,parse({"--encounter-rate","100","--experience-rate","100"}).options),"identified image registers");
    check(plain.entries().size()==explicit_100.entries().size(),"explicit 100 registers the same routines");
    for (const auto& [entry,routine]:plain.entries()) {
        const auto* other=explicit_100.find(entry);
        check(other && other->spec.unit==routine.spec.unit && std::equal(other->spec.exits.begin(),other->spec.exits.end(),routine.spec.exits.begin(),routine.spec.exits.end(),[](const SegmentExit& x,const SegmentExit& y){return x.same_declaration(y);}),"explicit 100 registers the same segments");
    }
}
}
int main() {
    // The authored programs' code that no test renders runs on the oracle's core.
    oracle::Scaffold scaffold;
    try { colours(); scaling(); canvas_windows(); compositing(); inputs(); saves(); backups(); driver(); flags(); guard(); observed_segments();
        frame_end_save(); game_stop(); hooks_at_depth(); automap_files(); unavailable_fm(); resampling(); speed_indicator(); pacing(); screen_pacing();
        std::cout<<"App test groups passed (no SDL or ROM).\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
