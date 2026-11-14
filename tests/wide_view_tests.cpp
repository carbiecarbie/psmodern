// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 10, part W2: the widescreen view (map reader, copies, held
// camera, gating, decoder, band renderer) through FrameDriver::wide_picture().
// Parts:
//   unit                       no ROM: the decoder against Vdp on random
//                              states; the reader on a synthetic image; the
//                              state probes (both wraps, a vehicle, the road,
//                              space flight, drops, resets) on synthetic copies
//   rom <rom> <us|br>          the reader on the player's ROM (11 areas, 192
//                              ids a screen, the ninth row and column) and the
//                              state probes again on its world map and a town
//   recording <rom> <us|br> <recordings> <reference> <work> <stem>
//                              one recording, option off and on side by side
//                              (two threads): machines equal every frame,
//                              centre agreement, scroll consistency, the
//                              gating oracle and the screen window
//   probes <rom> <us|br> <recordings> <reference> <work> <other rom>
//                              the option switched on mid-stay and during a
//                              transfer fade, power cycle, ROM swap, recording
//                              mode and --no-routines
//   ship <rom> <us|br> <recordings> <reference> <work> <routes>
//                              the space flight from later-town's start image
//                              (routes: C2E9 values in hex, comma separated;
//                              "extra" adds the option switched on mid-flight
//                              and a skipped flight):
//                              classification, centre agreement, space bands,
//                              the fade that follows, option on mid-flight, a
//                              skipped flight, passivity
//   timing <rom> <us|br> <recordings> <reference> <work> <stem>
//                              not a test: the option's cost per frame
// ROM parts exit 77 without their ROM. No image is written.
#include "app/frame_driver.h"
#include "app/session_roms.h"
#include "app/wide_view.h"
#include "game/map_screens.h"
#include "replay/recording.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <Z80.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace psmodern {
// Seeds a power-on VDP directly, as tests/vdp_picture_tests.cpp does.
struct VdpFixture {
    static void load(Vdp& v,const app::VdpState& s) {
        v.vram_=s.vram;v.cram_=s.cram;
        for(unsigned r=0;r<s.registers.size();++r) { v.write_control(s.registers[r]);v.write_control(static_cast<std::uint8_t>(0x80|r)); }
    }
};
}

using namespace psmodern;
using app::HeldCamera;
using app::VdpState;
using app::WidePicture;
using app::WideSnapshot;
using app::WideView;
namespace map=game::map;
namespace {
void check(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
std::string hex(unsigned v,int digits=2) {
    static constexpr char d[]="0123456789ABCDEF";std::string s;
    for(int i=digits-1;i>=0;--i)s+=d[(v>>(4*i))&15];
    return s;
}
struct Rng {
    std::uint64_t s;
    std::uint32_t next() { s^=s<<13;s^=s>>7;s^=s<<17;return static_cast<std::uint32_t>(s>>11); }
    std::uint8_t byte() { return static_cast<std::uint8_t>(next()); }
};
struct Timer {
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    double seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); }
};
std::uint64_t fnv(const void* data,std::size_t size,std::uint64_t h=1469598103934665603ull) {
    const auto* p=static_cast<const std::uint8_t*>(data);
    for(std::size_t i=0;i<size;++i) { h^=p[i];h*=1099511628211ull; }
    return h;
}
constexpr std::uint64_t frame_cycles=Machine::frame_cycles;

// ---------------------------------------------------------------- decoder
// Random VRAM, CRAM and registers (display enable, left blank, both scroll
// locks, backdrop, scrolls, name table base and row mask, pattern masks
// varied; sprites terminated by Y = D0h) with a static name table: the
// decoder's picture equals the VDP's second frame.
void decoder_against_vdp(unsigned count) {
    Rng rng{0x9e3779b97f4a7c15ull};
    unsigned display_off=0,left_blank=0,locks=0;
    for(unsigned n=0;n<count;++n) {
        VdpState s;
        for(auto& b:s.vram)b=rng.byte();
        for(auto& c:s.cram)c=rng.byte()&63;
        for(auto& r:s.registers)r=rng.byte();
        s.registers[0]|=0x04;                    // mode 4
        if(n%5)s.registers[1]|=0x40;             // mostly display on
        if(n%7==0)s.registers[3]=0xff,s.registers[4]=0xff;
        s.vram[(s.registers[5]&0x7e)<<7]=0xd0;   // no sprites
        display_off+=!(s.registers[1]&0x40);left_blank+=(s.registers[0]&0x20)!=0;locks+=(s.registers[0]&0xc0)!=0;
        Vdp v;v.read_status(0);
        VdpFixture::load(v,s);
        v.sync(2*frame_cycles);
        Vdp::Picture expected=v.picture(),got{};
        app::mode4::decode_background(s,got);
        if(got!=expected) {
            unsigned i=0;while(got[i]==expected[i])++i;
            std::string regs;for(auto r:s.registers)regs+=" "+hex(r);
            throw std::runtime_error("decoder differs from Vdp in state "+std::to_string(n)+" at line "+std::to_string(i/256)+
                                     " x "+std::to_string(i%256)+"; registers"+regs);
        }
    }
    check(display_off>0 && left_blank>0 && locks>0,"the random states cover display off, the left blank and the locks");
    std::cout<<"decoder: "<<count<<" random states equal to Vdp ("<<display_off<<" display off, "<<left_blank<<" left blank, "
             <<locks<<" with a scroll lock)\n";
}

// ------------------------------------------------------- synthetic image
// A 512 KiB image made here (not ROM data): bank 5 holds a table at 8100h
// and screens; bank 6 the metatiles.
struct Synthetic {
    std::shared_ptr<Memory::Rom> rom=std::make_shared<Memory::Rom>(512*1024,0);
    game::MapArea area{0x8100,5,6};
    std::array<game::MapScreen,64> screens{};
    void put(unsigned bank,unsigned address,std::uint8_t value) { (*rom)[bank*0x4000+address-0x8000]=value; }
    void word(unsigned bank,unsigned address,unsigned value) { put(bank,address,value&255);put(bank,address+1,value>>8); }
    Synthetic() {
        Rng rng{12345};
        unsigned at=0x8400;
        for(unsigned row=0;row<9;++row)for(unsigned column=0;column<9;++column) {
            const unsigned c=column%8,r=row%8;
            if(row<8 && column<8) {
                // Alternate literal and run coding; ids vary per cell.
                auto& screen=screens[r*8+c];
                for(auto& id:screen)id=static_cast<std::uint8_t>(rng.next()%7+((r*8+c)&3)*7);
                const unsigned start=at;
                for(unsigned i=0;i<screen.size();) {
                    unsigned run=1;while(i+run<screen.size() && screen[i+run]==screen[i] && run<127)++run;
                    if(run>=3) { put(5,at++,static_cast<std::uint8_t>(run));put(5,at++,screen[i]);i+=run; }
                    else {
                        unsigned length=0;
                        while(i+length<screen.size() && length<127) {
                            unsigned ahead=1;while(i+length+ahead<screen.size() && screen[i+length+ahead]==screen[i+length] && ahead<3)++ahead;
                            if(ahead>=3)break;
                            ++length;
                        }
                        put(5,at++,static_cast<std::uint8_t>(0x80|length));
                        for(unsigned k=0;k<length;++k)put(5,at++,screen[i+k]);
                        i+=length;
                    }
                }
                put(5,at++,0);
                word(5,0x8100+(row*9+column)*2,start);
            } else word(5,0x8100+(row*9+column)*2,(*rom)[5*0x4000+0x100+(r*9+c)*2]|((*rom)[5*0x4000+0x101+(r*9+c)*2]<<8));
        }
        for(unsigned id=0;id<256;++id)for(unsigned q=0;q<4;++q)
            word(6,0x8000+8*id+2*q,(rng.next()&0x1fff)|0);   // any tile, flips, palette, priority
    }
};

void reader_on_synthetic() {
    Synthetic img;
    game::MapScreens reader(img.rom);
    for(unsigned row=0;row<9;++row)for(unsigned column=0;column<9;++column) {
        unsigned count=0;
        const auto screen=reader.decode(img.area,column,row,&count);
        check(screen && count==map::cells_per_screen && *screen==img.screens[(row%8)*8+column%8],"synthetic screen decodes");
    }
    check(reader.select(img.area) && reader.ready(),"synthetic area selects");
    check(reader.screen(9,17)==img.screens[1*8+1],"screens wrap around the torus");
    check(reader.word(3,2)==((*img.rom)[6*0x4000+8*3+4]|((*img.rom)[6*0x4000+8*3+5]<<8)),"metatile words");
    // Failures: a table outside slot 2, a bank beyond the image, an empty
    // literal run, too few or too many ids, codes running past slot 2.
    auto failing=[&](auto edit,const std::string& what) {
        Synthetic bad;edit(bad);
        game::MapScreens r(bad.rom);
        check(!r.decode(bad.area,0,0),what+": decode refuses");
        check(!r.select(bad.area) && !r.ready(),what+": select refuses");
    };
    failing([](Synthetic& s){ s.area.table=0x7ff0; },"table below slot 2");
    failing([](Synthetic& s){ s.area.table=0xbffe; },"table past slot 2");
    failing([](Synthetic& s){ s.area.table_bank=32; },"table bank beyond the image");
    failing([](Synthetic& s){ s.word(5,0x8100,0x8000);s.put(5,0x8000,0x80); },"empty literal run");
    failing([](Synthetic& s){ s.word(5,0x8100,0x8000);s.put(5,0x8000,100);s.put(5,0x8001,1);s.put(5,0x8002,0); },"too few ids");
    failing([](Synthetic& s){ s.word(5,0x8100,0x8000);for(unsigned i=0;i<3;++i){s.put(5,0x8000+2*i,100);s.put(5,0x8001+2*i,1);}s.put(5,0x8006,0); },"too many ids");
    failing([](Synthetic& s){ s.word(5,0x8100,0xbffe);s.put(5,0xbffe,0x7f);s.put(5,0xbfff,1); },"codes past slot 2");
    failing([](Synthetic& s){ s.word(5,0x8100,0x4000); },"screen outside slot 2");
    {
        Synthetic bad;bad.area.metatile_bank=40;
        game::MapScreens r(bad.rom);check(!r.select(bad.area),"metatile bank beyond the image");
    }
    game::MapScreens none(nullptr);
    check(!none.select(img.area) && !none.decode(img.area,0,0) && !none.metatile(0,0),"no image reads nothing");
    std::cout<<"reader: synthetic image decoded; 9 refusals\n";
}

