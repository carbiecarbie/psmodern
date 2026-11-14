// SPDX-License-Identifier: GPL-3.0-or-later
// The automap in the frame driver (milestone 7, parts M2 and I) on the
// player's ROM. Arguments: <rom> <us|br> <recordings folder> <reference
// folder> <work folder> <part> [argument] (exit 77 without the ROM; the work
// folder is this test's own). Parts:
//   session            screen, admission, hold, persistence and an unusable
//                      file on first-dungeon (two drivers, two threads);
//   passivity <stem>   one recording played by four drivers on four threads:
//                      recording mode (no tracker), the option off, the option
//                      on and never opened, and routines off; machines equal at
//                      every frame, map events (samples with their values and
//                      cycles, stage, replace, reset) and explorations equal
//                      with routines on and off;
//   wrong-map          (US) the two paths that would show a wrong map (a new
//                      game or a Continue without a record after exploring)
//                      and a Continue of another recorded game, one driver;
//   chain <link>       the three walk recordings in one save folder, one per
//                      link (1-3, in order): each Continue finds the record
//                      the previous walk saved;
//   frames <folder>    inspection frames from the walk recordings (not a test).
// The walk recordings need their start images in the reference folder.
//
// Opening the map is not invisible to the game: like the Options window it
// runs and cancels the game's own field menu, a legal original action at the
// same wait. Passivity is therefore proven with the option on but the map
// never opened; the hold is compared with the Options hold.
#include "app/automap_screen.h"
#include "app/automap_store.h"
#include "app/frame_driver.h"
#include "replay/recording.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <algorithm>
#include <functional>
#include <thread>
#include "replay/checkpoint.h"
#include "replay/verification.h"

