// SPDX-License-Identifier: GPL-3.0-or-later
// Drive the actual cartridge procedures and native cursor/printer through the
// production enhanced driver. The observer only recognizes instruction boundaries.
#include "app/frame_driver.h"
#include "app/host.h"
#include "app/retro_cartridge.h"
#include "app/retro_field_motion.h"
#include "app/retro_font.h"
#include "game/retro_events.h"
#include "replay/recording.h"
#include <algorithm>
#include <fstream>
#include "temporary_cleanup.h"
#include <bitset>
#include <chrono>
#include <functional>
#include <string_view>
#include <iostream>
#include <stdexcept>

using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool ok,const std::string& why) { if(!ok)throw std::runtime_error(why); }
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("psmodern-retro-play-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Temporary() { test::remove_temporary_folder(path); }
};
std::uint16_t word(const Machine& m,unsigned address) { return m.peek(address)|(m.peek(address+1)<<8); }
void put(Machine& m,unsigned address,unsigned value) { m.memory().write(address,value); }
void put_word(Machine& m,unsigned address,unsigned value) { put(m,address,value);put(m,address+1,value>>8); }

std::filesystem::path captures;
class Scene {
public:
    Scene(const LoadedRom& rom,const Machine& base,const std::filesystem::path& path,unsigned entry,unsigned speed,std::string_view presses)
        :driver(rom.rom,settings(path,rom.version)),m(driver.machine()),burst(speed),script(presses) {
        check(driver.effective_interface()==InterfaceMode::enhanced,"enhanced profile was not published at startup");
        m=base;
        driver.set_clean_background(false);driver.set_clean_background(true);
        if(entry) {
            m.cpu().pc.uint16_value=entry;
            m.cpu().sp.uint16_value=stack;
            m.cpu().iff1=m.cpu().iff2=1;m.cpu().im=1;m.cpu().halt_line=0;
            put(m,0xfffc,0);put(m,0xfffd,0);put(m,0xfffe,1);put(m,0xffff,2);
            put(m,0xc202,9);put(m,0xc204,0);put(m,0xc205,0);put(m,0xc268,0);
            put(m,0xc2d3,0);put_word(m,stack,returned_to);
            put(m,0xc4f0,1);put_word(m,0xc4e0,1000);
            for(unsigned n=0;n<2;++n) {
                const unsigned addr=0xc400+n*16;
                for(unsigned i=0;i<16;++i)put(m,addr+i,0);
                put(m,addr,1);put(m,addr+1,10);put(m,addr+2,20);
                put(m,addr+5,3);put(m,addr+6,30);put(m,addr+7,40);
            }
        }
        m.set_boundary_observer(&observer);
        driver.speed(speed);
    }
    ~Scene() {m.set_boundary_observer(nullptr);}
    void frame(Pad pad={}) {
        FrameInput input;input.pads[0]=pad;
        for(unsigned i=0;i<burst;++i) {
            const auto result=driver.run(input,i+1==burst);
            check(!result.save_error && !result.audio_error,"failed to complete a gameplay frame");
            check(++frames<1500,"scene exceeded its frame budget; last wait "+std::to_string(wait)+" edges "+edges);
        }
    }
    // Q confirms a cursor, P advances a page/button wait, H holds a page
    // edge through the next question. Each consumes one planned physical press.
    void edge(char kind) {
        check(edges.size()<script.size() && script[edges.size()]==kind,"unexpected confirm after "+edges);
        edges+=kind;
    }
    void complete() const { check(edges==script,"expected "+std::string(script)+", supplied "+edges); }
    void press(Pad pad,char kind=0) {
        if(kind)edge(kind);
        frame(pad);
        if(!returned)frame();
    }
    // Only press after the same native wait survives a neutral presentation
    // batch; a transient poll while the picture is being published needs none.
    void advance() {
        if(wait==0x2d2a && edges.size()<script.size() && script[edges.size()]=='P') {
            frame();
            if(wait==0x2d2a && !returned)press({.two=true},'P');
        } else frame();
    }
    void await_choice() {
        while(wait!=0x2d65 && !returned)advance();
        check(!returned,"the routine returned before choosing");
    }
    void hold_page_into_question() {
        while(wait!=0x2d2a)frame();
        frame();edge('H');
        while(wait!=0x2d65)frame({.two=true});
        for(unsigned i=0;i<12;++i)frame({.two=true});
        check(wait==0x2d65 && m.peek(0xc268) && !reached(0x2a04),"holding the page advance answered the hospital price");
        frame(); // a fresh edge may now confirm the price
    }
    void choose(unsigned row,std::function<void()> inspect={},unsigned poll=0x2d65) {
        while(wait!=poll && !returned)advance();
        check(!returned,"the routine returned before the expected choice");
        frame();frame(); // allow this cursor to appear before the player's edge
        const auto before=consumed;
        check(m.peek(0xc26b)==0 && m.peek(0xc26e)>=row,"native selector opened on unexpected rows");
        for(unsigned i=0;i<row;++i)press({.down=true});
        if(inspect) { frame();frame();inspect(); } // state and scanout of the selected row have both been published
        check(m.peek(0xc26b)==row,"fresh Down did not select the intended row");
        press({.two=true},'Q');
        for(unsigned i=0;i<8 && !returned && consumed==before;++i)frame();
        check(consumed!=before,"native selector did not consume its fresh confirm; prior edges "+edges);
        ++choices;
    }
    void finish() { while(!returned)advance(); }
    void revisit(unsigned entry) {
        check(returned,"cannot revisit before the previous procedure returns");
        returned=false;visits.reset();wait=0;frames=0;
        m.cpu().pc.uint16_value=entry;m.cpu().sp.uint16_value=stack;
        m.cpu().iff1=m.cpu().iff2=1;put_word(m,stack,returned_to);
        // The forced entry shares the cycle just sampled at the previous
        // stop. The boundary observer deduplicates that cycle.
        if (trace_menu_rows) menu_motion.observe(m,entry);
    }
    void until(unsigned addr) {
        while(!reached(addr) && !returned)advance();
        check(reached(addr),"the intended continuation was skipped");
    }
    bool reached(unsigned pc) const { return visits.test(pc); }
    using Picture=std::vector<std::uint32_t>;
    Picture picture(const char* name) {
        Picture p(256*192);
        check(driver.render_retro(p),"enhanced frame was not delivered");
        if(!captures.empty() && burst==1) {
            std::filesystem::create_directories(captures);
            auto save=[&](std::string suffix,const Picture& image) {
                std::ofstream f(captures/(std::string(name)+suffix+".ppm"),std::ios::binary);
                f<<"P6\n256 192\n255\n";
                for(auto color:image)for(int shift:{16,8,0})f.put(static_cast<char>(color>>shift));
                check(bool(f),"cannot write rendered frame");
            };
            save("",p);
            if(std::string_view(name)=="shop") {
                Picture base(p.size());const auto& frame=*driver.clean_frame();
                centre_pixels(base,frame.native,nullptr);save("-native",base);
                centre_pixels(base,frame.background,nullptr);save("-background",base);
            }
        }
        return p;
    }
    void label(const Picture& p,int x,int y,const std::string& expected,std::uint32_t color,
               retro::Align align=retro::Align::left) const {
        Picture mask(p.size());
        retro::text(mask,x,y,expected,color,align);
        unsigned ink=0;
        for(std::size_t i=0;i<mask.size();++i)if(mask[i]) {
            ++ink;check(p[i]==color,"missing enhanced glyph pixels for "+expected+" at "+std::to_string(x)+","+std::to_string(y));
        }
        check(ink>0,"expected visible cartridge glyphs");
    }
    void track_menu_rows() { trace_menu_rows=true; }
    void expect_menu_rows(retro::Window window,unsigned total,bool closed) const {
        const auto& evidence=menu_rows[static_cast<unsigned>(window)];
        const unsigned expected=(1u<<(total+1))-2;
        check(evidence.drawn==expected,"native window "+std::to_string(unsigned(window))+
              " did not write every opening row (saw bits "+std::to_string(evidence.drawn)+")");
        if (closed) check(evidence.closed && evidence.restored==(expected ^ (1u<<total)),
                          "native window "+std::to_string(unsigned(window))+
                          " did not restore every displayed row (saw bits "+std::to_string(evidence.restored)+")");
    }
    void expect_inventory_redraw() const {
        check(inventory_names_seen==0x1ff && inventory_header_written && inventory_previous_first==0 &&
              inventory_previous_item==1 && !menu_motion.inventory_redraw(),
              "inventory page must retain the old rows until each new name is written");
    }
    FrameDriver driver;
    Machine& m;
    std::string edges;
    unsigned choices=0;
    bool returned=false;
private:
    static constexpr unsigned stack=0xdf00,returned_to=0x2d25;
    static SaveStore settings(const std::filesystem::path& path,RomVersion version) {
        SaveStore store(path,version);
        check(!store.choose({},false,WidescreenMode::off,InterfaceMode::enhanced,{}),"cannot seed interface preference");
        return store;
    }
    unsigned burst,frames=0,wait=0,consumed=0;
    const std::string_view script;
    std::bitset<65536> visits;
    struct RowEvidence { unsigned drawn=0, restored=0; bool closed=false; };
    bool trace_menu_rows=false, inventory_header_written=false;
    unsigned inventory_names_seen=0,inventory_previous_first=0,inventory_previous_item=0;
    retro::MenuMotion menu_motion;
    std::array<RowEvidence,static_cast<unsigned>(retro::Window::count)> menu_rows{};
    void trace_menu(const Machine::Boundary& b) {
        if (!trace_menu_rows || b.in_body || !game::retro_window_entry(b.pc) ||
            m.memory().mapping(b.pc)!=Memory::Mapping{Memory::Kind::rom,0}) return;
        menu_motion.observe(m,b.pc);
        if (const auto* redraw=menu_motion.inventory_redraw()) {
            inventory_names_seen|=1u<<redraw->names_written;
            inventory_header_written|=redraw->header_written;
            inventory_previous_first=redraw->previous_first;
            inventory_previous_item=redraw->previous_items[0];
        }
        for (unsigned i=0;i<menu_rows.size();++i) {
            auto& evidence=menu_rows[i];
            if (const auto* rows=menu_motion.rows(static_cast<retro::Window>(i))) {
                if (rows->drawn) evidence.drawn|=1u<<rows->drawn;
                if (rows->restored) evidence.restored|=1u<<rows->restored;
            } else if (evidence.restored) evidence.closed=true;
        }
    }
    Machine::BoundaryObserver observer=[this](const Machine::Boundary& b) {
        if(b.depth)return;
        trace_menu(b);
        const unsigned sp=m.cpu().sp.uint16_value;
        if(b.pc==returned_to && sp==stack+2)returned=true;
        if(b.pc==0x0052)wait=word(m,sp);
        if(b.pc==0x005b)wait=0;
        if(b.pc==0x2da4 || b.pc==0x2e12 || b.pc==0x2e1b || b.pc==0x2e20)++consumed;
        visits.set(b.pc);
    };
};