// ------------------------------------------------- synthetic copies
// A copy as the map would leave it for camera (x, y): the name table holds
// the screen's metatile words where the VDP will fetch them (register 8 is
// minus x, register 9 runs with y modulo 224), the window holds the 2 x 2
// screens from the camera's page. Patterns and CRAM come from base.
WideSnapshot map_copy(const game::MapScreens& reader,const game::MapArea& area,const VdpState& base,unsigned x,unsigned y,
                      std::uint8_t r9,std::uint8_t mode=0x09) {
    WideSnapshot s;
    s.vdp=base;
    auto& r=s.vdp.registers;
    r={0x26,0x60,0xff,0xff,0xff,0xff,0xfb,0x00,static_cast<std::uint8_t>(-static_cast<int>(x&255)),r9,0xff};
    for(unsigned py=0;py<192;++py)for(unsigned px=0;px<256;++px) {
        const unsigned wx=(x+px)%map::world_width,wy=(y+py)%map::world_height;
        const auto& screen=reader.screen(wx/256,wy/192);
        const unsigned lx=(wx%256)>>4,ly=(wy%192)>>4;
        const auto name=reader.word(screen[ly*16+lx],((wx>>3)&1)+2*(((wy%192)>>3)&1));
        const unsigned row=((py+r9)%224)/8,column=((wx%256)/8);
        s.vdp.vram[0x3800+row*64+column*2]=name&255;s.vdp.vram[0x3801+row*64+column*2]=static_cast<std::uint8_t>(name>>8);
    }
    s.vdp.vram[0x3f00]=0xd0;
    s.mode=mode;
    s.h_shadow=r[8];s.camera_x=x&255;s.page_x=static_cast<std::uint8_t>(x>>8);
    s.v_shadow=r9;s.camera_y=y%192;s.page_y=static_cast<std::uint8_t>(y/192);
    s.area=area;
    for(unsigned k=0;k<4;++k) {
        const auto& screen=reader.screen(s.page_x+(k&1),s.page_y+(k>>1));
        std::copy(screen.begin(),screen.end(),s.window.begin()+k*256);
    }
    return s;
}
// Patterns everywhere except the name table and the sprite table, and a CRAM.
VdpState random_base(std::uint64_t seed) {
    Rng rng{seed};VdpState s;
    for(auto& b:s.vram)b=rng.byte();
    for(auto& c:s.cram)c=rng.byte()&63;
    return s;
}
// The renderer's own x 8-255 of the centre equals the given picture outside
// the masked pixels; returns the pixels compared.
std::size_t centre_agrees(const WideView& view,const Vdp::Picture& picture,const std::vector<bool>* mask,const std::string& what) {
    WidePicture own{};
    view.render(app::wide_centre_x+8,app::wide_centre_x+256,own);
    std::size_t compared=0;
    for(unsigned y=0;y<192;++y)for(unsigned x=8;x<256;++x) {
        if(mask && (*mask)[y*256+x])continue;
        ++compared;
        if(own[y*app::wide_width+app::wide_centre_x+x]!=picture[y*256+x])
            throw std::runtime_error(what+": renderer differs from the picture at line "+std::to_string(y)+" x "+std::to_string(x));
    }
    return compared;
}
void centre_agrees_decoded(const WideView& view,const std::string& what) {
    Vdp::Picture picture{};
    app::mode4::decode_background(view.shown()->vdp,picture);
    centre_agrees(view,picture,nullptr,what);
}
// The camera of the copy paired with the picture after a push.
void expect_camera(const WideView& view,unsigned x,unsigned y,const std::string& what) {
    check(view.widened() && view.camera() && view.camera()->x==x%map::world_width && view.camera()->y==y%map::world_height,
          what+": expected camera "+std::to_string(x%map::world_width)+","+std::to_string(y%map::world_height)+
          (view.camera() ? " got "+std::to_string(view.camera()->x)+","+std::to_string(view.camera()->y) : " got none"));
}