using namespace psmodern;
using app::HostMenu;
using app::MapEvent;
namespace dungeon=game::dungeon;
namespace {
void check(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
struct Setup {
    std::shared_ptr<const Memory::Rom> rom;
    RomVersion version;
    std::string region;
    std::filesystem::path recordings,reference,work;
};
std::filesystem::path fresh(const Setup& s,const std::string& name) {
    auto folder=s.work/name;
    std::filesystem::remove_all(folder);std::filesystem::create_directories(folder);
    return folder;
}
replay::Recording recording(const Setup& s,const std::string& stem) {
    auto result=replay::read(s.recordings/(stem+".psrec"));
    check(result.rom_sha1==sha1(*s.rom),stem+": recording made with another ROM");
    return result;
}
// The recording's start image as the save of a fresh folder.
void place_image(const Setup& s,const std::string& stem,const replay::Recording& rec,const std::filesystem::path& folder) {
    const auto image=s.reference/s.region/stem/"cart-ram.bin";
    if(sha1(Memory::CartridgeRam{})==rec.cart_ram_sha1)return;
    check(std::filesystem::exists(image),stem+": start image missing");
    std::filesystem::copy_file(image,folder/app::save_name(s.version));
}
std::array<std::uint8_t,dungeon::game_block_bytes> block(const Machine& m) {
    std::array<std::uint8_t,dungeon::game_block_bytes> bytes{};
    for(unsigned i=0;i<bytes.size();++i)bytes[i]=m.peek(static_cast<std::uint16_t>(dungeon::game_block+i));
    return bytes;
}
game::MapView view_now(const app::FrameDriver& d) {
    const auto& m=const_cast<app::FrameDriver&>(d).machine();
    game::DungeonLayout layout{};
    for(unsigned i=0;i<layout.size();++i)layout[i]=m.peek(static_cast<std::uint16_t>(dungeon::layout+i));
    const auto floor=m.peek(dungeon::floor);
    return game::build_view(*d.dungeon_tables(),d.exploration()->floors[floor],floor,layout,m.peek(dungeon::position),
                            static_cast<std::uint8_t>(m.peek(dungeon::facing)&3),block(m));
}
void write_ppm(const std::filesystem::path& path,std::span<const std::uint32_t> pixels) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n256 192\n255\n";
    for(auto pixel:pixels)for(unsigned shift:{16u,8u,0u})f.put(static_cast<char>((pixel>>shift)&255));
}
// The 256-column centre the host shows during a map hold (main.cpp): the
// held picture, then the map over it. The bands of a wide canvas go round
// this centre and the map never reaches them (milestone 10).
std::array<std::uint32_t,256*192> screen(const Vdp::Picture& background,const app::RomFont& font,const game::MapView& view) {
    std::array<std::uint32_t,256*192> pixels{};
    app::centre_pixels(pixels,background,nullptr);
    app::render_automap(pixels,font,view);
    return pixels;
}
// Per-frame equality: the CPU, cycles, work RAM, cartridge RAM, VRAM, CRAM
// and picture every frame, the machine's complete comparison every 32 frames
// and at the end of each phase.
bool same(const Machine& x,const Machine& y,std::uint64_t frame) {
    const auto& p=x.cpu();const auto& q=y.cpu();
    if(x.cycles()!=y.cycles() || p.pc.uint16_value!=q.pc.uint16_value || p.sp.uint16_value!=q.sp.uint16_value ||
       p.af.uint16_value!=q.af.uint16_value || p.bc.uint16_value!=q.bc.uint16_value || p.de.uint16_value!=q.de.uint16_value ||
       p.hl.uint16_value!=q.hl.uint16_value)return false;
    if(x.memory().ram()!=y.memory().ram() || x.memory().cartridge_ram()!=y.memory().cartridge_ram() || x.picture()!=y.picture() ||
       x.vdp().vram()!=y.vdp().vram())return false;
    return frame%32 || x.equivalent(y);
}
std::size_t count(const std::vector<MapEvent>& log,MapEvent::Kind kind) {
    return static_cast<std::size_t>(std::count_if(log.begin(),log.end(),[&](const MapEvent& e){return e.kind==kind;}));
}
void quiet(const app::FrameOutput& out,const std::string& what) { check(!out.save_error,what+": "+out.save_error.value_or("")); }

// One driver played through a recording: the seven manifest components and
// the cycle count after every frame, and the exploration at each change.
struct Trace {
    std::vector<replay::Manifest> frames;
    std::vector<std::uint64_t> cycles;
    std::vector<std::pair<std::uint64_t,game::Exploration>> explorations;
};
using FrameCheck=std::function<void(std::uint64_t)>;
Trace play(app::FrameDriver& d,const replay::Recording& rec,const std::string& what,std::vector<MapEvent>& events,
           const FrameCheck& before={},const FrameCheck& after={}) {
    Trace t;t.frames.reserve(rec.frames);t.cycles.reserve(rec.frames);
    d.log_map_events(&events);
    replay::FrameWrites writes;
    for(std::uint64_t n=0;n<rec.frames;++n) {
        if(before)before(n);
        auto out=d.run(rec.input_at(n));quiet(out,what);
        if(after)after(n);
        t.frames.push_back(replay::manifest(d.machine(),writes.take(n+1,std::move(out.writes))));
        t.cycles.push_back(d.machine().cycles());
        if(const auto* e=d.exploration(); e && (t.explorations.empty() || t.explorations.back().second!=*e))t.explorations.emplace_back(n,*e);
    }
    d.log_map_events(nullptr);
    return t;
}
// Runs independent drivers side by side; each job owns its driver and folder.
void parallel(const std::vector<std::function<void()>>& jobs) {
    std::vector<std::exception_ptr> errors(jobs.size());
    std::vector<std::thread> threads;
    for(std::size_t k=0;k<jobs.size();++k)threads.emplace_back([&,k]{ try { jobs[k](); } catch(...) { errors[k]=std::current_exception(); } });
    for(auto& thread:threads)thread.join();
    for(const auto& error:errors)if(error)std::rethrow_exception(error);
}
// The first unopened chest on a view that the party does not stand on and
// that is not directly ahead (a chest shows once its cell is seen: Maintainer
// decision, 2026-11-11), or nothing.
std::optional<unsigned> distant_chest(const game::MapView& view) {
    for(unsigned cell=0;cell<256;++cell)
        if(view.cells[cell].kind==game::MapKind::chest && cell!=view.party_cell &&
           cell!=static_cast<std::uint8_t>(view.party_cell+view.party_ahead))return cell;
    return std::nullopt;
}
// True right after the dungeon wait's sample, when the view built now is the
// one a hold would build.
bool at_sample(app::FrameDriver& d,const std::vector<MapEvent>& log) {
    return d.machine().peek(dungeon::game_mode)==dungeon::dungeon_mode && !log.empty() &&
           log.back().kind==MapEvent::Kind::sample && log.back().cycles+70000>d.machine().cycles();
}
void same_frames(const Trace& x,const Trace& y,const std::string& what) {
    check(x.frames.size()==y.frames.size(),what+": different frame counts");
    for(std::size_t n=0;n<x.frames.size();++n) {
        if(x.cycles[n]!=y.cycles[n])throw std::runtime_error(what+": cycles differ after frame "+std::to_string(n));
        for(std::size_t k=0;k<x.frames[n].size();++k)if(x.frames[n][k]!=y.frames[n][k])
            throw std::runtime_error(what+": "+replay::manifest_components[k]+" differs after frame "+std::to_string(n));
    }
}

// The window, grid colours, every icon's pixels and the legend, from a known
// view; nothing outside the window changes.
void screen_test(const app::RomFont& font,const std::filesystem::path& frames) {
    using game::MapKind;namespace colors=app::automap_colors;namespace layout=app::automap_layout;
    game::MapView view{};
    const MapKind kinds[]{MapKind::unknown,MapKind::floor,MapKind::wall,MapKind::stairs_up,MapKind::stairs_down,MapKind::door,
                          MapKind::locked_door,MapKind::magic_door,MapKind::chest,MapKind::opened_chest,MapKind::pitfall};
    for(unsigned i=0;i<std::size(kinds);++i)view.cells[16+i].kind=kinds[i];
    view.cells[48]={MapKind::stairs_up,true};view.cells[49]={MapKind::door,true};
    view.cells[80].kind=MapKind::floor;view.party_cell=80;
    check(app::floor_label_text(1)=="FLOOR 1" && app::floor_label_text(12)=="FLOOR 12" && app::floor_label_text(-1)=="FLOOR B1" &&
          app::floor_label_text(-12)=="FLOOR B12" && app::floor_label_text(0).empty(),"floor label text");
    unsigned variant=0;
    for(const std::int8_t ahead:{std::int8_t{-16},std::int8_t{1},std::int8_t{16},std::int8_t{-1}}) {
        view.party_ahead=ahead;
        static constexpr std::int8_t levels[]{1,12,-12,0};   // the label: shortest, longest above, longest below, none
        view.floor_level=levels[variant++];
        std::array<std::uint32_t,256*192> pixels;pixels.fill(0xff123456);
        app::render_automap(pixels,font,view);
        auto at=[&](unsigned cell,int x,int y){return pixels[(layout::grid_y+cell/16*8+y)*256+layout::grid_x+cell%16*8+x];};
        for(int y=0;y<192;++y)for(int x=0;x<256;++x)
            if(x<8||x>=248||y<8||y>=184)check(pixels[y*256+x]==0xff123456,"the map changed a pixel outside its window");
        // Every interior pixel outside the grid, title and legend is black.
        for(int y=16;y<176;++y)for(int x=16;x<240;++x) {
            const bool grid=x>=24 && x<152 && y>=32 && y<160,legend=x>=168 && y>=32 && y<32+14*9+8,title=y>=16 && y<24 && ((x>=24 && x<48) || x>=layout::floor_label_x);
            if(!grid && !legend && !title)check(pixels[y*256+x]==colors::black,"stray pixel in the map window");
        }
        std::array<std::uint32_t,256*192> text{};font.text(text,24,16,"MAP",0xffffffff);
        for(int y=16;y<24;++y)for(int x=24;x<48;++x)check((pixels[y*256+x]==colors::white)==(text[y*256+x]!=0),"title");
        {   // The floor label shares the title row, in the legend's columns, inside the window's border.
            const auto label_text=app::floor_label_text(view.floor_level);
            std::array<std::uint32_t,256*192> label{};font.text(label,layout::floor_label_x,layout::title_y,label_text,0xffffffff);
            for(int y=16;y<24;++y)for(int x=layout::floor_label_x;x<240;++x)check((pixels[y*256+x]==colors::white)==(label[y*256+x]!=0),"floor label");
            check(layout::floor_label_x+8*static_cast<int>(label_text.size())<=240 && layout::floor_label_x>=48,"the label fits the title row beside MAP");
        }
        for(unsigned i=0;i<std::size(kinds);++i) {
            const auto icon=[&]()->std::optional<app::MapIcon> {
                switch(kinds[i]) {
                    case MapKind::stairs_up: return app::MapIcon::stairs_up; case MapKind::stairs_down: return app::MapIcon::stairs_down;
                    case MapKind::door: case MapKind::locked_door: case MapKind::magic_door: return app::MapIcon::door;
                    case MapKind::chest: return app::MapIcon::chest; case MapKind::opened_chest: return app::MapIcon::opened_chest;
                    case MapKind::pitfall: return app::MapIcon::pitfall; default: return std::nullopt;
                }
            }();
            const auto background=app::automap_background(kinds[i]);
            const auto drawing=icon ? app::automap_icon(*icon) : app::IconPixels{};
            for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
                auto expected=drawing[y][x] ? drawing[y][x] : background;
                if(drawing[y][x]==colors::brown && kinds[i]==MapKind::locked_door)expected=colors::grey;
                if(drawing[y][x]==colors::brown && kinds[i]==MapKind::magic_door)expected=colors::magic;
                check(at(16+i,x,y)==expected,"cell colour or icon pixel");
            }
        }
        check(app::automap_background(MapKind::unknown)==colors::black && app::automap_background(MapKind::floor)==colors::floor &&
              app::automap_background(MapKind::wall)==colors::wall && app::automap_background(MapKind::door)==colors::wall,"backgrounds");
        check(at(16+5,4,3)==colors::orange && at(16+6,4,3)==colors::orange && at(16+7,4,3)==colors::orange,"one door drawing: the knob stays orange");
        const auto exit=app::automap_icon(app::MapIcon::exit);
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            check(at(48,x,y)==(exit[y][x] ? exit[y][x] : colors::floor),"exit replaces the stairs icon");
            check(at(49,x,y)==(exit[y][x] ? exit[y][x] : colors::wall),"exit replaces the door icon");
        }
        // The party arrow points along party_ahead: its tip is on that edge.
        const auto party=app::automap_icon(app::MapIcon::party);
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            int sx=x,sy=y;
            if(ahead==1){sx=y;sy=7-x;} else if(ahead==16){sx=7-x;sy=7-y;} else if(ahead==-1){sx=7-y;sy=x;}
            check(at(80,x,y)==(party[sy][sx] ? party[sy][sx] : colors::floor),"party arrow rotation");
        }
        const int tip_x=ahead==1 ? 7 : ahead==-1 ? 0 : 3,tip_y=ahead==-16 ? 0 : ahead==16 ? 7 : 3;
        check(at(80,tip_x,tip_y)==colors::yellow,"party arrow tip faces the party's direction");
        const char* labels[]{"YOU","CHEST","EMPTY","UP","DOWN","EXIT","DOOR","LOCKED","MAGIC","PIT"};
        for(unsigned i=0;i<10;++i) {
            std::array<std::uint32_t,256*192> label{};const int y=layout::legend_y+static_cast<int>(i)*layout::legend_step;
            font.text(label,layout::legend_label_x,y,labels[i],0xffffffff);
            for(int py=y;py<y+8;++py)for(int px=layout::legend_label_x;px<240;++px)
                check((pixels[py*256+px]==colors::white)==(label[py*256+px]!=0),"legend label");
            static constexpr app::MapIcon icons[]{app::MapIcon::party,app::MapIcon::chest,app::MapIcon::opened_chest,app::MapIcon::stairs_up,
                app::MapIcon::stairs_down,app::MapIcon::exit,app::MapIcon::door,app::MapIcon::door,app::MapIcon::door,app::MapIcon::pitfall};
            const auto drawing=app::automap_icon(icons[i]);
            const auto background=i>=6 && i<=8 ? colors::wall : colors::floor;
            for(int py=0;py<8;++py)for(int px=0;px<8;++px) {
                auto expected=drawing[py][px] ? drawing[py][px] : background;
                if(drawing[py][px]==colors::brown && i==7)expected=colors::grey;
                if(drawing[py][px]==colors::brown && i==8)expected=colors::magic;
                check(pixels[(y+py)*256+layout::legend_icon_x+px]==expected,"legend icon");
            }
        }
        if(ahead==-16)write_ppm(frames/"screen-synthetic.ppm",pixels);
    }
}
}
namespace {
struct Timer {
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    double seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); }
};