void seed_party(Machine& m) {
    put(m,0xc4f0,3);
    for(unsigned i=0;i<4;++i) {
        const auto member=0xc400+16*i;
        put(m,member,1);
        put(m,member+1,21+i);
        put(m,member+2,11+i);
        put(m,member+6,30);
    }
}

void party_motion(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed) {
    Scene s(rom,base,root,0x2ed9,speed,"");
    put(s.m,0xc309,1); // use a map outside the approved Parolit prototype
    seed_party(s.m);
    const auto border=[](const Scene::Picture& p,unsigned member,unsigned y) {
        const auto x=4+62*member+14;
        return std::all_of(p.begin()+y*256+x,p.begin()+y*256+x+24,[](auto pixel) {
            return pixel==retro::white;
        });
    };
    bool opening=false;
    while(!s.returned) {
        s.frame();
        if(!s.driver.clean_frame())continue;
        const auto p=s.picture("party-opening");
        opening|=border(p,0,150) && !border(p,0,187) && !border(p,1,150);
    }
    check(opening,"the first card must open on its own native rows");
    s.frame();
    const auto full=s.picture("party");
    for(unsigned i=0;i<4;++i) {
        check(border(full,i,150) && border(full,i,187),"a native party card was not complete");
        s.label(full,58+62*i,167,std::to_string(21+i),retro::white,retro::Align::right);
        s.label(full,58+62*i,177,std::to_string(11+i),retro::white,retro::Align::right);
    }

    s.revisit(0x2f3c);
    put(s.m,0xc411,29);
    bool old_hp=false;
    while(!s.returned) {
        s.frame();
        const auto p=s.picture("party-redraw");
        check(border(p,3,150) && border(p,3,187),"redrawing the second card cleared the other members");
        const auto& frame=*s.driver.clean_frame();
        const retro::Cartridge cart(*rom.rom,frame.state.ram);
        const retro::FieldMotion rows(cart,frame.window_table);
        old_hp|=rows.party_hp[1]==22 && s.m.peek(0xc411)==29;
        s.label(p,120,167,std::to_string(rows.party_hp[1]),retro::white,retro::Align::right);
    }
    if(speed==1)check(old_hp,"a party redraw updated the HP before its native printer");
    s.frame();
    s.label(s.picture("party-redrawn"),120,167,"29",retro::white,retro::Align::right);

    s.revisit(0x30b7);
    bool restoring=false;
    while(!s.returned) {
        s.frame();
        const auto p=s.picture("party-restoring");
        restoring|=!border(p,0,150) && border(p,0,187) && !border(p,3,150) && border(p,3,187);
    }
    check(restoring,"restoring the native strip must uncover the top of each card first");
    s.frame();
    check(!border(s.picture("party-closed"),0,187),"restoring the final row left a party card behind");

    s.revisit(0x2f93);
    s.m.cpu().af.uint8_values.at_1=2;
    s.finish();s.frame();
    const auto single=s.picture("party-single");
    check(border(single,2,150) && border(single,2,187) &&
          !border(single,0,150) && !border(single,1,150) && !border(single,3,150),
          "refreshing one native member created other cards");
    s.label(single,182,167,"23",retro::white,retro::Align::right);
    s.revisit(0x30b7);
    s.finish();s.frame();
    check(!border(s.picture("party-single-closed"),2,187),"restoring a single native card left its footer behind");
    s.complete();
}