// The state probes on one area of a reader: both wraps on foot and in a
// vehicle, the road, space flight, drops, the family modes and resets.
void state_probes(std::shared_ptr<const Memory::Rom> rom,const game::MapArea& area,const std::string& name) {
    game::MapScreens reader(rom);
    check(reader.select(area),name+": area readable");
    const auto base=random_base(fnv(&area,sizeof area));
    // Walks: (dx, dy) per frame from (x0, y0) for frames; every copy after the
    // first anchors-or-follows; the work RAM camera is scrambled after the
    // anchor to prove it is never read again.
    auto walk=[&](WideView& view,int x0,int y0,int dx,int dy,unsigned frames,const std::string& what,bool scramble) {
        int x=x0,y=y0;std::uint8_t r9=static_cast<std::uint8_t>(((y0%224)+224)%224);
        unsigned widened=0,windowed=0;
        for(unsigned n=0;n<frames;++n) {
            const unsigned ux=static_cast<unsigned>((x%2048+2048)%2048),uy=static_cast<unsigned>((y%1536+1536)%1536);
            auto copy=map_copy(reader,area,base,ux,uy,r9);
            if(scramble && n>2) { copy.camera_x^=0x55;copy.page_y=3;copy.h_shadow^=1; }
            view.push(copy);
            if(n>0) {
                const int px=x-dx,py=y-dy;
                expect_camera(view,static_cast<unsigned>((px%2048+2048)%2048),static_cast<unsigned>((py%1536+1536)%1536),what+" frame "+std::to_string(n));
                if(n%3==1)centre_agrees_decoded(view,what+" frame "+std::to_string(n));
                ++widened;windowed+=view.window_used();
            } else check(!view.widened(),what+": the first copy has no picture to pair with");
            x+=dx;y+=dy;r9=static_cast<std::uint8_t>(((r9+dy)%224+224)%224);
        }
        return std::pair{widened,windowed};
    };
    {   // Both wraps on foot (1 px), right and down across 2047 and 1535.
        WideView view(rom);
        const auto [w,win]=walk(view,2048-140,1536-120,1,1,280,name+" wraps on foot",false);
        check(w==279 && win>200,name+": every paired frame widened across both wraps, the window used on steady pages");
    }
    {   // A vehicle (2 px), left and up across 0; RAM scrambled after the anchor.
        WideView view(rom);
        walk(view,120,90,-2,-2,160,name+" vehicle",true);
    }
    {   // Diagonal step deltas of 2 are kept, 3 drops; a family mode that never
        // anchors then shows nothing until a map frame.
        WideView view(rom);
        view.push(map_copy(reader,area,base,500,700,700%224));
        view.push(map_copy(reader,area,base,500,700,700%224));
        expect_camera(view,500,700,name+" anchor");
        auto jump=map_copy(reader,area,base,503,700,700%224,0x0B);   // 3 px in mode 0Bh
        view.push(jump);expect_camera(view,500,700,name+" before the jump");
        view.push(jump);check(!view.widened() && !view.camera(),name+": a 3 px jump drops the anchor");
        view.push(jump);check(!view.widened(),name+": mode 0Bh never takes an anchor");
        view.push(map_copy(reader,area,base,503,700,700%224));
        check(!view.widened(),name+": pillarbox until the map frame is paired");
        view.push(map_copy(reader,area,base,503,700,700%224));
        expect_camera(view,503,700,name+" re-anchored on the map");
    }
    {   // Family modes keep the anchor while the RAM camera is teleported
        // (transfers, exit restores); none of them takes one; the road does.
        for(std::uint8_t mode:{std::uint8_t(0x08),std::uint8_t(0x0A),std::uint8_t(0x0B),std::uint8_t(0x0C),std::uint8_t(0x0E)}) {
            WideView view(rom);
            view.push(map_copy(reader,area,base,1000,300,300%224));
            auto moved=map_copy(reader,area,base,1000,300,300%224,mode);
            moved.camera_x=0x40;moved.page_x=2;moved.camera_y=10;moved.page_y=5;   // destination camera in RAM
            for(int n=0;n<5;++n) { view.push(moved);expect_camera(view,1000,300,name+" mode "+hex(mode)+" keeps"); }
            WideView fresh(rom);
            for(int n=0;n<5;++n) { fresh.push(map_copy(reader,area,base,1000,300,300%224,mode));check(!fresh.widened(),name+": mode "+hex(mode)+" never anchors"); }
        }
        WideView road(rom);
        road.push(map_copy(reader,area,base,64,1400,1400%224,0x0F));
        road.push(map_copy(reader,area,base,65,1400,1400%224,0x0F));
        expect_camera(road,64,1400,name+" road anchors");
        centre_agrees_decoded(road,name+" road");
    }
    {   // The space flight (modes 4, 5): over a map whose name table matches
        // the camera from work RAM it shows the map and holds that camera for
        // the fade (mode 8) that follows; with register 8 at 0 and any other
        // name table it shows space (the name table wrapped); otherwise, with
        // register 0 = 06h or the display off, it pillarboxes.
        for(std::uint8_t mode:{std::uint8_t(0x04),std::uint8_t(0x05)}) {
            const std::string what=name+" ship mode "+hex(mode);
            WideView view(rom);
            auto over=map_copy(reader,area,base,800,808,808%224,mode);
            over.v_shadow=static_cast<std::uint8_t>(over.vdp.registers[9]-8);over.camera_y=800%192;   // scroll 8 lines ahead of RAM
            over.h_shadow^=0x11;                                                                        // shadows need not agree
            view.push(over);view.push(over);
            expect_camera(view,800,808,what+" over the map");
            check(view.source()==app::WideSource::map && !view.window_used(),what+": map source, ROM screens");
            centre_agrees_decoded(view,what+" over the map");
            auto fade=over;fade.mode=0x08;fade.camera_x=0;fade.page_x=5;   // the transfer has moved the RAM camera
            view.push(fade);view.push(fade);
            expect_camera(view,800,808,what+" fade inherits the camera");
            Rng rng{mode};
            auto space=over;
            for(unsigned i=0x3800;i<0x3f00;++i)space.vdp.vram[i]=rng.byte();
            space.vdp.registers[8]=0;
            // The departure fade before the take-off: register 8 at 0 (a
            // spaceport camera whose low byte is 0) and a name table that is
            // not the map at the RAM camera: pillarbox until the scene has
            // shown a map; a frame outside the scene and a reset forget it.
            // No such spaceport exists on the eight routes 81h-88h (their
            // departure cameras' low bytes are 10h, 50h, B0h, C0h and D0h,
            // ROM run), so this case is synthetic only.
            {
                WideView departure(rom);
                for(int k=0;k<4;++k) { departure.push(space);check(!departure.widened(),what+": the departure fade pillarboxes"); }
                departure.push(over);departure.push(over);
                check(departure.source()==app::WideSource::map,what+": the take-off shows the map");
                departure.push(space);departure.push(space);
                check(departure.source()==app::WideSource::space,what+": space after the take-off");
                departure.push(fade);departure.push(space);departure.push(space);
                check(!departure.widened(),what+": a frame outside the scene ends it");
                departure.push(over);departure.push(over);departure.reset();
                departure.push(space);departure.push(space);
                check(!departure.widened(),what+": a reset ends the scene");
            }
            view.push(over);view.push(over);
            view.push(space);view.push(space);
            check(view.widened() && view.source()==app::WideSource::space && !view.camera(),what+": space");
            Vdp::Picture picture{};app::mode4::decode_background(space.vdp,picture);
            centre_agrees(view,picture,nullptr,what+" space");
            WidePicture wrapped{};app::mode4::render_wrapped(space.vdp,0,app::wide_width,wrapped);
            // This proves only that the view sends space frames to the
            // wrapped renderer; that renderer is proved against the decoder
            // (unit) and the VDP picture (ship test). Nothing can prove what
            // columns the VDP never draws "would" show.
            const auto* wide=view.picture(picture);
            check(wide && std::equal(wide->begin(),wide->begin()+52,wrapped.begin()),what+": space bands are the wrapped name table");
            auto stray=space;stray.vdp.registers[8]=0x30;
            view.push(stray);view.push(stray);
            check(!view.widened() && !view.camera(),what+": another name table with register 8 not 0 pillarboxes");
            for(auto edit:{0,1}) {
                auto off=over;
                if(edit)off.vdp.registers[1]=0x20; else off.vdp.registers[0]=0x06;
                view.push(off);view.push(off);
                check(!view.widened() && !view.camera(),what+(edit ? ": display off pillarboxes" : ": register 0 = 06h pillarboxes"));
            }
        }
    }
    {   // Register 0 = 06h, display off and a mode outside the family never
        // widen, and drop an anchor.
        auto variant=[&](const std::string& what,auto edit) {
            WideView view(rom);
            view.push(map_copy(reader,area,base,800,800,800%224));
            view.push(map_copy(reader,area,base,800,800,800%224));
            expect_camera(view,800,800,name+" before "+what);
            auto copy=map_copy(reader,area,base,800,800,800%224);edit(copy);
            for(int n=0;n<3;++n) { view.push(copy);if(n)check(!view.widened() && !view.camera(),name+": "+what+" never widens"); }
        };
        variant("register 0 = 06h",[](WideSnapshot& s){ s.vdp.registers[0]=0x06; });
        variant("display off",[](WideSnapshot& s){ s.vdp.registers[1]=0x20; });
        variant("battle mode 2Bh",[](WideSnapshot& s){ s.mode=0x2B; });
    }
    {   // A consistent map frame whose RAM camera differs from the held one
        // corrects it (500 and 756 share the scroll register); the screen
        // window is trusted on map and road frames only.
        WideView view(rom);
        view.push(map_copy(reader,area,base,500,700,700%224));
        view.push(map_copy(reader,area,base,500,700,700%224));
        expect_camera(view,500,700,name+" before the correction");
        const auto other=map_copy(reader,area,base,756,700,700%224);
        view.push(other);
        check(view.corrections()==0,name+": no correction while the copies agree");
        view.push(other);
        expect_camera(view,756,700,name+" corrected");
        check(view.corrections()==1,name+": the correction is counted");
        view.push(other);
        check(view.window_used(),name+": a steady map page uses the window");
        auto fade=other;fade.mode=0x08;
        view.push(fade);view.push(fade);
        check(view.widened() && !view.window_used(),name+": a transfer fade (mode 8) never uses the window");
        auto road=other;road.mode=0x0F;
        view.push(road);view.push(road);
        check(view.widened() && view.window_used(),name+": the road uses the window");
    }
    {   // Inconsistent first frames (stale registers) wait; resets forget.
        WideView view(rom);
        auto stale=map_copy(reader,area,base,256,384,384%224);stale.vdp.registers[8]^=4;
        view.push(stale);view.push(stale);view.push(stale);
        check(!view.widened(),name+": stale scroll registers never anchor");
        view.push(map_copy(reader,area,base,256,384,384%224));view.push(map_copy(reader,area,base,256,384,384%224));
        expect_camera(view,256,384,name+" consistent");
        view.reset();check(!view.widened() && !view.shown() && !view.picture(Vdp::Picture{}),name+": reset forgets");
        view.push(map_copy(reader,area,base,256,384,384%224));check(!view.widened(),name+": nothing paired after a reset");
        view.push(map_copy(reader,area,base,256,384,384%224));expect_camera(view,256,384,name+" after reset");
        view.replace_rom(rom);check(!view.widened() && !view.camera(),name+": a ROM swap drops the anchor");
        // An unreadable area pillarboxes, the anchor is kept.
        auto bad=map_copy(reader,area,base,256,384,384%224);bad.area.table_bank=200;
        view.push(map_copy(reader,area,base,256,384,384%224));view.push(bad);view.push(bad);
        check(!view.widened() && view.camera(),name+": an unreadable area pillarboxes");
        view.push(map_copy(reader,area,base,256,384,384%224));view.push(map_copy(reader,area,base,256,384,384%224));
        expect_camera(view,256,384,name+" readable again");
        // The wide picture: the centre's x 8-255 copied, the rest drawn.
        Vdp::Picture centre{};for(std::size_t i=0;i<centre.size();++i)centre[i]=static_cast<std::uint8_t>(i*7&63);
        const auto* wide=view.picture(centre);
        check(wide!=nullptr,name+": a picture");
        WidePicture own{};view.render(0,app::wide_width,own);
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<344;++x) {
            const bool copied=x>=52 && x<300;
            check((*wide)[y*344+x]==(copied ? centre[y*256+x-44] : own[y*344+x]),name+": wide picture composition at "+std::to_string(x)+","+std::to_string(y));
        }
    }
    std::cout<<name<<": state probes passed (both wraps, vehicle, road, ship over a map and in space, drops, family modes, correction, window modes, resets)\n";
}

// The wrapped renderer's x 52-299 equals the decoder's x 8-255 on random
// states (both scroll locks, fine scrolls, display off included).
void wrapped_against_decoder(unsigned count) {
    Rng rng{0x51ed270b27ull};
    for(unsigned n=0;n<count;++n) {
        VdpState s;
        for(auto& b:s.vram)b=rng.byte();
        for(auto& c:s.cram)c=rng.byte()&63;
        for(auto& r:s.registers)r=rng.byte();
        if(n%5)s.registers[1]|=0x40;
        Vdp::Picture picture{};app::mode4::decode_background(s,picture);
        WidePicture wide{};app::mode4::render_wrapped(s,0,app::wide_width,wide);
        for(unsigned y=0;y<192;++y)for(unsigned x=8;x<256;++x)
            if(wide[y*app::wide_width+app::wide_centre_x+x]!=picture[y*256+x])
                throw std::runtime_error("wrapped renderer differs from the decoder in state "+std::to_string(n)+" at line "+std::to_string(y)+" x "+std::to_string(x));
    }
    std::cout<<"wrapped renderer: "<<count<<" random states equal to the decoder at x 8-255\n";
}