// Admission, hold and persistence on first-dungeon (a new game, a save inside
// the dungeon, the party left walking): the option off and the option on
// never opened, side by side (passivity against recording mode and routines
// off is the passivity part's).
void session(const Setup& s,const app::RomFont& font,const std::filesystem::path& frames) {
    Timer timer;
    const auto rec=recording(s,"first-dungeon");
    const auto fb=fresh(s,"off"),fc=fresh(s,"on"),fd=fresh(s,"no-routines");
    {
        app::SaveStore store(fc,s.version);store.load_preferences(store.load());
        check(!store.choose({},true,store.load()),"applying the automap option");
    }
    std::vector<std::string> writes;
    auto raw=[&](const std::filesystem::path& p,const Memory::CartridgeRam& image){writes.push_back("sav");app::atomic_save(p,image);};
    auto text=[&](const std::filesystem::path& p,const std::string& t){writes.push_back(p.extension().string());app::atomic_text(p,t);};
    {
        app::FrameDriver raw_z80(s.rom,app::SaveStore(fd,s.version),false);
        check(!raw_z80.automap_tracking() && !raw_z80.request_menu(HostMenu::map),"--no-routines neither tracks nor shows a map");
    }
    app::FrameDriver b(s.rom,app::SaveStore(fb,s.version));
    app::FrameDriver c(s.rom,app::SaveStore(fc,s.version,raw,app::atomic_settings,text));
    check(b.automap_tracking() && !b.automap_enabled() && c.automap_tracking() && c.automap_enabled(),"tracking always on; the option gates the screen");
    std::vector<MapEvent> lb,lc;
    std::set<unsigned> refused_modes;
    std::optional<game::Exploration> at_save;std::string saved_digest;
    std::size_t before=0,stages=0;
    Trace tb,tc;
    parallel({
        [&] { tb=play(b,rec,"option off",lb,[&](std::uint64_t){ check(!b.request_menu(HostMenu::map),"map admitted with the option off"); }); },
        [&] { tc=play(c,rec,"option on",lc,
            [&](std::uint64_t) {
                const auto mode=c.machine().peek(dungeon::game_mode);
                if(mode!=dungeon::dungeon_mode) { check(!c.request_menu(HostMenu::map),"map admitted outside a dungeon");refused_modes.insert(mode); }
                before=writes.size();stages=count(lc,MapEvent::Kind::stage);
            },
            [&](std::uint64_t) {
                if(count(lc,MapEvent::Kind::stage)>stages) {
                    // The first cartridge write after 00:1D2D follows the map's write.
                    check(writes.size()>=before+2 && writes[before]==".automap" && writes[before+1]=="sav","write order at the save: .automap then .sav");
                    at_save=*c.exploration();saved_digest=lc.back().digest;
                }
            }); }});
    same_frames(tb,tc,"option off against option on");
    c.log_map_events(&lc);
    check(lb==lc && count(lc,MapEvent::Kind::sample)>500 && count(lc,MapEvent::Kind::stage)==1 && count(lc,MapEvent::Kind::reset)==1,
          "the same map events with the option off and on");
    check(at_save.has_value() && b.machine().equivalent(c.machine()),"exploration staged at the save");
    const auto written=app::parse_automap(app::read_text_file(c.store().automap_path(),app::automap_max_bytes,"automap"));
    check(written.find(saved_digest) && *written.find(saved_digest)==*at_save && written==app::parse_automap(app::read_text_file(b.store().automap_path(),app::automap_max_bytes,"automap")),
          "the saved game's record is on disk");
    check(app::used_slot_digests(c.machine().memory().cartridge_ram()).count(saved_digest)==1,"the record belongs to a used slot");
    check(refused_modes.size()>=2,"map refused in several non-dungeon modes");
    check(c.machine().peek(dungeon::game_mode)==dungeon::dungeon_mode,"first-dungeon ends in a dungeon");
    const auto passivity=timer.seconds();

    // Mid-step: a turn runs its animation away from the dungeon wait.
    FrameInput turn{};turn.pads[0].left=true;
    const auto facing=c.machine().peek(dungeon::facing);
    unsigned refused=0;bool turned=false;
    for(unsigned n=0;n<120 && !turned;++n) {
        const auto samples=count(lc,MapEvent::Kind::sample);
        const auto& input=n<2 ? turn : FrameInput{};
        if(n>=2) { check(!c.request_menu(HostMenu::map),"map admitted mid-turn");++refused; }
        quiet(b.run(input),"option off");quiet(c.run(input),"option on");
        turned=n>=2 && count(lc,MapEvent::Kind::sample)>samples && c.machine().peek(dungeon::facing)!=facing;
    }
    check(turned && refused>10,"the turn ran without admitting the map");

    // The hold: the map (C) against the Options window (B) from equal machines.
    bool admitted=false;
    for(unsigned n=0;n<120 && !admitted;++n) {
        check(same(b.machine(),c.machine(),n),"machines equal before the hold");
        if(b.request_menu()) {
            check(!b.request_menu(HostMenu::map),"option off: the map stays closed");
            check(c.request_menu(HostMenu::map) && !c.request_menu(HostMenu::options),"map admitted at the dungeon wait, one request per frame");
            admitted=true;break;
        }
        quiet(b.run({}),"option off");quiet(c.run({}),"option on");
    }
    check(admitted,"the dungeon wait admitted the hold");
    const auto background=c.machine().picture();
    const auto ram=c.machine().memory().ram();
    quiet(b.run({}),"options hold");quiet(c.run({}),"map hold");
    check(b.menu_active() && b.menu_purpose()==HostMenu::options && c.menu_active() && c.menu_purpose()==HostMenu::map,"holds began");
    check(c.map_view() && *c.map_view()==view_now(c),"the view is build_view of the held state");
    write_ppm(frames/(s.region+"-first-dungeon-hold.ppm"),screen(background,font,*c.map_view()));
    for(unsigned n=0;n<120;++n) {
        FrameInput hostile{};hostile.pause=true;hostile.reset=true;hostile.pads[0].up=hostile.pads[0].one=true;
        quiet(b.run(hostile),"options hold");quiet(c.run(hostile),"map hold");
        check(c.menu_active() && !c.request_menu(HostMenu::map) && !c.request_menu(),"the hold keeps its native call");
        check(c.machine().peek(dungeon::game_mode)==dungeon::dungeon_mode,"no encounter or scene while the map is open");
        for(unsigned i=0x300;i<0x800;++i)check(c.machine().memory().ram()[i]==ram[i],"dungeon state changed while the map was open");
    }
    b.close_menu();c.close_menu();
    unsigned closing=0;
    while((b.menu_active() || c.menu_active()) && closing++<100) {
        check(b.menu_active()==c.menu_active(),"map and Options close on the same frame");
        quiet(b.run({}),"options close");quiet(c.run({}),"map close");
    }
    check(!c.menu_active(),"the map closed");
    for(unsigned n=0;n<20;++n) { quiet(b.run({}),"after options");quiet(c.run({}),"after map"); }
    check(b.machine().equivalent(c.machine()),"the machine after the map hold equals the machine after the Options hold");
    check(c.machine().peek(0xc204)==0 && c.machine().peek(0xc205)==0 && c.machine().peek(dungeon::game_mode)==dungeon::dungeon_mode,
          "input released and play continues in the dungeon");
    // Options open: the map key is ignored; play continues afterwards.
    admitted=false;
    for(unsigned n=0;n<120 && !admitted;++n) { admitted=c.request_menu(); quiet(c.run({}),"options"); }
    check(admitted && c.menu_active() && c.menu_purpose()==HostMenu::options && !c.request_menu(HostMenu::map),"map refused while Options is open");
    c.close_menu();for(unsigned n=0;n<100 && c.menu_active();++n)quiet(c.run({}),"options close");
    const auto direction=c.machine().peek(dungeon::facing);
    for(unsigned n=0;n<30;++n)quiet(c.run(n<2 ? turn : FrameInput{}),"turn after the hold");
    check(c.machine().peek(dungeon::facing)!=direction,"dungeon controls resume after the hold");
    const auto hold=timer.seconds()-passivity;

    // Persistence: F5 loses unsaved exploration; Continue restores the saved
    // record (deep-dungeon's own inputs Continue on slot 1).
    const auto before_f5=*c.exploration();
    check(!before_f5.empty() && before_f5!=*at_save,"exploration after the save is not in the saved record");
    check(!c.power_cycle() && c.exploration()->empty() && lc.back().kind==MapEvent::Kind::reset,"power cycle clears the map");
    const auto cont=recording(s,"deep-dungeon");
    bool restored=false;
    for(std::uint64_t n=0;n<3000 && !restored;++n) {
        const auto events=lc.size();
        quiet(c.run(cont.input_at(n)),"continue");
        for(auto k=events;k<lc.size();++k)if(lc[k].kind==MapEvent::Kind::replace) {
            check(lc[k].digest==saved_digest && lc[k].found,"Continue found the saved game's record");
            check(k+1==lc.size() && *c.exploration()==*at_save,"Continue restores the saved exploration");
            restored=true;
        }
    }
    check(restored,"Continue reached 00:06B4");
    std::cout<<s.region<<": unsaved exploration "<<(before_f5==*at_save ? "equal to" : "beyond")<<" the saved record before F5.\n";
    // A new game clears the map at 00:05F4.
    check(!c.power_cycle(),"second power cycle");
    bool cleared=false;
    for(std::uint64_t n=0;n<1500 && !cleared;++n) {
        const auto events=lc.size();
        quiet(c.run(rec.input_at(n)),"new game");
        for(auto k=events;k<lc.size();++k)cleared|=lc[k].kind==MapEvent::Kind::reset && lc[k].cycles>0;
    }
    check(cleared && c.exploration()->empty(),"a new game resets the map at 00:05F4");
    const auto persistence=timer.seconds()-passivity-hold;
    std::cout<<s.region<<": first-dungeon "<<rec.frames<<" frames x2 in "<<passivity<<" s, hold "<<hold<<" s, persistence "<<persistence<<" s.\n";
}

