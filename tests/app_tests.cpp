// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "app/automap_store.h"
#include "app/host.h"
#include "game/routines.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
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
// The host-observed instructions stay native: activating routines fails at
// once when any of them is a registered entry or a declared exit address.
void guard() {
    check(host_observed_instructions.size()==13,"thirteen observed instructions");
    for (const auto address:{0x00af,0x0055,0x0085,0x0b14,0x0f48,0x2d93,0x02ed,0x02f5,0x1d2d,0x06b4,0x05f4})
        check(std::count(host_observed_instructions.begin(),host_observed_instructions.end(),RoutineEntry{0,static_cast<std::uint16_t>(address)})==1,"bank 0 instruction observed");
    for (const auto address:{0x8043,0x82ef})
        check(std::count(host_observed_instructions.begin(),host_observed_instructions.end(),RoutineEntry{12,static_cast<std::uint16_t>(address)})==1,"tempo audio instruction observed");
    const auto bound=[](const Machine&){return 10u;};
    const auto body=[](RoutineContext& c){c.finish_return(0);};
    RoutineRegistry clean; clean.add({0,0x1000},bound,body);
    clean.add({1,0x6000},SegmentSpec{{1,0x6000},{SegmentExit{RoutineExit::core_final,0x6010}}},bound,body);
    guard_observed_instructions(clean);
    for (const auto watched:host_observed_instructions) {
        RoutineRegistry entry; entry.add(watched,bound,body);
        bool refused=false;
        try { guard_observed_instructions(entry); } catch (const std::logic_error& e) {
            refused=std::string(e.what()).find("registered entry")!=std::string::npos;
        }
        check(refused,"an observed instruction registered as an entry is refused");
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
        unsigned dungeon_entries=0,dungeon_exits=0;
        for (const auto& [entry,routine]:real.entries())
            if (entry.bank==1 && entry.address>=0x65ee && entry.address<0x6c06) { ++dungeon_entries; dungeon_exits+=static_cast<unsigned>(routine.spec.exits.size()); }
        check(dungeon_entries==53 && dungeon_exits>dungeon_entries,"the 53 dungeon segments are registered");
        for (const RoutineEntry entry:{RoutineEntry{1,0x65ee},RoutineEntry{1,0x661c},RoutineEntry{1,0x681b},RoutineEntry{1,0x68bc},
                                        RoutineEntry{1,0x6963},RoutineEntry{1,0x6bca},RoutineEntry{1,0x6be9}})
            check(real.find(entry)!=nullptr,"a dungeon routine entry is registered");
        guard_observed_instructions(real);
    }
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
        check(std::abs(frames*frame_seconds-60*speed)<frame_seconds*speed && accelerated.discarded_seconds()==0,
              "accelerated clock independent of display refresh");
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
    for (double device_error:{-0.002,0.002}) {
        double queue=audio_target_seconds;
        for (int frame=0;frame<11000;++frame) {
            queue+=frame_seconds/rate_adjustment(queue)-frame_seconds*(1+device_error);
            check(queue>0.02 && queue<0.12,"queue remains bounded with device clock mismatch");
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
    check(original.options==game::Options{} && original.routines_enabled && !original.record_path && original.rom=="game.sms","no flags means original");
    for (const auto* value:{"0","50","100","200"}) check(parse({"--encounter-rate",value}).options.encounter_percent==std::stoul(value),"encounter values");
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
    for (const auto* old:{"1","25","99","101","150","400","401"}) rejects({"--encounter-rate",old},"must be one of 0, 50, 100 or 200 percent");
    for (const auto* old:{"0","25","50","99","101","300","1000","1001"}) rejects({"--experience-rate",old},"must be one of 100, 200 or 400 percent");
    rejects({"--bogus"},"Usage:");
    // Non-default values are refused with --no-routines and --record, each flag on its own.
    for (const auto* flag:{"--encounter-rate","--experience-rate"}) {
        const char* value=std::string_view(flag)=="--encounter-rate" ? "50" : "400";
        rejects({"--no-routines",flag,value},"cannot be combined with --no-routines");
        rejects({flag,value,"--no-routines"},flag);
        rejects({"--record","out.psrec",flag,value},"cannot be combined with --record");
        rejects({flag,value,"--record","out.psrec","--no-routines"},"cannot be combined with --no-routines");
        // Explicit 100 is original mode: accepted wherever no flag is, and identical to no flag.
        const auto same=parse({"--no-routines","--record","out.psrec",flag,"100"});
        check(same.options==game::Options{} && !same.routines_enabled && same.record_path==std::filesystem::path("out.psrec"),"explicit 100 with refused combinations");
    }
    check(parse({"--encounter-rate","100","--experience-rate","100"}).options==original.options,"explicit 100 equals no flags");
    // Options reach registration: the driver passes them on, so a value outside its range is refused there.
    Temporary folder;
    refuses([&] { FrameDriver(program(),SaveStore(folder.path,RomVersion::us_rev_a),true,game::Options{100,0}); },"driver validates options");
    for (const auto& options:{game::Options{},game::Options{50,400}}) {
        FrameDriver driver(program(),SaveStore(folder.path,RomVersion::us_rev_a),true,options);
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
    try { colours(); scaling(); inputs(); saves(); backups(); driver(); flags(); guard(); automap_files(); resampling(); pacing();
        std::cout<<"App: 11 test groups passed (no SDL or ROM).\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