void unit() {
    decoder_against_vdp(240);
    wrapped_against_decoder(240);
    reader_on_synthetic();
    Synthetic img;
    state_probes(img.rom,img.area,"synthetic");
    // An edited window cell shows in the bands only while the window is trusted.
    game::MapScreens reader(img.rom);reader.select(img.area);
    const auto base=random_base(7);
    WideView view(img.rom);
    auto copy=map_copy(reader,img.area,base,300,192,192%224);
    copy.window[0]=static_cast<std::uint8_t>(copy.window[0]^0x15);   // world cell (256, 192) in page 1, 1
    view.push(copy);view.push(copy);
    check(view.widened() && !view.window_used(),"the window waits for a steady page");
    // World cell (256, 192): wide x 0-15, lines 0-15 for a camera at (300, 192).
    auto expected=[&](std::uint8_t id,unsigned x,unsigned y) {
        const unsigned wx=256+x,wy=192+y;
        const auto name=reader.word(id,((wx>>3)&1)+2*(((wy%192)>>3)&1));
        return copy.vdp.cram[app::mode4::tile_row(copy.vdp,name,(wy%192)&7)[wx&7]];
    };
    auto cell_shows=[&](std::uint8_t id) {
        WidePicture own{};view.render(0,16,own);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)if(own[y*app::wide_width+x]!=expected(id,x,y))return false;
        return true;
    };
    check(cell_shows(reader.screen(1,1)[0]),"the ROM cell shows while the window waits");
    view.push(copy);
    check(view.window_used(),"a steady page at the camera's page uses the window");
    check(cell_shows(copy.window[0]),"the edited window cell shows");
    std::cout<<"unit: passed\n";
}

// ------------------------------------------------------------- ROM parts
struct Setup {
    std::shared_ptr<const Memory::Rom> rom;
    RomVersion version;
    std::string region;
    std::filesystem::path recordings,reference,work;
};
std::shared_ptr<const Memory::Rom> read_rom(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}
// The area records the map load 00:0B6A reads (11 records of 14 bytes at
// 0DF1h in bank 0): the screen table pointer, its bank, the metatile bank.
std::vector<game::MapArea> areas(const Memory::Rom& rom) {
    std::vector<game::MapArea> out;
    for(unsigned k=0;k<11;++k) {
        const auto* r=rom.data()+0x0DF1+14*k;
        out.push_back({static_cast<std::uint16_t>(r[0]|(r[1]<<8)),r[2],r[9]});
    }
    return out;
}
void rom_part(const Setup& s) {
    game::MapScreens reader(s.rom);
    const auto list=areas(*s.rom);
    unsigned screens=0;
    for(unsigned k=0;k<list.size();++k) {
        std::array<std::array<game::MapScreen,9>,9> all{};
        for(unsigned row=0;row<9;++row)for(unsigned column=0;column<9;++column) {
            unsigned count=0;
            const auto screen=reader.decode(list[k],column,row,&count);
            check(screen && count==map::cells_per_screen,"area "+std::to_string(k)+" screen "+std::to_string(column)+","+std::to_string(row)+
                  " gives "+std::to_string(count)+" ids");
            all[row][column]=*screen;++screens;
        }
        for(unsigned i=0;i<9;++i)
            check(all[8][i]==all[0][i] && all[i][8]==all[i][0],"area "+std::to_string(k)+": the ninth row and column repeat the first");
        check(reader.select(list[k]),"area "+std::to_string(k)+" selects");
    }
    std::cout<<s.region<<": "<<list.size()<<" areas, "<<screens<<" screens of exactly 192 ids, ninth row and column repeat\n";
    state_probes(s.rom,list[0],s.region+" world map (area 0)");
    state_probes(s.rom,list[4],s.region+" town (area 4)");
}

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
// A part that cannot run completely here (a start image or the other ROM
// not configured): the test exits 77, which CMake reports as skipped.
struct Skip { std::string why; };
// The recording's start image as the save of a fresh folder; skips when the
// image is needed and the reference folder is not configured.
void place_image(const Setup& s,const std::string& stem,const replay::Recording& rec,const std::filesystem::path& folder) {
    if(sha1(Memory::CartridgeRam{})==rec.cart_ram_sha1)return;
    if(s.reference.empty())throw Skip{stem+": the start image needs the reference folder"};
    const auto image=s.reference/s.region/stem/"cart-ram.bin";
    check(std::filesystem::exists(image),stem+": start image missing");
    std::filesystem::copy_file(image,folder/app::save_name(s.version));
}
// The pixels of a picture covered by sprites of a VDP state (boxes, with the
// zoom and the early clock), which the background renderer does not draw.
std::vector<bool> sprite_mask(const VdpState& v) {
    std::vector<bool> mask(256*192,false);
    const auto& r=v.registers;
    const unsigned sat=(r[5]&0x7e)<<7,zoom=(r[1]&1) ? 2 : 1,height=((r[1]&2) ? 16 : 8)*zoom;
    for(unsigned i=0;i<64;++i) {
        const unsigned y=v.vram[sat+i];
        if(y==0xd0)break;
        int x=v.vram[(sat+((r[5]&1) ? 0x80 : 0)+2*i)&0x3fff];
        if(r[0]&8)x-=8;
        for(unsigned dy=0;dy<height;++dy) {
            const unsigned line=(y+1+dy)&255;
            if(line>=192)continue;
            for(int dx=0;dx<static_cast<int>(8*zoom);++dx)if(x+dx>=0 && x+dx<256)mask[line*256+static_cast<unsigned>(x+dx)]=true;
        }
    }
    return mask;
}
// Equality of two machines after a frame, as the automap passivity test
// does: cycles, CPU, work and cartridge RAM, VRAM, CRAM and picture.
std::uint64_t fingerprint(const Machine& m) {
    const auto& c=m.cpu();
    const std::uint16_t regs[]{c.pc.uint16_value,c.sp.uint16_value,c.af.uint16_value,c.bc.uint16_value,c.de.uint16_value,c.hl.uint16_value,
        c.ix_iy[0].uint16_value,c.ix_iy[1].uint16_value,c.af_.uint16_value,c.bc_.uint16_value,c.de_.uint16_value,c.hl_.uint16_value};
    auto h=fnv(regs,sizeof regs);
    const auto cycles=m.cycles();h=fnv(&cycles,sizeof cycles,h);
    h=fnv(m.memory().ram().data(),m.memory().ram().size(),h);
    h=fnv(m.memory().cartridge_ram().data(),m.memory().cartridge_ram().size(),h);
    h=fnv(m.vdp().vram().data(),m.vdp().vram().size(),h);
    h=fnv(m.vdp().cram().data(),m.vdp().cram().size(),h);
    h=fnv(m.vdp().registers().data(),m.vdp().registers().size(),h);
    return fnv(m.picture().data(),m.picture().size(),h);
}
std::uint64_t writes_hash(const std::vector<PsgWrite>& writes) {
    std::uint64_t h=fnv(nullptr,0);
    for(const auto& w:writes) { h=fnv(&w.cycle,sizeof w.cycle,h);h=fnv(&w.byte,sizeof w.byte,h); }
    return h;
}
// A Machine copy stepped natively to line 192 of the frame about to run.
bool patterns_change_before_192(const Machine& before,const FrameInput& input) {
    Machine copy(before);
    for(unsigned i=0;i<2;++i)copy.set_pad(i,input.pads[i]);
    copy.set_reset(input.reset);copy.set_pause(input.pause);
    const auto target=(copy.cycles()/frame_cycles)*frame_cycles+192*228+4;
    while(copy.cycles()<target || copy.cpu().resume==Z80_RESUME_XY)copy.step();
    const auto& a=before.vdp().vram();const auto& b=copy.vdp().vram();
    return std::memcmp(a.data(),b.data(),0x3800)!=0;
}
bool family(std::uint8_t mode) {
    return mode==0x08 || mode==0x09 || mode==0x0A || mode==0x0B || mode==0x0C || mode==0x0E || mode==0x0F;
}

struct Counts {
    std::uint64_t frames=0,eligible=0,widened=0,pre_anchor=0,map_frames=0,runs=0,runs_widened=0;
    std::map<unsigned,std::uint64_t> widened_by_mode,eligible_by_mode;
    std::uint64_t r0_06=0,mode4_5=0,display_off_family=0;
    std::uint64_t centre_frames=0,centre_pixels=0,late_patterns=0;
    std::uint64_t scroll_pairs=0,scroll_pixels=0;
    std::uint64_t window_frames=0,window_used=0;
    std::uint64_t transfer_runs=0,dungeon_runs=0,interaction_runs=0,exit_runs=0;
};