// An unusable .automap: a warning, an empty map at Continue, the file kept
// until a save preserves it as .automap.bak (app_tests covers the save);
// recording mode and --no-routines never read it.
void unusable_file(const Setup& s) {
    const auto rec=recording(s,"deep-dungeon");
    const auto folder=fresh(s,"corrupt");
    place_image(s,"deep-dungeon",rec,folder);
    const std::string bad="psmodern-automap 9\nrecord nothing\n";
    const auto path=folder/(std::string(app::save_name(s.version))+".automap");
    { std::ofstream f(path,std::ios::binary);f<<bad; }
    for(const bool routines:{false,true}) {
        app::FrameDriver d(s.rom,app::SaveStore(folder,s.version),routines,{},routines);
        check(!d.store().automap_warning(),"recording mode and --no-routines never read the file");
    }
    app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
    check(d.store().automap_warning().has_value(),"an unusable file warns at start");
    std::vector<MapEvent> log;d.log_map_events(&log);
    bool continued=false;
    for(std::uint64_t n=0;n<3000 && !continued;++n) {
        quiet(d.run(rec.input_at(n)),"continue with an unusable file");
        continued=count(log,MapEvent::Kind::replace)>0;
    }
    check(continued && !log.back().found && d.exploration()->empty(),"unusable file: empty map at Continue");
    auto backup=path;backup+=".bak";
    check(app::read_text_file(path,app::automap_max_bytes,"automap")==bad && !std::filesystem::exists(backup),"unusable file kept while nothing is saved");
}