void battle_windows(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed) {
    Scene s(rom,base,root,0x30ed,speed,"");
    put(s.m,0xc29d,255);
    put(s.m,0xc2e6,0);
    put(s.m,0xc2c7,4);
    for(unsigned i=0;i<4;++i)put(s.m,0xc441+16*i,31+i);
    const auto border=[](const Scene::Picture& p,unsigned x,unsigned y) {
        return std::all_of(p.begin()+y*256+x,p.begin()+y*256+x+20,[](auto pixel) {
            return pixel==retro::white;
        });
    };
    const auto exposed=[&](const Scene::Picture& p,unsigned x,unsigned top,unsigned bottom) {
        const auto& background=s.driver.clean_frame()->background;
        for(unsigned y=top;y<bottom;++y)
            for(unsigned i=y*256+x;i<y*256+x+20;++i)
                if(p[i]!=rgb(background[i]))return false;
        return true;
    };
    const retro::Cartridge data(*rom.rom,s.m.memory().ram());
    bool partial_stats=false;
    while(!s.returned) {
        s.frame();
        if(!s.driver.clean_frame())continue;
        const auto p=s.picture("battle-stats-opening");
        partial_stats|=border(p,109,4) && border(p,211,4) && exposed(p,211,47,50);
    }
    check(partial_stats,"enemy HP appeared as a completed column");
    s.frame();
    const auto enemies=s.picture("battle-enemies");
    s.label(enemies,107,11,data.enemy_name(),retro::white);
    for(unsigned i=0;i<4;++i)
        s.label(enemies,246,10+9*i,std::to_string(31+i),retro::white,retro::Align::right);

    s.revisit(0x30c3);
    bool partial_commands=false;
    while(!s.returned) {
        s.frame();
        const auto p=s.picture("battle-commands-opening");
        partial_commands|=border(p,20,4) && exposed(p,20,73,76);
    }
    check(partial_commands,"battle commands appeared as a completed window");
    s.frame();
    check(border(s.picture("battle-commands"),20,75),"final battle command row is absent");

    s.revisit(0x2eac);
    put(s.m,0xc267,1);
    s.finish();s.frame();
    s.label(s.picture("battle-actor"),11,10,data.member_name(1),retro::gold);
    s.revisit(0x2ecd);
    s.finish();s.frame();
    const auto no_actor=s.picture("battle-actor-closed");
    check(no_actor[12*256+16]==retro::black && border(no_actor,20,75),
          "restoring the native actor erased battle commands or left a name");

    s.revisit(0x30e1);
    bool restoring_commands=false;
    while(!s.returned) {
        s.frame();
        const auto p=s.picture("battle-commands-restoring");
        restoring_commands|=exposed(p,20,4,7) && border(p,20,75) && border(p,109,4);
    }
    check(restoring_commands,"closing battle commands must leave its trailing rows and neighbors intact");
    s.revisit(0x319e);
    bool restoring_stats=false;
    while(!s.returned) {
        s.frame();
        const auto p=s.picture("battle-stats-restoring");
        restoring_stats|=exposed(p,211,4,7) && border(p,211,49) && border(p,109,4);
    }
    check(restoring_stats,"the enemy HP column must restore independently of the name box");
    s.frame();
    const auto closed=s.picture("battle-enemies-closed");
    check(exposed(closed,109,4,26) && exposed(closed,211,4,50),"restoring the enemy windows left enhanced rows");
    s.complete();
}