// One recording with the option off and on, each driver on its own thread.
void recording_part(const Setup& s,const std::string& stem) {
    Timer timer;
    const auto rec=recording(s,stem);
    const auto off_folder=fresh(s,stem+"-off"),on_folder=fresh(s,stem+"-on");
    place_image(s,stem,rec,off_folder);place_image(s,stem,rec,on_folder);
    app::FrameDriver off(s.rom,app::SaveStore(off_folder,s.version)),on(s.rom,app::SaveStore(on_folder,s.version));
    off.set_widescreen(false);on.set_widescreen(true);
    check(!off.widescreen() && on.widescreen(),stem+": the option applies at once");
    std::vector<std::uint64_t> off_trace(rec.frames),on_trace(rec.frames);
    std::exception_ptr off_error;
    std::thread off_thread([&] {
        try {
            for(std::uint64_t n=0;n<rec.frames;++n) {
                auto out=off.run(rec.input_at(n));
                if(out.save_error)throw std::runtime_error(*out.save_error);
                if(off.wide_picture())throw std::runtime_error("option off gave a wide picture");
                off_trace[n]=fingerprint(off.machine())^writes_hash(out.writes);
            }
        } catch(...) { off_error=std::current_exception(); }
    });
    Counts c;
    game::MapScreens reader(s.rom);
    // The state after the previous frame: what the picture of this frame is drawn with.
    struct Prev {
        std::uint8_t mode=0,r0=0,r1=0,r8=0,r9=0,c300=0,c301=0,c302=0,c304=0,c305=0,c306=0;
        std::array<std::uint8_t,2> page{};bool valid=false;
        // Work RAM is the displayed camera (computed here, not by WideView).
        bool consistent() const {
            return r8==c300 && c300==static_cast<std::uint8_t>(0x100-c301) && r9==c304 && c304<224 && c302<8 && c305<192 && c306<8;
        }
    } prev;
    // The gating oracle's own camera: anchored on a consistent map or road
    // frame, moved by the register deltas, dropped by a jump above 2 px or a
    // frame outside decision 6.
    struct OracleCamera { unsigned x=0,y=0;std::uint8_t r8=0,r9=0; };
    std::optional<OracleCamera> oracle;
    std::uint64_t oracle_corrections=0,stay_pre_anchor=0,max_pre_anchor=0,moving=0;
    constexpr std::uint64_t pre_anchor_bound=2;   // map frames before a stay's anchor (0 in every recording)
    // Every recording whose camera moves on at least 16 widened frames (one
    // step on foot) has band pixels that later reach the centre.
    constexpr std::uint64_t moving_for_scroll=16;
    bool in_run=false,run_widened=false,run_transfer=false,run_dungeon=false,run_interaction=false;
    // Scroll consistency: recent widened frames' bands with the camera, the
    // area, the anchor they belong to, a digest of CRAM and the pattern masks
    // and their patterns (a pixel is compared only when the tile the later
    // centre shows there has the same pattern bytes in both copies).
    struct Past {
        std::uint64_t frame,stay,digest;HeldCamera camera;game::MapArea area;
        std::vector<std::uint8_t> bands;   // x 0-51 then 300-343, per line
        std::vector<std::uint8_t> patterns;
    };
    std::deque<Past> past;
    std::uint64_t stay=0;bool was_anchored=false;
    std::uint8_t prev_c202_after=0;
    try {
        for(std::uint64_t n=0;n<rec.frames;++n) {
            const auto input=rec.input_at(n);
            const Machine* before=nullptr;std::optional<Machine> saved;
            const auto& m0=on.machine();
            const bool candidate=family(m0.peek(0xC202)) && m0.vdp().registers()[0]==0x26 && (m0.vdp().registers()[1]&0x40);
            if(candidate) { saved.emplace(m0);before=&*saved; }
            auto out=on.run(input);
            check(!out.save_error,stem+": save error");
            on_trace[n]=fingerprint(on.machine())^writes_hash(out.writes);
            const auto* wide=on.wide_picture();
            const auto* view=on.wide_view();
            const bool widened=wide!=nullptr;
            ++c.frames;
            // Pairing: the copy shown is the state after the previous frame.
            if(prev.valid) check(view->shown() && view->shown()->mode==prev.mode && view->shown()->vdp.registers[0]==prev.r0,
                                 stem+": the picture is paired with the state after the previous frame (frame "+std::to_string(n)+")");
            const bool eligible=prev.valid && family(prev.mode) && prev.r0==0x26 && (prev.r1&0x40);
            const bool ship=prev.valid && (prev.mode==4 || prev.mode==5);
            if(ship) {
                // The ship's own rule (proved by the ship test): only with
                // register 0 = 26h and the display on; a map frame holds the
                // camera the following fade starts from.
                check(!widened || (prev.r0==0x26 && (prev.r1&0x40)),stem+": ship frame "+std::to_string(n)+" widened outside its rule");
                oracle.reset();
                if(widened && view->source()==app::WideSource::map) {
                    // The camera the ship shows, from work RAM (computed here).
                    int dy=(prev.r9%224-prev.c304%224+224)%224;if(dy>=112)dy-=224;
                    const OracleCamera ram{(prev.c302&7u)*256u+prev.c301,
                        static_cast<unsigned>((static_cast<int>((prev.c306&7u)*192u+prev.c305)+dy+1536*2)%1536),prev.r8,prev.r9};
                    check(view->camera() && view->camera()->x==ram.x && view->camera()->y==ram.y,stem+": ship frame "+std::to_string(n)+" camera differs from work RAM's");
                    oracle=ram;
                } else if(widened) check(view->source()==app::WideSource::space && prev.r8==0,stem+": ship frame "+std::to_string(n)+" shows space with register 8 not 0");
            } else if(!eligible)oracle.reset();
            else {
                if(oracle) {
                    const int dx=-static_cast<int>(static_cast<std::int8_t>(static_cast<std::uint8_t>(prev.r8-oracle->r8)));
                    int dy=(prev.r9%224-oracle->r9%224+224)%224;if(dy>=112)dy-=224;
                    if(std::abs(dx)>2 || std::abs(dy)>2)oracle.reset();
                    else *oracle={static_cast<unsigned>(static_cast<int>(oracle->x)+dx+2048)%2048,
                                  static_cast<unsigned>(static_cast<int>(oracle->y)+dy+1536)%1536,prev.r8,prev.r9};
                }
                if((prev.mode==0x09 || prev.mode==0x0F) && prev.consistent()) {
                    const OracleCamera ram{prev.c302*256u+prev.c301,prev.c306*192u+prev.c305,prev.r8,prev.r9};
                    if(oracle && (oracle->x!=ram.x || oracle->y!=ram.y))++oracle_corrections;
                    oracle=ram;
                }
            }
            // Exactly when: a frame widens if and only if decision 6 holds and
            // the oracle holds a camera, which is then the view's.
            if(!ship) check(widened==oracle.has_value(),stem+": frame "+std::to_string(n)+(widened ? " widened" : " pillarboxed")+
                  " against the gating oracle (mode "+hex(prev.mode)+", R0 "+hex(prev.r0)+", consistent "+std::to_string(prev.consistent())+")");
            const auto expected=oracle.value_or(OracleCamera{});
            if(widened && !ship) check(view->camera()->x==expected.x && view->camera()->y==expected.y,stem+": frame "+std::to_string(n)+" camera differs from the oracle's");
            if(prev.valid && prev.r0==0x06)++c.r0_06;
            if(prev.valid && (prev.mode==4 || prev.mode==5))++c.mode4_5;
            if(prev.valid && family(prev.mode) && prev.r0==0x26 && !(prev.r1&0x40))++c.display_off_family;
            check(!widened || eligible || ship,stem+": frame "+std::to_string(n)+" widened outside decision 6 (mode "+hex(prev.mode)+", R0 "+hex(prev.r0)+")");
            if(eligible) {
                ++c.eligible;++c.eligible_by_mode[prev.mode];
                if(!in_run) { in_run=true;run_widened=false;run_transfer=run_dungeon=run_interaction=false;++c.runs;stay_pre_anchor=0; }
                if(prev.mode==0x08)run_transfer=true;
                if(prev.mode==0x0A)run_dungeon=true;
                if(prev.mode==0x0C)run_interaction=true;
                if(widened) { if(!run_widened)++c.runs_widened;run_widened=true;++c.widened;++c.widened_by_mode[prev.mode]; }
                else if(prev.mode==0x09) {
                    // Every display-on map frame widens, except before the anchor.
                    check(!run_widened,stem+": map frame "+std::to_string(n)+" pillarboxed after its stay was anchored");
                    check(!prev.consistent(),stem+": consistent map frame "+std::to_string(n)+" pillarboxed");
                    ++c.pre_anchor;
                    max_pre_anchor=std::max(max_pre_anchor,++stay_pre_anchor);
                    check(stay_pre_anchor<=pre_anchor_bound,stem+": more than "+std::to_string(pre_anchor_bound)+
                          " map frames before the anchor of a stay (frame "+std::to_string(n)+")");
                }
            } else if(in_run) {
                in_run=false;c.transfer_runs+=run_transfer;c.dungeon_runs+=run_dungeon;c.interaction_runs+=run_interaction;
            }
            if(prev.valid && prev.mode==0x09)++c.map_frames;
            // Space frames (none recorded) have no camera; the ship test checks them.
            if(widened && view->source()==app::WideSource::map) {
                const auto& shown=*view->shown();
                if(!was_anchored)++stay;
                // Centre agreement, unless pattern VRAM changed before line 192.
                const auto mask=sprite_mask(shown.vdp);
                const bool late=std::memcmp(shown.vdp.vram.data(),on.machine().vdp().vram().data(),0x3800)!=0 && before &&
                                patterns_change_before_192(*before,input);
                if(late)++c.late_patterns;
                else { c.centre_pixels+=centre_agrees(*view,on.machine().picture(),&mask,stem+" frame "+std::to_string(n));++c.centre_frames; }
                // Scroll consistency against earlier widened frames of the same stay.
                const auto digest=fnv(shown.vdp.cram.data(),32,fnv(&shown.vdp.registers[2],3));
                const auto& cam=*view->camera();
                if(was_anchored && !past.empty() && past.back().stay==stay && !(past.back().camera==cam))++moving;
                if(!late) for(const auto& f:past) {
                    const auto lag=n-f.frame;
                    if(f.stay!=stay || f.digest!=digest || !(f.area==shown.area) || (lag!=8 && lag!=16 && lag!=32 && lag!=48 && lag!=64 && lag!=96))continue;
                    std::uint64_t compared=0;
                    std::array<std::int8_t,512> same_tile;same_tile.fill(-1);
                    const auto& r=shown.vdp.registers;
                    for(unsigned y=0;y<192;++y)for(unsigned k=0;k<96;++k) {
                        const unsigned x=k<52 ? k : 248+k;
                        const unsigned wx=(f.camera.x+2048+x-44)%2048,wy=(f.camera.y+y)%1536;
                        const unsigned gx=(wx+2048-cam.x)%2048,gy=(wy+1536-cam.y)%1536;
                        if(gx<8 || gx>=256 || gy>=192 || mask[gy*256+gx])continue;
                        const unsigned at=((r[2]&0x0e)<<10)|((((gy+r[9])%224)/8)<<6)|((((gx-r[8])&255)/8)<<1);
                        const unsigned tile=(shown.vdp.vram[at]|(shown.vdp.vram[at+1]<<8))&0x1ff;
                        if(same_tile[tile]<0)same_tile[tile]=std::memcmp(f.patterns.data()+tile*32,shown.vdp.vram.data()+tile*32,32)==0;
                        if(!same_tile[tile])continue;
                        ++compared;
                        if(f.bands[y*96+k]!=on.machine().picture()[gy*256+gx])
                            throw std::runtime_error(stem+": band pixel of frame "+std::to_string(f.frame)+" at x "+std::to_string(x)+" line "+
                                std::to_string(y)+" differs from the centre of frame "+std::to_string(n));
                    }
                    if(compared) { ++c.scroll_pairs;c.scroll_pixels+=compared; }
                }
                Past p{n,stay,digest,cam,shown.area,std::vector<std::uint8_t>(96*192),
                       std::vector<std::uint8_t>(shown.vdp.vram.begin(),shown.vdp.vram.begin()+0x4000)};
                for(unsigned y=0;y<192;++y)for(unsigned k=0;k<96;++k)p.bands[y*96+k]=(*wide)[y*344+(k<52 ? k : 248+k)];
                past.push_back(std::move(p));
                while(!past.empty() && n-past.front().frame>96)past.pop_front();
                c.window_used+=view->window_used();
            }
            was_anchored=widened;
            // The screen window equals the reader when settled: a map frame
            // whose page has not changed since the previous frame.
            const auto& m=on.machine();
            const std::array<std::uint8_t,2> page{m.peek(0xC302),m.peek(0xC306)};
            if(m.peek(0xC202)==0x09 && prev_c202_after==0x09 && prev.valid && prev.page==page && (m.vdp().registers()[1]&0x40)) {
                const game::MapArea area{static_cast<std::uint16_t>(m.peek(0xC260)|(m.peek(0xC261)<<8)),m.peek(0xC262),m.peek(0xC263)};
                for(unsigned k=0;k<4;++k) {
                    const auto screen=reader.decode(area,page[0]+(k&1),page[1]+(k>>1));
                    check(screen.has_value(),stem+": window area readable at frame "+std::to_string(n));
                    for(unsigned i=0;i<192;++i)
                        check(m.peek(static_cast<std::uint16_t>(0xCC00+k*256+i))==(*screen)[i],
                              stem+": settled screen window differs from the ROM at frame "+std::to_string(n)+" slot "+std::to_string(k)+" id "+std::to_string(i));
                }
                ++c.window_frames;
            }
            prev_c202_after=m.peek(0xC202);
            const auto& regs=m.vdp().registers();
            prev={m.peek(0xC202),regs[0],regs[1],regs[8],regs[9],m.peek(0xC300),m.peek(0xC301),m.peek(0xC302),m.peek(0xC304),
                  m.peek(0xC305),m.peek(0xC306),page,true};
        }
    } catch(...) { off_thread.join();throw; }
    off_thread.join();
    if(off_error)std::rethrow_exception(off_error);
    for(std::uint64_t n=0;n<rec.frames;++n)
        if(off_trace[n]!=on_trace[n])throw std::runtime_error(stem+": the machine with the option on differs from the option off after frame "+std::to_string(n));
    check(off.machine().equivalent(on.machine()),stem+": machines equivalent at the end");
    if(c.map_frames>0) check(c.widened>0 && c.centre_frames>0,stem+": map frames widen and agree with the centre");
    // No recording flies the ship (its own test does); the ship branches
    // above would need that test's checks if one ever did.
    check(c.mode4_5==0,stem+": a recording reaches the space flight; extend this test");
    // The held camera never needed a correction on the recorded paths.
    check(on.wide_view()->corrections()==0 && oracle_corrections==0,stem+": a consistent map frame disagreed with the held camera");
    // The scroll and window checks are not vacuous where they can apply.
    if(c.widened>=100) check(c.window_used>0 && c.window_frames>0,stem+": the screen window was used and checked");
    if(moving>=moving_for_scroll) check(c.scroll_pairs>0 && c.scroll_pixels>0,stem+": band pixels were compared with a later centre");
    std::cout<<s.region<<" "<<stem<<": "<<rec.frames<<" frames equal with the option off and on; gating oracle equal on every frame, no camera correction, "
             <<moving<<" moving widened frames, at most "<<max_pre_anchor<<" map frames before a stay's anchor; eligible "<<c.eligible<<" (";
    for(auto [mode,count]:c.eligible_by_mode)std::cout<<" "<<hex(mode)<<":"<<count;
    std::cout<<" ), widened "<<c.widened<<" (";
    for(auto [mode,count]:c.widened_by_mode)std::cout<<" "<<hex(mode)<<":"<<count;
    std::cout<<" ), map frames before an anchor "<<c.pre_anchor<<", stays "<<c.runs<<" ("<<c.runs_widened<<" widened; "<<c.transfer_runs
             <<" with a transfer fade, "<<c.dungeon_runs<<" with a dungeon entry, "<<c.interaction_runs<<" with an interaction load); never widened: "
             <<c.r0_06<<" frames with R0 = 06h; "<<c.mode4_5<<" ship frames; "<<c.display_off_family<<" display off; centre "<<c.centre_frames
             <<" frames / "<<c.centre_pixels<<" pixels equal, "<<c.late_patterns<<" skipped (patterns written before line 192); scroll "
             <<c.scroll_pairs<<" frame pairs / "<<c.scroll_pixels<<" band pixels equal to a later centre; window used in "<<c.window_used
             <<" frames, settled window equal to the ROM in "<<c.window_frames<<" frames; "<<timer.seconds()<<" s\n";
}