// The two paths that would make the map wrong rather than empty, in one
// driver that never power cycles: the console's reset button (read by the
// frame interrupt, which restarts the program at its setup; the cartridge RAM
// stays) takes the party back to the title between phases. US walk
// recordings; every one of them Continues and saves on slot 1. The folder
// holds walk-1's start image (game W on slot 1, game G on slot 2, which no
// recording touches) and a .automap with a record for G only.
//   1. walk-1: Continue W (no record), explore, save game A on slot 1;
//   2. reset, first-dungeon's inputs: the new game clears the map at 00:05F4;
//   3. reset, walk-2's inputs: Continue A (its record exactly), then explore
//      beyond it, stopped before walk-2's own save;
//   4. reset, G's saved game copied to slot 1: Continue G, a different
//      recorded game, gives exactly G's record;
//   5. reset, one byte of slot 1's saved game changed (money; the game accepts
//      an edited save): Continue finds no record, and the map is empty, not
//      G's.
// Without the reset at 00:05F4 phase 2 fails; without the reset when
// 00:06B4 finds no record phase 5 fails.
void wrong_map(const Setup& s) {
    Timer timer;
    const auto walk1=recording(s,"dungeon-walk-1"),walk2=recording(s,"dungeon-walk-2"),first=recording(s,"first-dungeon");
    const auto folder=fresh(s,"wrong-map");
    place_image(s,"dungeon-walk-1",walk1,folder);
    Memory::CartridgeRam image{};
    {
        std::ifstream file(folder/app::save_name(s.version),std::ios::binary);
        file.read(reinterpret_cast<char*>(image.data()),static_cast<std::streamsize>(image.size()));
        check(file.gcount()==static_cast<std::streamsize>(image.size()),"walk-1 start image");
    }
    const auto slot_block=[](const Memory::CartridgeRam& cart,unsigned slot) {
        return std::span<const std::uint8_t,dungeon::game_block_bytes>(cart.data()+dungeon::slot_offset(slot),dungeon::game_block_bytes);
    };
    check(image[dungeon::slot_used_flag(1)]!=0 && image[dungeon::slot_used_flag(2)]!=0,"walk-1 starts with slots 1 and 2 used");
    const auto digest_w=app::automap_digest(slot_block(image,1)),digest_g=app::automap_digest(slot_block(image,2));
    // G's record: any exploration; it stays on disk, since slot 2 stays used.
    game::Exploration record_g{};
    for(unsigned cell=0x40;cell<0x48;++cell)record_g.floors[0].seen.set(cell);
    record_g.floors[dungeon::floor_count-1].found.set(0x11);
    {
        app::AutomapImage file;file.put(digest_g,record_g);
        app::atomic_text(folder/(std::string(app::save_name(s.version))+".automap"),app::write_automap(file));
    }
    app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
    check(d.automap_tracking() && !d.store().automap_warning() && d.exploration()->empty(),"tracking from an empty map");
    std::vector<MapEvent> log;d.log_map_events(&log);
    auto events_of=[&](MapEvent::Kind kind,std::size_t from) {
        std::vector<MapEvent> found;
        for(auto k=from;k<log.size();++k)if(log[k].kind==kind)found.push_back(log[k]);
        return found;
    };
    auto reset_button=[&] {
        FrameInput pressed{};pressed.reset=true;
        for(unsigned n=0;n<4;++n)quiet(d.run(n<2 ? pressed : FrameInput{}),"reset button");
        check(d.machine().peek(dungeon::game_mode)!=dungeon::dungeon_mode,"the reset button left the dungeon");
    };
    // Plays `rec` from its first frame until an event of `kind` (at most 3000
    // frames); returns the frames played and the events.
    auto play_to=[&](const replay::Recording& rec,MapEvent::Kind kind) {
        const auto mark=log.size();
        std::uint64_t n=0;
        while(n<3000 && events_of(kind,mark).empty())quiet(d.run(rec.input_at(n++)),"to the title choice");
        return std::pair{n,events_of(kind,mark)};
    };
    // Writes cartridge RAM through the mapper, as the game itself reaches it
    // (control 08h maps it at 8000h); the control byte and its RAM mirror are
    // put back.
    auto write_cart=[&](unsigned offset,std::span<const std::uint8_t> bytes) {
        auto& memory=d.machine().memory();
        const auto control=memory.read(0xfffc);
        memory.write(0xfffc,0x08);
        for(std::size_t k=0;k<bytes.size();++k)memory.write(static_cast<std::uint16_t>(0x8000+offset+k),bytes[k]);
        memory.write(0xfffc,control);
    };

    // 1. walk-1: Continue W, explore, save A.
    std::optional<game::Exploration> record_a;std::string digest_a;
    for(std::uint64_t n=0;n<walk1.frames;++n) {
        const auto events=log.size();
        quiet(d.run(walk1.input_at(n)),"walk-1");
        for(const auto& e:events_of(MapEvent::Kind::replace,events))check(!e.found && e.digest==digest_w,"walk-1 Continues W, which has no record");
        if(const auto staged=events_of(MapEvent::Kind::stage,events);!staged.empty()) { record_a=*d.exploration();digest_a=staged.back().digest; }
    }
    check(record_a && !record_a->empty() && digest_a!=digest_w && digest_a!=digest_g,"game A explored and saved");
    check(!d.flush(),"game A flushed");
    // 2. Reset, then the new-game path: the map is cleared at 00:05F4.
    {
        const auto left=log.size();
        reset_button();
        check(!d.exploration()->empty(),"the reset button alone keeps the tracker (it is not a power cycle)");
        const auto mark=log.size();
        const auto resets=play_to(first,MapEvent::Kind::reset).second;
        check(!resets.empty() && events_of(MapEvent::Kind::replace,mark).empty(),"the new game passed 00:05F4");
        check(d.exploration()->empty(),"a new game after exploring starts with an empty map");
        // The title's mode after a pass (00:00AF) ended the dungeon walk
        // first: no later sample could pair with the last one for a fake wall.
        const auto leaves=events_of(MapEvent::Kind::leave,left);
        check(leaves.size()==1 && leaves[0].cycles<resets[0].cycles,"leaving the dungeon forgot the previous sample");
    }
    // 3. Reset, Continue A: its record exactly; then explore beyond it.
    reset_button();
    {
        auto [n,replaced]=play_to(walk2,MapEvent::Kind::replace);
        check(replaced.size()==1 && replaced[0].found && replaced[0].digest==digest_a && *d.exploration()==*record_a,
              "Continue of game A restores its record exactly");
        const auto mark=log.size();
        while(n<walk2.frames && *d.exploration()==*record_a && events_of(MapEvent::Kind::stage,mark).empty())
            quiet(d.run(walk2.input_at(n++)),"walk-2");
        check(events_of(MapEvent::Kind::stage,mark).empty() && *d.exploration()!=*record_a,"game A explored beyond its record, not saved");
        // The mode test at 00:0F48: the main loop dispatches on the mode's
        // low five bits, so mode 2Bh runs the dungeon pass too; it is not a
        // dungeon walk and gives no sample. Mode 0Bh again resumes them.
        const auto walking=log.size();
        for(unsigned k=0;k<5;++k)quiet(d.run({}),"walking");
        check(d.machine().peek(dungeon::game_mode)==dungeon::dungeon_mode && events_of(MapEvent::Kind::sample,walking).size()>=4,"walking samples");
        d.machine().memory().write(dungeon::game_mode,0x2B);
        const auto odd=log.size();
        for(unsigned k=0;k<3;++k)quiet(d.run({}),"mode 2Bh");
        check(d.machine().peek(dungeon::game_mode)==0x2B && events_of(MapEvent::Kind::sample,odd).empty(),"no sample outside mode 0Bh at 00:0F48");
        d.machine().memory().write(dungeon::game_mode,dungeon::dungeon_mode);
        const auto again=log.size();
        for(unsigned k=0;k<3;++k)quiet(d.run({}),"mode 0Bh");
        check(!events_of(MapEvent::Kind::sample,again).empty(),"samples resume in mode 0Bh");
    }
    // 4. Reset, G copied to slot 1, Continue: a different recorded game,
    // exactly its record.
    reset_button();
    write_cart(dungeon::slot_offset(1),slot_block(image,2));
    check(app::automap_digest(slot_block(d.machine().memory().cartridge_ram(),1))==digest_g,"slot 1 holds G");
    {
        const auto before=*d.exploration();
        const auto replaced=play_to(walk2,MapEvent::Kind::replace).second;
        check(before!=record_g && replaced.size()==1 && replaced[0].found && replaced[0].digest==digest_g,"Continue found G's record");
        check(*d.exploration()==record_g,"Continue of another recorded game after exploring gives exactly that game's record");
    }
    // 5. Reset, slot 1 edited, Continue: no record, an empty map.
    reset_button();
    {
        const auto offset=dungeon::slot_offset(1)+0x1e0;   // money, low byte ($C4E0)
        const std::uint8_t edited[]{static_cast<std::uint8_t>(d.machine().memory().cartridge_ram()[offset]^1)};
        write_cart(offset,edited);
        const auto digest=app::automap_digest(slot_block(d.machine().memory().cartridge_ram(),1));
        check(!d.exploration()->empty() && digest!=digest_g && digest!=digest_a,"slot 1 holds a game without a record; the map is G's");
        const auto replaced=play_to(walk2,MapEvent::Kind::replace).second;
        check(replaced.size()==1 && !replaced[0].found && replaced[0].digest==digest,"Continue of the edited game found no record");
        check(d.exploration()->empty(),"a Continue without a record after exploring gives an empty map, not the previous one");
    }
    std::cout<<s.region<<": wrong-map paths (a new game, a Continue without a record, a Continue of another recorded game) in one driver; "
             <<timer.seconds()<<" s.\n";
}

