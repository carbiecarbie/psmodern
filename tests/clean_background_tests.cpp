// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/clean_background.h"
#include "app/frame_driver.h"
#include "app/host.h"
#include "app/retro_font.h"
#include "game/menu.h"
#include "probe.h"
#include <algorithm>

using namespace psmodern;
using namespace psmodern::app;
using probe::check;
namespace {
constexpr auto frame=Console::frame_cycles;
constexpr std::uint8_t red=3, green=12, blue=48;
constexpr std::uint16_t test_cell=0x3910;
constexpr std::size_t test_pixel=32*256+64;
void port(Machine& m,std::uint8_t number,std::uint8_t value) {
    m.write_port(number,value,m.cycles());
}
void address(Machine& m,std::uint16_t command) {
    port(m,0xbf,static_cast<std::uint8_t>(command));
    port(m,0xbf,static_cast<std::uint8_t>(command>>8));
}
void data(Machine& m,std::uint8_t byte) { port(m,0xbe,byte); m.advance(32); }
void palette(Machine& m,std::uint8_t index,std::uint8_t color) {
    address(m,0xc000|index); data(m,color);
}
void write_word(Machine& m,std::uint16_t at,std::uint16_t value) {
    m.memory().write(at,static_cast<std::uint8_t>(value));
    m.memory().write(at+1,static_cast<std::uint8_t>(value>>8));
}
void begin_scene(Machine& m) {
    // Three authored, solid tiles; all untouched name cells select red.
    for (const auto command:{0x8000,0x8140,0x82ff,0x83ff,0x84ff,0x857e})
        address(m,static_cast<std::uint16_t>(command));
    address(m,0x4000);
    for(unsigned color=1;color<=3;++color) for(unsigned row=0;row<8;++row)
        for(unsigned plane=0;plane<4;++plane) data(m,color&(1u<<plane) ? 0xff : 0);
    address(m,0x7f00); data(m,0xd0); // no sprites in the test scene
    m.memory().write(0xc400,0xd0);
    palette(m,1,red); palette(m,2,green); palette(m,3,blue);
    // Background index 0 and the same three entries in palette 2 are
    // distinct from red so that native labels and borders remain visible.
    palette(m,0,0); palette(m,16,0);
    palette(m,17,blue); palette(m,18,blue); palette(m,19,blue);
    m.cpu().im=1;
    (void)m.read_port(0xbf,m.cycles());
}
// Drive the registered original routines to a real return or a named next
// routine. Both runs see the same inputs, addresses, frame waits and IRQs.
// Comparing all hardware and each port transaction includes the saved-window
// RAM, VDP address/read-buffer pipeline, canonical pixels, and PSG writes.
struct Rig {
    Machine on,off;
    CleanBackground clean;
    RoutineRegistry routines;
    explicit Rig(const probe::Rom& rom):on(rom.image),off(rom.image),clean(on) {
        check(game::register_routines(routines,rom.sha1,{}),"identified game routines");
        on.set_video_observer(&clean);
        both(begin_scene);
        scan();
    }
    template<class F> void both(F f) { f(on); f(off); }
    void equal(const char* site) { check(on.equivalent(off),std::string(site)+" changed canonical state"); }
    const CleanFrame& scan() {
        // Let a complete picture be scanned with the prepared state.
        for(int n=0;n<2;++n) {
            both([](Machine& m) { m.take_samples();m.take_psg_writes();m.advance(frame-m.cycles()%frame); });
            clean.capture(on);
        }
        equal("scanout");
        check(clean.shown() && clean.shown()->native==off.picture(),"completed native picture");
        return *clean.shown();
    }
    template<class Prepare> void run(std::uint16_t pc,std::uint16_t stop,Prepare prepare) {
        both([&](Machine& m) {
            m.cpu().pc.uint16_value=pc;
            m.cpu().sp.uint16_value=0xd680;
            write_word(m,0xd680,0xaaaa);
            prepare(m);
        });
        Console::PortTrace traced(on),plain(off);
        const auto limit=on.cycles()+20*frame;
        const auto execute=[&](Machine& m) {
            while(m.cpu().pc.uint16_value!=stop) {
                if(m.cycles()>=limit) throw std::runtime_error("Cartridge routine did not reach boundary "+std::to_string(stop));
                auto before=m.cycles();
                m.step(routines);
                if (before/frame!=m.cycles()/frame) { m.take_samples();m.take_psg_writes(); }
            }
        };
        execute(on); execute(off);
        equal("routine boundary");
        check(traced.accesses()==plain.accesses(),"port reads/writes or their T-states differ");
    }
    void rect(bool save,std::uint16_t buffer,std::uint16_t dest,unsigned count) {
        run(save?0x3aa6:0x3a83,0xaaaa,[&](Machine& m) {
            m.cpu().hl.uint16_value=buffer;
            m.cpu().de.uint16_value=dest|0x4000;
            m.cpu().bc.uint16_value=static_cast<std::uint16_t>(0x0100|count);
        });
    }
};
void compose(const CleanFrame& f,std::size_t revealed,std::uint8_t color) {
    std::vector<std::uint32_t> pixels(256*192);
    centre_pixels(pixels,f.background,nullptr);
    retro::box(pixels,28,88,94,28);
    retro::text(pixels,38,99,"Aa");
    check(pixels.at(revealed)==rgb(color),"incorrect exposed pixel beside the new window");
    check(std::count(pixels.begin()+99*256+38,pixels.begin()+100*256+52,retro::white)>0,
          "VWF line missing from the composited window");
}
void nested_and_changing(Rig& g) {
    g.both([](Machine& m) { write_word(m,0xc800,1); write_word(m,0xc802,1); write_word(m,0xc810,2); });
    g.rect(true,0xd700,test_cell,4);
    g.rect(false,0xc800,test_cell,4);
    g.rect(true,0xd710,test_cell,2);
    check(g.on.peek(0xd710)==1 && g.on.peek(0xd700)==0,"nested save read the native parent");
    g.rect(false,0xc810,test_cell,2);
    const auto& child=g.scan();
    check(child.native.at(test_pixel)==blue && child.background.at(test_pixel)==red,"nested foreground");
    compose(child,test_pixel,red);
    g.rect(false,0xd710,test_cell,2);
    const auto& parent=g.scan();
    check(parent.native.at(test_pixel)==green && parent.background.at(test_pixel)==red,"nested restore");
    compose(parent,test_pixel,red);

    // A genuine scene writer changes a name under the open parent. Redrawing
    // just its first cell and then closing it copies older native rectangles.
    g.run(0x0363,0xaaaa,[](Machine& m) {
        m.cpu().de.uint16_value=test_cell|0x4000;
        m.cpu().bc.uint16_value=2;
        m.cpu().hl.uint16_value=2;
    });
    g.rect(false,0xc800,test_cell,2);
    const auto& partial=g.scan();
    check(partial.native.at(test_pixel)==green && partial.native.at(test_pixel+8)==blue &&
          partial.background.at(test_pixel)==blue,"partial redraw held an old scene cell");
    compose(partial,test_pixel,blue);
    // Run the real fade, flash and interrupt upload in vblank. The scene
    // under the native cell follows each new phase on the next scanout.
    for (bool flash:{false,true}) {
        const auto old=g.on.vdp().cram()[3];
        const auto phase=flash?63:32;
        g.both([](Machine& m) { m.take_samples();m.advance(194*228); });
        g.run(flash?0x7bee:0x7b40,0xaaaa,[&](Machine& m) {
            for(unsigned i=0;i<32;++i) m.memory().write(game::menu::palette+i,m.vdp().cram()[i]);
            if(flash) {
                m.memory().write(game::menu::flash_timer,2);
                write_word(m,game::menu::flash_range,0x0103);
            } else {
                const auto timer=m.peek(0x7b48)|(m.peek(0x7b49)<<8);
                m.memory().write(0xc21d,0);
                m.memory().write(timer,1); m.memory().write(timer+1,4);
            }
        });
        g.run(0x017a,0x0186,[](Machine&){}); // The frame handler uploads the palette.
        check(g.on.vdp().cram()[3]==phase,"native palette phase expected "+std::to_string(phase)+
              ", RAM "+std::to_string(g.on.peek(game::menu::palette+3))+", CRAM "+std::to_string(g.on.vdp().cram()[3]));
        const auto& changed=g.scan();
        check(changed.background.at(test_pixel)==phase,"clean picture missed a cartridge palette phase");
        // A mid-scanline-region change cannot be reconstructed by applying
        // the last CRAM indiscriminately to a whole already-scanned frame.
        g.both([&](Machine& m) {
            m.take_samples();m.advance(36*228);
            palette(m,3,old);
            m.advance(frame-m.cycles()%frame);
        });
        g.clean.capture(g.on);
        const auto& split=*g.clean.shown();
        check(split.background.at(test_pixel)==phase && split.background.at(test_pixel+7*256)==old,
              "palette phase crossed the wrong scanline");
        g.equal("palette transition");
        g.both([&](Machine& m) { palette(m,3,static_cast<std::uint8_t>(phase)); });
        g.scan();
    }
    g.both([](Machine& m) { palette(m,1,red);palette(m,2,green); });
    g.rect(false,0xd700,test_cell,4);
    const auto& closed=g.scan();
    check(closed.native.at(test_pixel)==red && closed.background.at(test_pixel)==63 &&
          closed.background.at(test_pixel+8)==63,"close restored a stale scene cell");
    compose(closed,test_pixel,63);
    std::cout<<"Nested restore and changing scene/partial redraw/close passed.\n";
}
void member_cursor_scroll_opening(Rig& g) {
    // Stop at the first frame wait after the real caller's row draw. The
    // revealing output is scanned without advancing the game past that wait.
    constexpr std::uint16_t wait=0x0052;
    constexpr std::size_t member_pixel=144*256;
    g.run(0x2f93,wait,[](Machine& m){m.cpu().af.uint8_values.at_1=0;});
    const auto& member=g.scan();
    check(member.native.at(member_pixel)!=member.background.at(member_pixel) &&
          member.background.at(member_pixel)==red,"single-member window row was not removed");
    compose(member,member_pixel,red);
    g.run(0x2e62,0xaaaa,[](Machine& m) {
        m.memory().write(game::menu::selection_active,1);
        m.memory().write(game::menu::choice,0);
        m.memory().write(0xc26c,0);
        m.memory().write(game::menu::blink_phase,1);
        write_word(m,game::menu::cursor_address,test_cell|0x4000);
    });
    const auto& cursor=g.scan();
    check(cursor.native.at(test_pixel)!=cursor.background.at(test_pixel) &&
          cursor.background.at(test_pixel)==63,"cursor IRQ leaked into the clean picture");
    compose(cursor,test_pixel,63);

    // Mark the first column of the source row as a native window tile;
    // the cartridge saves its bytes in DB08 and copies them up one row.
    const auto scroll_source=static_cast<std::uint16_t>((g.on.peek(0x343d)|(g.on.peek(0x343e)<<8))&0x3fff);
    g.rect(false,0xc800,scroll_source,2);
    g.run(0x3439,wait,[](Machine&){});
    const auto& scroll=g.scan();
    constexpr std::size_t scrolled_pixel=152*256+56;
    check(g.on.peek(0xdb08)==1 && scroll.native.at(scrolled_pixel)==green &&
          scroll.background.at(scrolled_pixel)==red,"scroll did not preserve both the native row and the scene");
    compose(scroll,scrolled_pixel,red);

    // The direct caller's pre-tiled opening caption is scene content.
    g.run(0x436c,wait,[](Machine&){});
    const auto& opening=g.scan();
    constexpr std::size_t caption=2*8*256+3*8;
    bool exposed=false;
    for(unsigned x=0;x<160;++x) {
        check(opening.background.at(caption+x)==opening.native.at(caption+x),"opening row erased");
        exposed|=opening.native.at(caption+x)!=red;
    }
    check(exposed,"opening did not actually draw its caption");
    compose(opening,caption,opening.native.at(caption));
    std::cout<<"Single member, cursor, scroll and opening-row cases passed.\n";
}
void driver_pairing(const probe::Rom& rom,const std::filesystem::path& folder) {
    SaveStore config(folder,rom.version);
    const auto ram=config.load();
    check(!config.choose({},false,WidescreenMode::off,InterfaceMode::enhanced,ram),"seed enhanced preference");
    FrameDriver driver(rom.image,std::move(config));
    driver.set_clean_background(true);
    auto previous=driver.machine().memory().ram();
    auto previous_vram=driver.machine().vdp().vram();
    auto previous_cram=driver.machine().vdp().cram();
    auto previous_cycle=driver.machine().cycles();
    bool changed=false;
    driver.set_frame_probe([&](const Machine& m) {
        const auto* shown=driver.clean_frame();
        check(shown && shown->completed_cycle==m.cycles() && shown->state.captured_cycle==previous_cycle &&
              shown->native==m.picture() && shown->state.ram==previous && shown->state.cram==previous_cram &&
              shown->state.vram==previous_vram,"snapshot does not belong to this completed picture");
        changed|=previous!=m.memory().ram() || previous_vram!=m.vdp().vram();
        previous=m.memory().ram();previous_vram=m.vdp().vram();previous_cram=m.vdp().cram();previous_cycle=m.cycles();
    });
    for(int n=0;n<5;++n)driver.run({});
    check(changed,"pairing was only checked on identical snapshots");
    check(!driver.power_cycle() && !driver.clean_frame(),"power cycle exposed an old picture");
    previous=driver.machine().memory().ram();previous_vram=driver.machine().vdp().vram();
    previous_cram=driver.machine().vdp().cram();previous_cycle=0;
    driver.run({});
    check(driver.clean_frame()->native==driver.clean_frame()->background,"first reset picture was stale");
    // Exercise a physical console Reset as well. The game only polls it in
    // the frame IRQ. Wait for that first released sample before pressing it.
    FrameDriver original(rom.image,SaveStore(folder/"original",rom.version));
    original.run({});
    bool soft_reset=false;
    const auto tick=[&](FrameInput input) {
        driver.run(input);original.run(input);
        check(driver.machine().equivalent(original.machine()),"console Reset or frame observer changed the game");
    };
    for(unsigned i=0;i<180 && driver.machine().peek(0xc20b)!=0x10;++i) tick({});
    check(driver.machine().peek(0xc20b)==0x10,"console Reset was never polled released");
    const auto* reset_entry=driver.routines().find({0,0x0085});
    const auto prior_resets=reset_entry->counters.cpp_calls;
    driver.set_frame_probe([&](const Machine& m) {
        if (reset_entry->counters.cpp_calls>prior_resets && !soft_reset) {
            check(!driver.clean_frame(),"first frame across console Reset exposed a pre-reset snapshot");
            soft_reset=true;
        } else if(soft_reset) {
            const auto* shown=driver.clean_frame();
            check(shown && shown->native==m.picture() && shown->background==m.picture(),
                  "scene or native picture survived console Reset incorrectly");
        }
    });
    FrameInput held;held.reset=true;
    for(unsigned i=0;i<8 && !soft_reset;++i) tick(held);
    check(soft_reset,"console Reset did not re-enter the cartridge setup");
    tick({});
    driver.set_frame_probe({});
    const auto* frozen=driver.clean_frame();
    driver.set_clean_background(false);
    check(frozen && driver.clean_frame()==frozen,"disabling replaced an already published frame");
    tick({});
    check(!driver.clean_frame(),"disabled presentation kept drawing");
    // A cursor routine spans this frame boundary. It copies the new choice
    // into C26C *after* the observer but before run() returns. The following
    // picture must still carry C26C as the observer saw it.
    driver.set_clean_background(true);
    auto& m=driver.machine();
    m.cpu().iff1=m.cpu().iff2=0;
    m.cpu().pc.uint16_value=0x2e62; m.cpu().sp.uint16_value=0xd680;
    write_word(m,0xd680,0x0055);
    m.memory().write(0xc208,1);
    m.memory().write(game::menu::selection_active,1);
    m.memory().write(game::menu::choice,2);
    m.memory().write(0xc26c,0);
    write_word(m,game::menu::cursor_address,test_cell|0x4000);
    m.advance(frame-2-m.cycles()%frame);
    std::uint64_t cutoff=0;
    driver.set_frame_probe([&](const Machine& at) {
        cutoff=at.cycles();
        check(at.peek(0xc26c)==0,"cursor write did not follow the observer");
    });
    driver.run({});
    check(m.cycles()>cutoff && m.peek(0xc26c)==2,"cursor did not write during the overshoot");
    driver.set_frame_probe({});
    driver.run({});
    check(driver.clean_frame() && driver.clean_frame()->state.captured_cycle==cutoff &&
          driver.clean_frame()->state.ram[0x26c]==0,"picture used RAM from the overshoot");
    check(!driver.choose_options({},false,WidescreenMode::off,InterfaceMode::original) && !driver.power_cycle(),
          "cycle after choosing Original");
    driver.run({});
    check(driver.effective_interface()==InterfaceMode::original && !driver.clean_frame(),
          "power cycle removed the old enhanced presentation");
    check(!driver.choose_options({},false,WidescreenMode::off,InterfaceMode::enhanced) && !driver.power_cycle(),
          "cycle after choosing Enhanced");
    driver.run({});
    check(driver.effective_interface()==InterfaceMode::enhanced && driver.clean_frame(),
          "power cycle seeded the new enhanced presentation");
    FrameDriver recording(rom.image,SaveStore(folder/"recording",rom.version),{},true);
    recording.set_clean_background(true); recording.run({});
    check(!recording.clean_frame(),"recording enabled the optional picture");
}
}
int main(int argc,char** argv) try {
    check(argc==4,"Expected <ROM> <us|br> <test folder>");
    auto rom=probe::load(argv[1],argv[2]);
    if(!rom)return 77;
    auto g=std::make_unique<Rig>(*rom);
    nested_and_changing(*g);
    member_cursor_scroll_opening(*g);
    driver_pairing(*rom,argv[3]);
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