// ----------------------------------------------------------------- probes
// Plays the recording with the option off up to the first frame for which
// at(n, driver) is true; returns that frame or nothing.
std::optional<std::uint64_t> play_until(app::FrameDriver& d,const replay::Recording& rec,std::uint64_t from,
                                        const std::function<bool(std::uint64_t,app::FrameDriver&)>& at) {
    for(std::uint64_t n=from;n<rec.frames;++n) {
        d.run(rec.input_at(n));
        if(at(n,d))return n;
    }
    return std::nullopt;
}
bool map_state(const Machine& m,std::uint8_t mode) {
    return m.peek(0xC202)==mode && m.vdp().registers()[0]==0x26 && (m.vdp().registers()[1]&0x40);
}
void centre_check(app::FrameDriver& d,const std::string& what) {
    const auto* view=d.wide_view();
    const auto mask=sprite_mask(view->shown()->vdp);
    centre_agrees(*view,d.machine().picture(),&mask,what);
}
void probes(const Setup& s,const std::filesystem::path& other_path) {
    Timer timer;
    // A recording with long stays in a town.
    const std::string stem="later-town";
    const auto rec=recording(s,stem);
    {   // Option on during a transfer fade (mode 8, display on; first-battle
        // has one): pillarbox until the next map anchor, then the centre agrees.
        const auto transfer=recording(s,"first-battle");
        const auto folder=fresh(s,"probe-transfer");place_image(s,"first-battle",transfer,folder);
        app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
        const auto at=play_until(d,transfer,0,[](std::uint64_t,app::FrameDriver& x){ return map_state(x.machine(),0x08); });
        check(at.has_value(),"first-battle has a transfer fade");
        d.set_widescreen(true);
        bool seen_map=false;std::uint64_t n=*at+1,pillarboxed=0;
        for(;n<transfer.frames;++n) {
            if(d.machine().peek(0xC202)==0x09)seen_map=true;
            d.run(transfer.input_at(n));
            if(d.wide_picture()) { check(seen_map,"no picture before a map frame is paired");centre_check(d,"transfer probe");break; }
            ++pillarboxed;
        }
        check(n<transfer.frames,"the transfer probe widens again on the map");
        std::cout<<s.region<<" probe: option on during a transfer fade at frame "<<*at<<": "<<pillarboxed<<" frames pillarboxed until the map anchor\n";
    }
    std::uint64_t mid=0;
    {   // Option on mid-stay: the first frame has nothing paired; a map frame
        // anchors within a few frames.
        const auto folder=fresh(s,"probe-midstay");place_image(s,stem,rec,folder);
        app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
        unsigned streak=0;
        const auto at=play_until(d,rec,0,[&](std::uint64_t,app::FrameDriver& x){ streak=map_state(x.machine(),0x09) ? streak+1 : 0;return streak==120; });
        check(at.has_value(),"later-town has a stay of 120 map frames");
        mid=*at;
        d.set_widescreen(true);
        d.run(rec.input_at(mid+1));
        check(!d.wide_picture(),"nothing is paired on the first frame after the option is switched on");
        unsigned waited=1;std::uint64_t n=mid+2;
        for(;n<mid+40 && !d.wide_picture();++n,++waited)d.run(rec.input_at(n));
        check(d.wide_picture()!=nullptr,"the map anchors within a few frames of the option");
        centre_check(d,"mid-stay probe");
        // Off again: no picture, nothing kept; on again starts over.
        d.set_widescreen(false);check(!d.widescreen() && !d.wide_picture() && !d.wide_view(),"option off forgets");
        d.set_widescreen(true);check(!d.wide_picture(),"option on starts over");
        std::cout<<s.region<<" probe: option on mid-stay at frame "<<mid<<": "<<waited<<" frames pillarboxed\n";
        // Power cycle: nothing widens until the game reaches a map again.
        for(unsigned k=0;k<40 && !d.wide_picture();++k)d.run(rec.input_at(n+k));
        check(d.wide_picture()!=nullptr,"widened before the power cycle");
        check(!d.power_cycle(),"power cycle");
        check(!d.wide_picture(),"a power cycle drops the picture");
        for(unsigned k=0;k<300;++k) { d.run({});check(!d.wide_picture(),"nothing widens while the game boots"); }
        std::cout<<s.region<<" probe: power cycle drops the anchor; 300 boot frames pillarboxed\n";
    }
    {   // Recording mode and --no-routines refuse the option.
        app::FrameDriver recording_mode(s.rom,app::SaveStore(fresh(s,"probe-recording-mode"),s.version),true,{},true);
        recording_mode.set_widescreen(true);
        app::FrameDriver no_routines(s.rom,app::SaveStore(fresh(s,"probe-no-routines"),s.version),false);
        no_routines.set_widescreen(true);
        check(!recording_mode.widescreen() && !no_routines.widescreen(),"recording mode and --no-routines stay at the original picture");
    }
    if(!other_path.empty() && std::filesystem::exists(other_path)) {
        // ROM swap: the Options hold at the map wait, a language change, the
        // return swaps the image; the reader then reads the new image and
        // the centre agrees again.
        const auto other=read_rom(other_path);
        app::SessionRoms session;
        session.source_version=s.version;
        const bool us=s.version==RomVersion::us_rev_a;
        session.images={us ? s.rom : other,us ? other : s.rom};
        session.active=us ? app::Language::english : app::Language::portuguese;
        const auto folder=fresh(s,"probe-swap");place_image(s,stem,rec,folder);
        app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
        d.configure_languages(session);
        for(std::uint64_t n=0;n<=mid;++n)d.run(rec.input_at(n));
        d.set_widescreen(true);
        for(unsigned k=0;k<10;++k)d.run({});
        check(d.wide_picture()!=nullptr,"widened before the swap");
        bool opened=false;
        for(unsigned k=0;k<120 && !opened;++k) { d.run({});if(d.request_menu()) { d.run({});opened=d.menu_active(); } }
        check(opened,"the Options hold opens on the map");
        d.prepare_language(us ? app::LanguagePreference::portuguese : app::LanguagePreference::english);
        d.commit_language();d.close_menu();
        const auto changes=d.language_changes();
        unsigned k=0;
        for(;k<100 && d.language_changes()==changes;++k)d.run({});
        check(d.language_changes()==changes+1,"the language swap happened");
        check(!d.wide_picture(),"the swap frame pillarboxes (reader refreshed, anchor dropped)");
        std::uint64_t widened=0;
        for(unsigned j=0;j<60;++j) { d.run({});if(d.wide_picture()) { centre_check(d,"after the ROM swap");++widened; } }
        check(widened>0,"widened again after the swap");
        std::cout<<s.region<<" probe: ROM swap pillarboxes the swap frame; "<<widened<<" of 60 later frames widened, centre equal\n";
    } else throw Skip{"the ROM swap probe needs the other ROM; every other probe passed"};
    std::cout<<s.region<<" probes passed in "<<timer.seconds()<<" s\n";
}
}