// Inspection frames from real game states of the walk recordings: each
// rendered with build_view at the dungeon wait (the same view a hold builds)
// over the current picture, without opening the map, so the replay stays the
// recording's.
void walk_frames(const Setup& s,const app::RomFont& font,const std::filesystem::path& out) {
    const auto folder=fresh(s,"walk");
    struct Shot { std::uint64_t frame; const char* name; };
    struct Walk { const char* stem; std::vector<Shot> shots; };
    const Walk walks[]{
        {"dungeon-walk-1",{{1700,"01-fresh-floor"},{5850,"02-chests-opened"},{6560,"03-on-the-pitfall"},{7120,"04-fake-wall-crossed"},
                           {7990,"05-door-floor"},{9300,"06-walk-1-saved"}}},
        {"dungeon-walk-2",{{1700,"07-continue-restored"},{5600,"08-after-fixed-fight"},{8200,"09-back-on-1f"},{10100,"10-walk-2-saved"}}},
        {"dungeon-continue",{{1700,"11-continue-restored"},{1890,"12-at-the-exit"}}}};
    for(const auto& walk:walks) {
        const auto rec=recording(s,walk.stem);
        // Each walk continues the save the previous one wrote into this
        // folder; the first starts from its own image.
        if(std::string_view(walk.stem)=="dungeon-walk-1")place_image(s,walk.stem,rec,folder);
        app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
        check(sha1(d.machine().memory().cartridge_ram())==rec.cart_ram_sha1,std::string(walk.stem)+": chained start image");
        std::vector<MapEvent> log;d.log_map_events(&log);
        std::size_t shot=0;
        bool chest_shot=std::string_view(walk.stem)!="dungeon-walk-1";
        for(std::uint64_t n=0;n<rec.frames;++n) {
            quiet(d.run(rec.input_at(n)),walk.stem);
            if(!chest_shot && at_sample(d,log))
                if(const auto view=view_now(d);distant_chest(view)) {
                    write_ppm(out/"01b-unopened-chest-seen.ppm",screen(d.machine().picture(),font,view));
                    std::cout<<"01b-unopened-chest-seen: frame "<<n<<" floor "<<int(d.machine().peek(dungeon::floor))<<" chest cell "<<*distant_chest(view)<<"\n";
                    chest_shot=true;
                }
            if(shot<walk.shots.size() && n>=walk.shots[shot].frame && d.machine().peek(dungeon::game_mode)==dungeon::dungeon_mode &&
               !log.empty() && log.back().kind==MapEvent::Kind::sample && log.back().cycles+70000>d.machine().cycles()) {
                write_ppm(out/(std::string(walk.shots[shot].name)+".ppm"),screen(d.machine().picture(),font,view_now(d)));
                std::cout<<walk.shots[shot].name<<": frame "<<n<<" floor "<<int(d.machine().peek(dungeon::floor))<<"\n";
                ++shot;
            }
        }
        for(const auto& e:log)if(e.kind!=MapEvent::Kind::sample)std::cout<<"  "<<walk.stem<<" event "<<int(e.kind)<<" found "<<e.found<<"\n";
        // Save, Continue and power cycle with real files: each Continue after
        // the first finds the record the previous walk saved.
        const bool chained=std::string_view(walk.stem)!="dungeon-walk-1";
        check(std::any_of(log.begin(),log.end(),[&](const MapEvent& e){return e.kind==MapEvent::Kind::replace && e.found==chained;}),
              std::string(walk.stem)+": Continue restored the saved record");
        check(shot==walk.shots.size() && chest_shot,std::string(walk.stem)+": every inspection frame rendered");
    }
}
// A real floor with locked and magic doors, every cell explored: samples
// from every walkable cell in every facing (the tracker's own rule).
void door_frame(const Setup& s,const app::RomFont& font,const std::filesystem::path& out) {
    const game::DungeonTables tables(*s.rom);
    for(unsigned floor=0;floor<dungeon::floor_count;++floor) {
        const auto layout=tables.layout(floor);
        bool locked=false,magic=false;
        for(auto v:layout){locked|=dungeon::kind(v)==dungeon::CellKind::locked_door;magic|=dungeon::kind(v)==dungeon::CellKind::magic_door;}
        if(!locked || !magic)continue;
        game::ExplorationTracker tracker(tables);
        unsigned start=0;
        for(unsigned cell=0;cell<256;++cell)if(dungeon::walkable(layout[cell])) {
            if(!start)start=cell;
            for(std::uint8_t f=0;f<4;++f)tracker.observe({static_cast<std::uint8_t>(floor),static_cast<std::uint8_t>(cell),f,layout});
        }
        const std::array<std::uint8_t,dungeon::game_block_bytes> blank{};
        const auto view=game::build_view(tables,tracker.exploration().floors[floor],static_cast<std::uint8_t>(floor),layout,
                                         static_cast<std::uint8_t>(start),1,blank);
        Vdp::Picture black{};
        write_ppm(out/"13-locked-and-magic-doors.ppm",screen(black,font,view));
        std::cout<<"13-locked-and-magic-doors: floor "<<floor<<" (synthetic full exploration)\n";
        return;
    }
    throw std::runtime_error("no floor with both locked and magic doors");
}