void field_nesting(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,bool parolit) {
    Scene s(rom,base,root,0x1be1,1,"Q");
    put(s.m,0xc309,parolit ? 6 : 1);
    put(s.m,0xc4f0,3);
    const retro::Cartridge data(*rom.rom,s.m.memory().ram());
    const auto parent_cursor=data.word(0x36e1)+0x40;
    const auto member_cursor=data.word(0x367a);
    const auto last_command=data.tile_row(data.word(0x36f0)+110,4);
    const auto border=[](const Scene::Picture& picture,int y) {
        return std::all_of(picture.begin()+y*256+22,picture.begin()+y*256+53,
                           [](auto pixel) { return pixel==retro::white; });
    };
    const auto ready=[&](unsigned cursor) { return s.m.peek(0xc268) && word(s.m,0xc269)==cursor; };
    const auto full_parent=[&](const Scene::Picture& picture) {
        check(border(picture,4) && border(picture,64),"nested selector cut off the command panel");
        s.label(picture,17,51,last_command,retro::white);
    };

    s.until(0x36dd);
    s.frame(); // The reseeded scanout needs one complete picture before inspection.
    bool opening=false;
    while(!ready(parent_cursor)) {
        const auto picture=s.picture("field-opening");
        opening|=border(picture,4) && !border(picture,64);
        s.frame();
    }
    check(opening,"field window did not open progressively");
    s.choose(0);
    while(!ready(member_cursor)) {
        full_parent(s.picture("field-while-member-opens"));
        s.frame();
    }
    s.frame();s.frame();
    const auto nested=s.picture("field-with-member");
    full_parent(nested);
    check(nested[4*256+200]==retro::white,"the member picker was not shown beside the parent");
    s.press({.one=true});
    while(!ready(parent_cursor)) {
        full_parent(s.picture("field-while-member-closes"));
        s.frame();
    }
    s.frame();s.frame();
    full_parent(s.picture("field-after-member"));

    s.press({.one=true});
    bool restoring=false;
    while(!s.returned) {
        const auto picture=s.picture("field-closing");
        restoring|=!border(picture,4) && border(picture,64);
        s.frame();
    }
    check(restoring,"field window did not restore progressively");
    s.complete();
}