// ------------------------------------------------------------------ ship
// The space flight, reached as the game reaches it: later-town's start image
// to frame 4990 (an interaction scene, mode 0Dh), the route byte C2E9 written
// there, the recording's input until the ship loader (mode 4) runs, then no
// input. Option on and off side by side: machines equal every frame.
struct ShipCounts {
    std::uint64_t frames=0,pillarbox=0,map=0,space=0,fade_widened=0,fade_frames=0,tolerated=0,tolerated_frames=0,space_band_pixels=0;
    std::uint64_t scene=0;   // frames of modes 4 and 5
};
// One pixel of the name table wrapped past the screen edges, for a picture
// with register 0 = 26h (no scroll locks): wide x shows name-table column
// (x - 44 - R8) modulo 256. Written here from the VDP rules, apart from the
// view's renderer.
std::uint8_t wrapped_pixel(const VdpState& v,unsigned x,unsigned line) {
    const auto& r=v.registers;
    const unsigned column=static_cast<unsigned>((static_cast<int>(x)-44-r[8]+512)%256);
    const unsigned y=(line+r[9])%224;
    const unsigned at=((r[2]&0x0e)<<10)|(((y>>3)&((r[2]&1) ? 31u : 15u))<<6)|((column>>3)<<1);
    const unsigned name=v.vram[at]|(v.vram[at+1]<<8);
    const unsigned row=(name&0x400) ? 7-(y&7) : (y&7);
    const unsigned pattern=(name&0x1ff)*32+row*4;
    const unsigned low=pattern&((unsigned(r[3])<<6)|0x3f),high=pattern&(((r[4]&7u)<<11)|0x7ff);
    const unsigned bit=(name&0x200) ? (column&7) : 7-(column&7);
    const unsigned colour=((v.vram[low]>>bit)&1)|(((v.vram[low+1]>>bit)&1)<<1)|(((v.vram[high+2]>>bit)&1)<<2)|(((v.vram[high+3]>>bit)&1)<<3);
    return v.cram[colour+((name&0x800) ? 16 : 0)];
}
// Lines of a name-table row only partly on the screen (top and bottom).
bool partial_row(unsigned y,unsigned r9) { const unsigned top=y-((y+r9)%8); return y<((y+r9)%8) || top+8>192; }
struct ShipRun {
    std::optional<std::uint64_t> switch_on,switch_off;     // frame indices after mode 4 (option off before)
    bool skip=false;                                      // press button 1 every 60 frames of mode 5
};
ShipCounts fly(const Setup& s,const Machine& base,const replay::Recording& rec,std::uint64_t at,unsigned route,const ShipRun& run,
               const std::string& what,WideSnapshot* map_copy_out=nullptr,WideSnapshot* space_copy_out=nullptr) {
    app::FrameDriver on(s.rom,app::SaveStore(fresh(s,what+"-on"),s.version)),off(s.rom,app::SaveStore(fresh(s,what+"-off"),s.version));
    on.machine()=base;off.machine()=base;
    on.machine().memory().write(0xC2E9,static_cast<std::uint8_t>(route));off.machine().memory().write(0xC2E9,static_cast<std::uint8_t>(route));
    on.set_widescreen(!run.switch_on);
    ShipCounts c;
    bool reached4=false,seen5=false,back9=false;unsigned after9=0;std::uint64_t since4=0,mode5=0;
    for(std::uint64_t f=0;f<5000;++f) {
        if(!reached4 && on.machine().peek(0xC202)==4)reached4=true;
        FrameInput input=reached4 ? FrameInput{} : rec.input_at(at+f);
        if(run.skip && on.machine().peek(0xC202)==5 && ++mode5%60==30)input.pads[0].one=true;
        if(reached4) {
            if(run.switch_on && since4==*run.switch_on)on.set_widescreen(true);
            if(run.switch_off && since4==*run.switch_off)on.set_widescreen(false);
            ++since4;
        }
        const auto& m0=on.machine();
        const std::uint8_t mode=m0.peek(0xC202);
        const bool switched=run.switch_on && since4==*run.switch_on+1;
        const auto a=on.run(input),b=off.run(input);
        check(!a.save_error && !b.save_error,what+": save error");
        check(fingerprint(on.machine())==fingerprint(off.machine()) && writes_hash(a.writes)==writes_hash(b.writes),
              what+": the machine with the option on differs after frame "+std::to_string(f));
        const auto* wide=on.wide_picture();
        const auto* view=on.wide_view();
        if(switched) check(!wide,what+": nothing is paired on the frame the option is switched on");
        ++c.frames;
        if(mode==4 || mode==5) {
            ++c.scene;
            if(!wide)++c.pillarbox;
            else {
            const auto& shown=*view->shown();
            const auto& picture=on.machine().picture();
            const auto mask=sprite_mask(shown.vdp);
            WidePicture own{};view->render(0,app::wide_width,own);
            if(view->source()==app::WideSource::map) {
                ++c.map;
                if(map_copy_out)*map_copy_out=shown;
                // Centre agreement; a row only partly on the screen may already
                // hold the starfield the scene uploads (the fade's first lines).
                const auto before=c.tolerated;
                for(unsigned y=0;y<192;++y)for(unsigned x=8;x<256;++x) {
                    if(mask[y*256+x] || own[y*344+44+x]==picture[y*256+x])continue;
                    if(!partial_row(y,shown.vdp.registers[9]%224))
                        throw std::runtime_error(what+": ship map frame "+std::to_string(f)+" differs at line "+std::to_string(y)+" x "+std::to_string(x));
                    ++c.tolerated;
                }
                c.tolerated_frames+=c.tolerated>before;
            } else {
                check(view->source()==app::WideSource::space && shown.vdp.registers[8]==0,what+": space source with register 8 at 0");
                ++c.space;
                if(space_copy_out)*space_copy_out=shown;
                centre_agrees(*view,picture,&mask,what+" space frame "+std::to_string(f));
                // The bands equal a wrap of the name table computed here from
                // VRAM (independent of mode4::render_wrapped), and their
                // colours are colours the centre's background shows (the
                // starfield and the backdrop). This proves the bands are the
                // scene's own name table; the VDP draws no such columns, so
                // nothing proves what they "would" show.
                std::array<bool,64> shown_colours{};
                for(unsigned i=0;i<picture.size();++i)if(!mask[i] && i%256>=8)shown_colours[picture[i]&63]=true;
                shown_colours[shown.vdp.cram[16+(shown.vdp.registers[7]&15)]&63]=true;
                for(unsigned y=0;y<192;++y)for(unsigned x=0;x<344;++x) {
                    if(x>=52 && x<300)continue;
                    check((*wide)[y*344+x]==wrapped_pixel(shown.vdp,x,y),what+": space band pixel is not the wrapped name table at frame "+std::to_string(f));
                    check(shown_colours[(*wide)[y*344+x]&63],what+": space band colour not in the starfield at frame "+std::to_string(f));
                    ++c.space_band_pixels;
                }
            }
            }
            seen5=seen5 || mode==5;
        } else if(seen5 && mode==8) {
            ++c.fade_frames;
            if(wide) { ++c.fade_widened;centre_check(on,what+" fade frame "+std::to_string(f)); }
        }
        if(seen5 && on.machine().peek(0xC202)==9)back9=true;
        if(back9 && ++after9>90)break;
    }
    check(back9,what+": the flight ends on a map");
    return c;
}
void ship_part(const Setup& s,const std::string& routes_text) {
    Timer timer;
    const auto rec=recording(s,"later-town");
    constexpr std::uint64_t at=4990;
    const Machine base=[&] {
        const auto folder=fresh(s,"ship-image");place_image(s,"later-town",rec,folder);
        app::FrameDriver loaded(s.rom,app::SaveStore(folder,s.version));
        for(std::uint64_t n=0;n<at;++n)loaded.run(rec.input_at(n));
        check(loaded.machine().peek(0xC202)==0x0D,"later-town is in an interaction scene at frame 4990");
        return Machine(loaded.machine());
    }();
    std::vector<unsigned> routes;bool extra=false;
    for(std::size_t i=0;i<routes_text.size();) {
        auto end=routes_text.find(',',i);if(end==std::string::npos)end=routes_text.size();
        const auto token=routes_text.substr(i,end-i);i=end+1;
        if(token=="extra")extra=true; else routes.push_back(static_cast<unsigned>(std::stoul(token,nullptr,16)));
    }
    check(!routes.empty(),"routes given");
    WideSnapshot map_snapshot,space_snapshot;
    for(const auto route:routes) {
        const auto name=s.region+"-route-"+hex(route);
        const auto c=fly(s,base,rec,at,route,{},name,&map_snapshot,&space_snapshot);
        // The ship study's classification (identical on every route and both
        // ROMs): 114 frames before the display shows the scene's picture, two
        // with the name table being replaced, 606 over a map, 860 in space.
        check(c.pillarbox==116 && c.map==606 && c.space==860,name+": classification "+std::to_string(c.pillarbox)+" pillarboxed, "+
              std::to_string(c.map)+" map, "+std::to_string(c.space)+" space");
        check(c.fade_frames>0 && c.fade_widened==c.fade_frames,name+": the fade after the descent widens on every frame");
        check(c.tolerated_frames<=3,name+": partial-row mismatches on at most the three frames before space ("+std::to_string(c.tolerated_frames)+")");
        std::cout<<name<<": "<<c.scene<<" ship frames: "<<c.pillarbox<<" pillarboxed, "<<c.map<<" map, "<<c.space<<" space ("<<c.space_band_pixels
                 <<" band pixels from the starfield); centre equal ("<<c.tolerated<<" pixels in partial rows on "<<c.tolerated_frames<<" frames tolerated); fade "<<c.fade_widened<<"/"
                 <<c.fade_frames<<" widened and equal; machines equal with the option off\n";
    }
    if(extra) {   // Option on during the take-off and again in space: nothing on the
        // switch frame, then the scene's own rule at once.
        const auto c=fly(s,base,rec,at,routes.front(),{140,700},s.region+"-switch-on");
        const auto d=fly(s,base,rec,at,routes.front(),{900,std::nullopt},s.region+"-switch-space");
        // Over a map it widens at once; in space it waits for the descent:
        // the view cannot tell space from the departure fade without having
        // seen the scene's take-off.
        check(c.map>0,"the option switched on during the take-off widens at once");
        check(d.space==0 && d.map>0,"the option switched on in space pillarboxes until the descent");
        std::cout<<s.region<<" option on mid-flight: take-off "<<c.map<<" map frames until switched off; on in space: "<<d.space<<" space (pillarbox), "
                 <<d.map<<" descent map frames\n";
    }
    if(extra) {   // A flight skipped with button 1: every widened frame agrees; the fade
        // widens or pillarboxes (a jump above 2 px).
        ShipRun skip;skip.skip=true;
        const auto c=fly(s,base,rec,at,routes.front(),skip,s.region+"-skip");
        check(c.scene<116+606+860,"button 1 shortens the flight");
        std::cout<<s.region<<" skipped flight: "<<c.scene<<" ship frames ("<<c.map<<" map, "<<c.space<<" space, "<<c.pillarbox<<" pillarboxed), fade "
                 <<c.fade_widened<<"/"<<c.fade_frames<<" widened\n";
    }
    // Wall-clock (reported, not asserted): the ship branch and its pictures.
    auto bench=[&](const WideSnapshot& snapshot,const char* label) {
        WideView view(s.rom);Vdp::Picture centre{};
        view.push(map_snapshot);view.push(map_snapshot);   // the scene has started
        view.push(snapshot);
        constexpr int rounds=4000;
        auto start=std::chrono::steady_clock::now();
        for(int k=0;k<rounds;++k)view.push(snapshot);
        const double update=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/rounds*1e6;
        check(view.widened(),"benchmark copy widens");
        start=std::chrono::steady_clock::now();
        for(int k=0;k<rounds/4;++k) { view.push(snapshot);(void)view.picture(centre); }
        const double total=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/(rounds/4)*1e6;
        std::cout<<s.region<<" ship "<<label<<" frame: copy pairing and rule "<<update<<" us, with the picture "<<total<<" us\n";
    };
    bench(map_snapshot,"map");bench(space_snapshot,"space");
    std::cout<<s.region<<" ship test passed in "<<timer.seconds()<<" s\n";
}