// Tracking changes nothing: one recording played by recording mode (no
// tracker), the option off, the option on and never opened, and routines off
// (Z80 only, with a tracker), each driver on its own thread from its own copy
// of the start image. The four machines are equal after every frame (the
// seven manifest components and the cycle count) and at the end; the three
// trackers log the same map events, values and cycles included, and hold the
// same exploration after every frame.
void passivity(const Setup& s,const std::string& stem) {
    Timer timer;
    const auto rec=recording(s,stem);
    const auto fa=fresh(s,"passivity-"+stem+"-recording"),fb=fresh(s,"passivity-"+stem+"-off"),
               fc=fresh(s,"passivity-"+stem+"-on"),fd=fresh(s,"passivity-"+stem+"-z80");
    for(const auto& folder:{fa,fb,fc,fd})place_image(s,stem,rec,folder);
    for(const auto& folder:{fc,fd}) {
        app::SaveStore store(folder,s.version);store.load_preferences(store.load());
        check(!store.choose({},true,store.load()),"applying the automap option");
    }
    app::FrameDriver a(s.rom,app::SaveStore(fa,s.version),true,{},true);
    app::FrameDriver b(s.rom,app::SaveStore(fb,s.version));
    app::FrameDriver c(s.rom,app::SaveStore(fc,s.version));
    app::FrameDriver d(s.rom,app::SaveStore(fd,s.version));
    // Routines off with a tracker: the registry stays as registered, disabled.
    d.routines().enabled=false;
    check(!a.automap_tracking() && b.automap_tracking() && !b.automap_enabled() && c.automap_enabled() && d.automap_tracking(),
          stem+": recording mode has no tracker; the others track");
    std::vector<MapEvent> la,lb,lc,ld;
    Trace ta,tb,tc,td;
    parallel({[&]{ ta=play(a,rec,stem+" recording",la); },[&]{ tb=play(b,rec,stem+" option off",lb); },
              [&]{ tc=play(c,rec,stem+" option on",lc); },[&]{ td=play(d,rec,stem+" routines off",ld); }});
    same_frames(ta,tb,stem+": recording mode against the option off");
    same_frames(ta,tc,stem+": recording mode against the option on");
    same_frames(ta,td,stem+": routines on against routines off");
    check(a.machine().equivalent(b.machine()) && a.machine().equivalent(c.machine()) && a.machine().equivalent(d.machine()),
          stem+": the machines are equivalent at the end");
    check(la.empty() && ta.explorations.empty() && !std::filesystem::exists(a.store().automap_path()),
          stem+": recording mode neither tracks nor writes a .automap");
    check(lb==lc,stem+": the same map events with the option off and on");
    if(lc!=ld) {
        std::size_t k=0;while(k<lc.size() && k<ld.size() && lc[k]==ld[k])++k;
        throw std::runtime_error(stem+": map events differ with routines on and off at event "+std::to_string(k)+" of "+
                                 std::to_string(lc.size())+" / "+std::to_string(ld.size()));
    }
    check(tb.explorations==tc.explorations && tc.explorations==td.explorations,stem+": the same exploration after every frame, routines on and off");
    check(count(lc,MapEvent::Kind::sample)>0,stem+": the recording reaches the dungeon wait");
    // The comparison means something only if the dungeon segments ran in C++
    // in the routines-on drivers and not at all with routines off.
    auto cpp=[](app::FrameDriver& driver,RoutineEntry entry) {
        for(const auto& [at,routine]:driver.routines().entries())if(at==entry)return routine.counters.cpp_calls;
        throw std::runtime_error("dungeon handler not registered");
    };
    check(cpp(c,{1,0x65EE})>0 && cpp(a,{1,0x65EE})>0 && cpp(d,{1,0x65EE})==0,stem+": the dungeon handler ran in C++ with routines on only");
    std::cout<<s.region<<" "<<stem<<": "<<rec.frames<<" frames x4 equal; "<<lc.size()<<" map events ("<<count(lc,MapEvent::Kind::sample)
             <<" samples, "<<count(lc,MapEvent::Kind::stage)<<" stage, "<<count(lc,MapEvent::Kind::replace)<<" replace, "
             <<count(lc,MapEvent::Kind::reset)<<" reset) and "<<tc.explorations.size()<<" exploration changes equal with routines on and off; "
             <<timer.seconds()<<" s.\n";
}