void status_windows(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root) {
    Scene s(rom,base,root,0x1ca1,1,"QP");
    put(s.m,0xc40e,0);
    s.track_menu_rows();
    s.choose(0);
    s.finish();
    s.expect_menu_rows(retro::Window::member,5,true);
    s.expect_menu_rows(retro::Window::equipment,8,true);
    s.expect_menu_rows(retro::Window::status,14,true);
    s.complete();
}

void inventory_pages(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root) {
    Scene s(rom,base,root,0x2168,1,"");
    put(s.m,0xc4e2,18);
    for (unsigned i=0;i<18;++i) put(s.m,0xc4c0+i,i<8 ? 1 : 2);
    s.track_menu_rows();
    while (!s.m.peek(0xc268)) s.frame();
    s.frame();
    s.press({.right=true});
    while (!s.m.peek(0xc268) || s.m.peek(0xc299)!=8) s.frame();
    s.expect_inventory_redraw();
    s.frame();
    s.press({.one=true});
    s.finish();
    s.expect_menu_rows(retro::Window::items,21,true);
    s.complete();
}

void menu_choices(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root) {
    const struct {
        retro::Window window;
        unsigned entry, close, rows;
        const char* name;
    } choices[]{
        {retro::Window::buy_sell,0x3777,0x3797,5,"buy-sell"},
        {retro::Window::saves,0x39a5,0x39dd,12,"save-slots"},
        {retro::Window::choice,0x3a21,0,7,"three-choice"}
    };
    for (const auto& choice:choices) {
        Scene s(rom,base,root/choice.name,choice.entry,1,"Q");
        s.track_menu_rows();
        s.choose(0);
        s.finish();
        if (choice.close) {
            s.revisit(choice.close);
            s.finish();
        }
        s.expect_menu_rows(choice.window,choice.rows,true);
        s.complete();
    }
}