// Not a test (wall-clock): the option's cost per frame on one recording.
// Whole replays with the option off and on (best of three each, interleaved),
// the time spent in wide_picture() per widened frame, and the copy alone.
void timing(const Setup& s,const std::string& stem) {
    const auto rec=recording(s,stem);
    struct Result { double frame_us=0,picture_us=0;std::uint64_t widened=0; };
    auto replay=[&](bool on) {
        const auto folder=fresh(s,on ? "timing-on" : "timing-off");place_image(s,stem,rec,folder);
        app::FrameDriver d(s.rom,app::SaveStore(folder,s.version));
        d.set_widescreen(on);
        Result r;double picture=0;
        const auto start=std::chrono::steady_clock::now();
        for(std::uint64_t n=0;n<rec.frames;++n) {
            d.run(rec.input_at(n));
            if(on) {
                const auto a=std::chrono::steady_clock::now();
                if(d.wide_picture())++r.widened;
                picture+=std::chrono::duration<double>(std::chrono::steady_clock::now()-a).count();
            }
        }
        r.frame_us=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/static_cast<double>(rec.frames)*1e6;
        r.picture_us=r.widened ? picture/static_cast<double>(r.widened)*1e6 : 0;
        return r;
    };
    Result off{1e9},on{1e9};
    for(int k=0;k<3;++k) {
        const auto a=replay(false),b=replay(true);
        if(a.frame_us<off.frame_us)off=a;
        if(b.frame_us<on.frame_us)on=b;
    }
    Machine m(s.rom);
    WideView view(s.rom);
    const auto start=std::chrono::steady_clock::now();
    for(int k=0;k<20000;++k)view.capture(m);
    const auto copy_us=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()/20000*1e6;
    std::cout<<s.region<<" "<<stem<<" ("<<rec.frames<<" frames): option off "<<off.frame_us<<" us/frame, option on "<<on.frame_us
             <<" us/frame including "<<on.picture_us<<" us per widened picture ("<<on.widened<<" widened); copy and camera alone "<<copy_us<<" us\n";
}

int main(int argc,char** argv) {
    try {
        if(argc<2)throw std::runtime_error("Expected unit | rom <rom> <us|br> | recording <rom> <us|br> <recordings> <reference> <work> <stem> | probes <rom> <us|br> <recordings> <reference> <work> <other rom>.");
        const std::string_view part=argv[1];
        if(part=="unit") { unit();return 0; }
        const bool full=part=="recording" || part=="probes" || part=="timing" || part=="ship";
        if((part!="rom" && !full) || argc!=(full ? 8 : 4))throw std::runtime_error("Unknown part or wrong arguments.");
        const std::string_view region=argv[3];
        if(region!="us" && region!="br")throw std::runtime_error("Expected ROM version us or br.");
        if(!*argv[2] || !std::filesystem::exists(argv[2]))return 77;
        if(identify_rom(argv[2])!=(region=="us" ? RomVersion::us_rev_a : RomVersion::brazilian))throw std::runtime_error("Wrong ROM version.");
        auto rom=read_rom(argv[2]);
        Setup s{rom,identify_digest(sha1(*rom)),std::string(region),{},{},{}};
        if(part=="rom") { rom_part(s);return 0; }
        s.recordings=argv[4];s.reference=argv[5];s.work=argv[6];
        std::filesystem::create_directories(s.work);
        if(part=="recording")recording_part(s,argv[7]);
        else if(part=="timing")timing(s,argv[7]);
        else if(part=="ship")ship_part(s,argv[7]);
        else probes(s,argv[7]);
        return 0;
    } catch(const Skip& skip) { std::cout<<"Skipped: "<<skip.why<<'\n';return 77; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