// Save, Continue and the chained walk recordings with real files in one
// folder, one link per test (ctest fixtures keep the order): link 1 plays
// walk-1 from its image and saves in the dungeon; link 2 plays walk-2, whose
// Continue must restore the record link 1 saved, then saves; link 3 plays
// dungeon-continue, whose Continue must restore link 2's record. Each link
// leaves the digest of the game it saved beside the files. Link 1 also checks
// that an unopened chest shows on the map before the party reaches it.
void chain(const Setup& s,unsigned link) {
    Timer timer;
    const std::string stems[]{"dungeon-walk-1","dungeon-walk-2","dungeon-continue"};
    const auto& stem=stems[link-1];
    const auto folder=s.work/"chain";
    const auto marker=[&](unsigned k){ return folder/("link-"+std::to_string(k)+".digest"); };
    const auto rec=recording(s,stem);
    std::string previous_digest;
    if(link==1) { fresh(s,"chain");place_image(s,stem,rec,folder); }
    else {
        check(std::filesystem::exists(marker(link-1)),stem+": the previous link did not finish");
        previous_digest=app::read_text_file(marker(link-1),64,"digest");
        std::filesystem::remove(marker(link-1));
    }
    const auto on_disk=link==1 ? app::AutomapImage{} :
        app::parse_automap(app::read_text_file(folder/(std::string(app::save_name(s.version))+".automap"),app::automap_max_bytes,"automap"));
    app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
    check(sha1(d.machine().memory().cartridge_ram())==rec.cart_ram_sha1,stem+": starts from the save the previous walk wrote");
    check(d.automap_tracking() && !d.store().automap_warning(),stem+": tracking, the companion file readable");
    std::vector<MapEvent> log;d.log_map_events(&log);
    unsigned replaces=0,stages=0;
    std::optional<game::Exploration> staged;std::string staged_digest;
    std::optional<std::uint64_t> chest_frame;
    for(std::uint64_t n=0;n<rec.frames;++n) {
        const auto events=log.size();
        quiet(d.run(rec.input_at(n)),stem);
        for(auto k=events;k<log.size();++k) {
            const auto& e=log[k];
            if(e.kind==MapEvent::Kind::replace) {
                ++replaces;
                if(link==1) check(!e.found && d.exploration()->empty(),stem+": no record for the start image; empty map");
                else check(e.found && e.digest==previous_digest && on_disk.find(e.digest) && *d.exploration()==*on_disk.find(e.digest),
                           stem+": Continue restores the record the previous walk saved");
            } else if(e.kind==MapEvent::Kind::stage) {
                ++stages;staged=*d.exploration();staged_digest=e.digest;
            } else if(e.kind==MapEvent::Kind::reset) check(!replaces,stem+": no new game after the Continue (frame "+std::to_string(n)+")");
        }
        if(link==1 && !chest_frame && at_sample(d,log))
            if(const auto view=view_now(d);distant_chest(view))chest_frame=n;
    }
    check(replaces==1 && stages==(link==3 ? 0u : 1u),stem+": one Continue and one save in the dungeon");
    check(!d.flush(),stem+": flushed");
    if(link<3) {
        const auto written=app::parse_automap(app::read_text_file(d.store().automap_path(),app::automap_max_bytes,"automap"));
        check(written.find(staged_digest) && *written.find(staged_digest)==*staged,stem+": the saved record is on disk");
        check(app::used_slot_digests(d.machine().memory().cartridge_ram()).count(staged_digest)==1,stem+": the record belongs to a used slot");
        app::atomic_text(marker(link),staged_digest);
    } else check(d.machine().peek(dungeon::game_mode)!=dungeon::dungeon_mode,"dungeon-continue leaves the dungeon");
    // dungeon-walk-1 opens its first chest at frame 3576 (the item chest).
    if(link==1) check(chest_frame && *chest_frame<3576,"an unopened chest shows on the map before the party reaches it");
    std::cout<<s.region<<" chain link "<<link<<" ("<<stem<<"): "<<rec.frames<<" frames, "<<count(log,MapEvent::Kind::sample)<<" samples";
    if(chest_frame)std::cout<<", first distant unopened chest on the map at frame "<<*chest_frame;
    std::cout<<"; "<<timer.seconds()<<" s.\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=7 && argc!=8)
            throw std::runtime_error("Expected <rom> <us|br> <recordings folder> <reference folder> <work folder> session|wrong-map|chain <1-3>|passivity <stem>|frames <folder>.");
        const std::string_view region=argv[2],part=argv[6];
        if(region!="us" && region!="br")throw std::runtime_error("Expected ROM version us or br.");
        const bool bare=part=="session" || part=="wrong-map";
        if(bare!=(argc==7) || (!bare && part!="chain" && part!="passivity" && part!="frames"))
            throw std::runtime_error("Unknown part or missing argument.");
        if(!*argv[1] || !std::filesystem::exists(argv[1]))return 77;
        if(identify_rom(argv[1])!=(region=="us" ? RomVersion::us_rev_a : RomVersion::brazilian))
            throw std::runtime_error("Wrong ROM version.");
        std::ifstream file(argv[1],std::ios::binary);
        auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
        Setup s{rom,identify_digest(sha1(*rom)),std::string(region),argv[3],argv[4],argv[5]};
        std::filesystem::create_directories(s.work);
        const app::RomFont font(*rom);
        if(part=="passivity") { passivity(s,argv[7]); return 0; }
        if(part=="wrong-map") { wrong_map(s); return 0; }
        if(part=="chain") {
            const std::string_view link=argv[7];
            if(link!="1" && link!="2" && link!="3")throw std::runtime_error("Expected chain link 1, 2 or 3.");
            chain(s,static_cast<unsigned>(link[0]-'0'));return 0;
        }
        if(part=="frames") {
            const std::filesystem::path out=argv[7];std::filesystem::create_directories(out);
            screen_test(font,out);door_frame(s,font,out);walk_frames(s,font,out);
            return 0;
        }
        const auto frames=s.work/"frames";std::filesystem::create_directories(frames);
        screen_test(font,frames);
        session(s,font,frames);
        unusable_file(s);
        std::cout<<"Automap session ("<<region<<"): screen, admission, hold, persistence and unusable file passed.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