void chest(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed,bool yes) {
    Scene s(rom,base,root,0x28db,speed,yes ? "QP" : "Q");
    // The genuine chest handler initializes its own object slot and plain
    // reveal animation before the question. A town object cannot complete it.
    put(s.m,0xc800,0x0d);put(s.m,0xc2e0,0);
    s.m.cpu().pc.uint16_value=0x5c63;s.m.cpu().ix_iy[1].uint16_value=0xc800;
    const auto sp=static_cast<std::uint16_t>(s.m.cpu().sp.uint16_value-2);
    s.m.cpu().sp.uint16_value=sp;put_word(s.m,sp,0x28db);
    put_word(s.m,0xc2e1,0xc550);put(s.m,0xc550,0);
    put_word(s.m,0xc2dd,50);put(s.m,0xc2df,0);
    s.choose(yes ? 0 : 1);
    if(yes) {
        s.until(0x28fe);
        s.finish();
        check(s.m.peek(0xc550)==255 && word(s.m,0xc4e0)==1050,"the opened chest must grant its reward exactly once");
    } else {
        s.finish();
        check(!s.m.peek(0xc550) && word(s.m,0xc4e0)==1000,"declining the chest changed loot");
    }
    check(s.choices==1 && s.reached(0x28e4) && s.reached(0x37a3),"chest lost its own prompt or answer");
    s.complete();
}
void hospital(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed) {
    Scene s(rom,base,root,0x298d,speed,"QHQP");
    put(s.m,0xc4f0,0);
    s.choose(0);s.hold_page_into_question();s.choose(0);s.finish();
    check(s.m.peek(0xc401)==30 && s.m.peek(0xc402)==40 && word(s.m,0xc4e0)==960,"hospital must refill both points and debit their missing sum once");
    s.complete();
}
void church(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed) {
    Scene s(rom,base,root,0x2a85,speed,"PQQQ");
    put(s.m,0xc410,0);put(s.m,0xc411,0);put(s.m,0xc412,0);put(s.m,0xc2db,7);
    s.choose(0);s.choose(1);s.choose(0);s.until(0x2b07);
    check(s.m.peek(0xc410)==1 && s.m.peek(0xc411)==30 && s.m.peek(0xc412)==40 && word(s.m,0xc4e0)==940 &&
          s.m.peek(0xc317)==7,"church must revive the selected second member for 20 times the level");
    s.complete();
}
void shops(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed,bool road) {
    Scene s(rom,base,root,0x2bc0,speed,road ? (rom.version==RomVersion::brazilian ? "PQQPPPQQPPPPQQ" : "PQQPPPQQPPPQQ") : "PQQQ");
    retro::Cartridge data(*rom.rom,s.m.memory().ram());
    unsigned row=0;
    bool found=false;
    for(unsigned shop=0;shop<32 && !found;++shop) {
        put(s.m,0xc2db,shop);
        for(row=0;row<data.shop_count();++row) {
            const auto item=data.shop_item(row);
            if(item.id && (item.id>=64)==road && item.price && item.price<65535) {found=true;break;}
        }
    }
    check(found,"no cartridge row for purchase fixture");
    const auto offer=data.shop_item(row);
    put_word(s.m,0xc4e0,65535);put(s.m,0xc4e2,0);put(s.m,0xc2ec,0);
    if(road)for(unsigned visit=1;visit<=2;++visit) {
        s.choose(0);s.choose(row);s.finish();
        check(s.m.peek(0xc2ec)==visit && !s.m.peek(0xc4e2) && word(s.m,0xc4e0)==65535,
              "the first two Road Pass attempts must remember the visit without charging or granting an item");
        s.revisit(0x2bc0);
    }
    s.choose(0);s.choose(row,[&]{
        const auto p=s.picture(road ? "road-pass" : "shop");
        s.label(p,159,11+10*row,std::to_string(offer.price),retro::gold,retro::Align::right);
        s.label(p,246,21,"0/24",retro::white,retro::Align::right);
        // The canonical cursor, glyphs and borders outside the enhanced panels
        // must reveal the clean scene, including the stretch between the shop
        // and item-details panels. Count differences from the actual cartridge frame.
        unsigned erased=0;
        const auto& frame=*s.driver.clean_frame();
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x) {
            if((x>=4 && x<166 && y>=4 && y<68) || (x>=170 && x<252 && y>=4 && y<34) ||
               (x>=4 && x<252 && y>=136 && y<188))continue;
            const auto i=256*y+x;
            check(p[i]==rgb(frame.background[i]),"native window pixels escaped between enhanced panels");
            erased+=frame.native[i]!=frame.background[i];
        }
        check(erased!=0,"shop fixture contained no native pixels outside the new panels");
    });
    s.until(road ? 0x2c80 : 0x2c22);
    check(word(s.m,0xc4e0)==65535-offer.price && s.m.peek(0xc4e2)==1 &&
          s.m.peek(0xc4c0)==(road ? s.m.peek(0x2c6d) : offer.id),"direct purchase must add the correct item and charge before the next question");
    if(road)check(!s.m.peek(0xc2ec),"third Road Pass visit did not reset its counter");
    else s.choose(1,[&]{
        const auto p=s.picture("after-purchase");
        s.label(p,246,21,"1/24",retro::cyan,retro::Align::right);
        s.label(p,246,10,std::to_string(65535-offer.price),retro::cyan,retro::Align::right);
    });
    s.complete();
}
void equip(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed,bool allowed) {
    Scene s(rom,base,root,0x2168,speed,"QQQP");
    s.track_menu_rows();
    retro::Cartridge data(*rom.rom,s.m.memory().ram());
    unsigned item=2;
    while(item<64 && !(data.compare(1,item).equipment && data.compare(1,item).eligible==allowed))++item;
    check(item<64,"no cartridge equipment matching the recipient");
    const auto slot=0xc41a+data.compare(1,item).slot;
    put(s.m,slot,1);put(s.m,0xc2c4,item);put_word(s.m,0xc29b,0xc4c0);put(s.m,0xc4c0,item);put(s.m,0xc4e2,18);
    for(unsigned i=1;i<18;++i)put(s.m,0xc4c0+i,1);
    s.choose(1,[&]{ // on a paged list, row 0 changes pages; Down selects the first item
        const auto p=s.picture("inventory");
        s.label(p,153,10,"18/24 \xc2\xb7 1/3",retro::grey,retro::Align::right);
        s.label(p,17,23,data.item_name(item),retro::gold);
    },0x2daf);
    s.choose(1); // EQUIP action
    s.choose(1,[&]{
        const auto p=s.picture(allowed ? "equip" : "equip-refused");
        s.label(p,177,94,data.member_name(1),retro::gold);
        s.label(p,12,143,data.item_name(item),retro::gold);
    });
    s.until(0x2741);
    check(s.m.peek(slot)==(allowed ? item : 1) && s.m.peek(0xc4c0)==(allowed ? 1 : item) && s.m.peek(0xc4e2)==18,"equip must respect eligibility and swap the original bag slot");
    check(s.reached(allowed ? 0x272a : 0x2747),"expected equip result or refusal was never displayed");
    s.finish();
    s.expect_menu_rows(retro::Window::items,21,true);
    s.expect_menu_rows(retro::Window::actions,7,true);
    s.expect_menu_rows(retro::Window::member,5,true);
    s.complete();
}
void magic(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed,bool battle) {
    Scene s(rom,base,root,battle ? 0 : 0x1d4d,speed,"QQQ");
    s.track_menu_rows();
    if(battle) {
        // Continue from before the first native battle windows. This gives the
        // clean scanout their real paint and keeps party/enemy ownership live.
        seed_party(s.m);
        put(s.m,0xc411,8);put(s.m,0xc421,12);put(s.m,0xc431,15);put(s.m,0xc402,20);
        s.await_choice();
        check(s.m.peek(0xc29d) && s.m.peek(0xc26e)==4,"a real five-command battle cursor is required");
    }
    put(s.m,0xc267,0);
    put(s.m,0xc40e,5);put(s.m,0xc40f,2);
    retro::Cartridge data(*rom.rom,s.m.memory().ram());
    const auto spell=data.spell_id(battle ? 0 : 3,0);
    if(!battle)s.choose(0); // field caster
    else {
        const auto commands=word(s.m,0x10af);
        unsigned row=0;while(row<5 && word(s.m,commands+2*row)!=0x19de)++row;
        check(row<5,"the command table has no magic row");
        s.choose(row);
    }
    s.choose(0,[&]{
        const auto p=s.picture(battle ? "battle-magic" : "field-magic");
        const auto group=battle ? 0u : 3u;
        const std::string mp=rom.version==RomVersion::us_rev_a ? "MP" : "PM";
        s.label(p,battle ? 15 : 17,23,data.spell_name(group,0),retro::gold);
        s.label(p,battle ? 90 : 129,23,std::to_string(data.spell_cost(spell))+(battle ? "" : " "+mp),retro::gold,retro::Align::right);
        if(battle) {
            s.label(p,15,63,data.spell_name(group,4),retro::white);
            s.label(p,196,156,data.member_name(3),retro::white);
        }
    });
    s.choose(1,[&]{
        const auto p=s.picture(battle ? "battle-target" : "field-target");
        s.label(p,battle ? 16 : 172,battle ? 33 : 69,data.member_name(1),retro::gold);
    });
    if(battle)s.until(0x1ac5);
    else s.finish();
    if(battle)
        check(s.m.peek(0xc2ae)==1 && s.m.peek(0xc2ad)==spell && s.m.peek(0xc402)==20,"battle choice queues the selected spell and ally before charging MP");
    else
        check(s.m.peek(0xc411)>10 && s.m.peek(0xc402)==20-data.spell_cost(spell),"field magic must charge the real MP cost and heal the chosen ally");
    s.expect_menu_rows(retro::Window::magic,12,!battle);
    s.expect_menu_rows(battle ? retro::Window::battle_member : retro::Window::member,battle ? 9 : 5,!battle);
    s.complete();
}
void story(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root,unsigned speed) {
    Scene s(rom,base,root,0x474b,speed,"P");
    const auto table=word(s.m,0x4753);
    unsigned event=1;
    while(event<256 && word(s.m,table+2*event)!=0x4dfb)++event;
    check(event<256,"story handler missing from cartridge table");
    put(s.m,0xc2db,event);put(s.m,0xc504,0);
    s.finish();
    check(s.reached(0x4765) && !s.reached(0x4758),"story wait must discard the handler return and resume its actual caller on one press");
    s.complete();
}

void title_continue(const LoadedRom& rom,const Machine& base,const std::filesystem::path& root) {
    Memory::CartridgeRam sram{};
    // Seed the same header and empty picker the cartridge installs on
    // initialization, then put two named saves into that native layout.
    std::copy_n(rom.rom->begin()+0x0807,64,sram.begin());
    std::copy_n(rom.rom->begin()+0x3ac4,12*18,sram.begin()+0x100);
    constexpr std::array<std::string_view,2> names{"ALIS", "NOAH"};
    constexpr unsigned purse=1234;
    const retro::Cartridge cart(*rom.rom,base.memory().ram());
    for (unsigned i=0;i<names.size();++i) {
        sram[0x201+i]=1;
        const auto slot=0x400*(i+1);
        std::copy_n(base.memory().ram().begin()+0x300,0x400,sram.begin()+slot);
        sram[slot+0x1e0]=static_cast<std::uint8_t>(purse+i);
        sram[slot+0x1e1]=static_cast<std::uint8_t>((purse+i)>>8);
        for (unsigned j=0;j<5;++j) {
            const unsigned code=j<names[i].size() ? names[i][j] : ' ';
            const auto glyph=0x7d17+2*(code-32);
            const auto cell=0x118+36*i+2*j;
            sram[cell]=cart.byte(glyph);
            sram[cell+18]=cart.byte(glyph+1);
        }
    }
    atomic_save(root/save_name(rom.version),sram);
    SaveStore store(root,rom.version);
    check(!store.choose({},false,WidescreenMode::off,InterfaceMode::enhanced,sram),"could not seed Enhanced profile");
    FrameDriver driver(rom.rom,std::move(store));
    auto& m=driver.machine();
    std::vector<std::uint32_t> pixels(retro::width*retro::height),mask(pixels.size());
    const auto ink=[&](unsigned x,unsigned y,std::string_view label,std::uint32_t color) {
        std::fill(mask.begin(),mask.end(),0);
        retro::text(mask,x,y,label,color);
        for (unsigned i=0;i<mask.size();++i)
            if (mask[i] && mask[i]!=pixels[i]) return false;
        return true;
    };
    const auto border=[&](unsigned y) {
        return std::all_of(pixels.begin()+y*retro::width+86,pixels.begin()+y*retro::width+170,
                           [](auto pixel) { return pixel==retro::white; });
    };
    unsigned frame=0;
    const auto step=[&](Pad pad={}) {
        FrameInput input;
        input.pads[0]=pad;
        check(++frame<700,"title Continue exceeded its frame budget in mode "+
              std::to_string(m.peek(0xc202))+" at cursor "+std::to_string(word(m,0xc269)));
        const auto result=driver.run(input);
        check(!result.save_error && !result.audio_error,"title Continue failed");
        return driver.render_retro(pixels);
    };
    const auto waiting=[&](unsigned operand) { return m.peek(0xc268) && word(m,0xc269)==word(m,operand); };
    const auto press=[&](Pad pad) { step();step(pad);step(pad);step(); };
    for (unsigned n=0;n<110;++n) step({.down=n==100 || n==101,.one=n==104 || n==105});
    while (!waiting(0x37b6)) step();
    press({.two=true});

    bool grew=false;
    while (!waiting(0x39c8)) {
        if (step() && border(4) && !border(69)) grew=true;
    }
    check(grew,"the title list must reveal its native rows before taking input");
    press({.down=true});
    bool showed_second_cursor=false;
    for (unsigned i=0;i<32 && !showed_second_cursor;++i) {
        step();
        showed_second_cursor=border(4) && border(69) && ink(93,11,"1  ALIS",retro::white) &&
            ink(93,22,"2  NOAH",retro::gold) && ink(86,22,"▸",retro::gold);
    }
    check(showed_second_cursor,"the title list must show both names with the cursor on slot two");
    press({.two=true});
    for (unsigned i=0;i<110 && word(m,0xc4e0)!=purse+1;++i) step();
    if (word(m,0xc4e0)!=purse+1) press({.two=true});
    while (word(m,0xc4e0)!=purse+1) step();
    check(m.peek(0xc2c5)==2,"title Continue did not load the selected second slot");
    step();step();
    check(!border(4) || !ink(93,22,"2  NOAH",retro::white),
          "the title picker stayed on screen after loading the selected slot");
}
}
int main(int argc,char** argv) { try {
    if(argc<3 || !std::filesystem::is_regular_file(argv[1]))return 77;
    if(argc>3)captures=argv[3];
    Temporary temp;const auto rom=load_rom(argv[1]);
    FrameDriver boot(rom.rom,SaveStore(temp.path/"boot",rom.version));
    const auto recording=replay::read(argv[2]);
    std::unique_ptr<Machine> field,battle_start,candidate,previous;
    const auto* enemy_window=boot.routines().find({0,0x30ed});
    check(enemy_window,"enemy window is missing from the native catalogue");
    for(unsigned frame=0;frame<recording.frames;++frame) {
        const auto calls=enemy_window->counters.cpp_calls;
        if(field) {
            if(!previous)previous=std::make_unique<Machine>(boot.machine());
            else *previous=boot.machine();
        }
        boot.run(recording.input_at(frame));
        if(!field && frame==3400)field=std::make_unique<Machine>(boot.machine());
        if(previous && enemy_window->counters.cpp_calls!=calls)candidate=std::move(previous);
        const auto& m=boot.machine();
        if(candidate && m.peek(0xc29d) && m.peek(0xc268) && m.peek(0xc26e)==4) {
            battle_start=std::move(candidate);break;
        }
    }
    check(field && battle_start,"recorded field and battle not reached");
    unsigned failed=0;
    for(unsigned speed:{1u,4u}) {
        const auto folder=temp.path/std::to_string(speed);
        auto run=[&](const char* label,auto scenario) {
            try { scenario(folder/label); }
            catch(const std::exception& e) { std::cerr<<label<<" at "<<speed<<"x: "<<e.what()<<"\n";++failed; }
        };
        for(bool yes:{false,true})run(yes ? "open" : "leave",[&](const auto& path){chest(rom,*field,path,speed,yes);});
        if(speed==1) {
            run("title-continue",[&](const auto& path){title_continue(rom,*field,path);});
            run("status-windows",[&](const auto& path){status_windows(rom,*field,path);});
            run("inventory-pages",[&](const auto& path){inventory_pages(rom,*field,path);});
            run("menu-choices",[&](const auto& path){menu_choices(rom,*field,path);});
            for(bool parolit:{true,false})
                run(parolit ? "field-nesting" : "field-nesting-away",
                    [&](const auto& path){field_nesting(rom,*field,path,parolit);});
        }
        run("hospital",[&](const auto& path){hospital(rom,*field,path,speed);});
        run("party-motion",[&](const auto& path){party_motion(rom,*field,path,speed);});
        run("battle-windows",[&](const auto& path){battle_windows(rom,*battle_start,path,speed);});
        run("church",[&](const auto& path){church(rom,*field,path,speed);});
        for(bool road:{false,true})run(road ? "pass" : "shop",[&](const auto& path){shops(rom,*field,path,speed,road);});
        for(bool ok:{false,true})run(ok ? "equip" : "refused",[&](const auto& path){equip(rom,*field,path,speed,ok);});
        for(bool battle:{false,true})run(battle ? "battle" : "field",[&](const auto& path){magic(rom,battle ? *battle_start : *field,path,speed,battle);});
        run("story",[&](const auto& path){story(rom,*field,path,speed);});
    }
    if(failed)return 1;
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<"\n";return 1;} }
