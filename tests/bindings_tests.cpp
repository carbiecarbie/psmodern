// SPDX-License-Identifier: GPL-3.0-or-later
// The controls model without SDL or a ROM (milestone 8): defaults,
// names, the swap and refusal rules, controls.txt, and the proof that the
// default bindings give exactly the 0.2.0 console and menu input, and that the
// start screen always does, whatever the bindings.
#include "app/bindings.h"
#include "app/save_store.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool condition,const std::string& message) { if (!condition) throw std::runtime_error(message); }
using P=PadInput;
// SDL scancodes used by the 0.2.0 samplers (USB HID usage numbers).
constexpr Key UP=82,DOWN=81,LEFT=80,RIGHT=79,Z=29,X=27,RETURN=40,BACKSPACE=42,ESCAPE=41,M=16,TAB=43,F5=62,K=14,F1=58;
constexpr Key LALT=226,RALT=230,LGUI=227,RGUI=231;

bool upper_word(std::string_view s,bool space) {
    return !s.empty() && std::all_of(s.begin(),s.end(),[&](char c) { return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||(space&&c==' '); });
}
void names() {
    std::set<std::string_view> tokens,labels,keys,pads,pad_labels;
    for (std::size_t i=0;i<action_count;++i) {
        const auto a=Action(i);
        const auto token=action_token(a),label=action_label(a);
        check(!token.empty() && std::all_of(token.begin(),token.end(),[](char c) { return (c>='a'&&c<='z')||(c>='0'&&c<='9'); }),"action token is a lowercase word");
        check(upper_word(label,true) && label.size()<=11,"action label fits the 11-character column");
        check(tokens.insert(token).second && labels.insert(label).second,"action names are unique");
    }
    check(action_label(Action::power)=="POWER CYCLE" && action_label(Action::button1)=="BUTTON 1" && action_token(Action::button2)=="button2","action names");
    for (const auto& entry:key_table) {
        check(upper_word(entry.token,false) && entry.token.size()<=9,"key token is A-Z 0-9, at most 9 characters");
        check(keys.insert(entry.token).second,"key tokens are unique");
        check(key_bindable(entry.key) && key_token(entry.key)==entry.token,"key token lookup");
        check(key_from_token(entry.token)==entry.key,"key token round trip");
        std::string lower(entry.token);for (auto& c:lower) if (c>='A'&&c<='Z') c=char(c-'A'+'a');
        check(key_from_token(lower)==entry.key,"key tokens are case-insensitive");
    }
    check(key_table_size==97 && key_token(Z)=="Z" && key_token(RETURN)=="ENTER" && key_token(100)=="OEM102" && key_token(135)=="INTL1","key table");
    for (Key k:{LALT,RALT,LGUI,RGUI,Key(0),Key(50),Key(83),Key(511),Key(512),Key(60000)})
        check(!key_bindable(k) && key_token(k).empty(),"Alt, GUI and keys outside the table are not bindable");
    for (auto t:{"LALT","RALT","ALT","LGUI","NONE","","KP10","F13"}) check(!key_from_token(t),"unknown key name");
    for (std::size_t i=0;i<pad_input_count;++i) {
        const auto p=PadInput(i);
        check(upper_word(pad_token(p),false) && pads.insert(pad_token(p)).second,"pad tokens are unique words");
        check(upper_word(pad_label(p),true) && pad_label(p).size()<=7 && pad_labels.insert(pad_label(p)).second,"pad labels fit 7 characters");
        check(pad_from_token(pad_token(p))==p,"pad token round trip");
    }
    check(pad_token(P::right_shoulder)=="RIGHTSHOULDER" && pad_label(P::right_shoulder)=="RB" && pad_token(P::left_trigger)=="LEFTTRIGGER" &&
          pad_label(P::dpad_up)=="D UP" && pad_from_token("dpUp")==P::dpad_up && !pad_from_token("NONE") && !pad_from_token("LEFTX"),"pad names");
    check(upper_word(fixed_fullscreen_label,true) && fixed_fullscreen_label.size()<=9 && upper_word(unbound_label,false),"fixed labels");
}
void defaults() {
    const auto b=Bindings::defaults();
    const std::optional<Key> keys[]={UP,DOWN,LEFT,RIGHT,Z,X,RETURN,BACKSPACE,TAB,ESCAPE,M,F5,std::nullopt};
    const std::optional<PadInput> pads[]={P::dpad_up,P::dpad_down,P::dpad_left,P::dpad_right,P::a,P::b,P::start,P::back,
        P::right_shoulder,P::y,P::x,std::nullopt,std::nullopt};
    for (std::size_t i=0;i<action_count;++i) check(b.keys[i]==keys[i] && b.pads[i]==pads[i],"default binding of "+std::string(action_token(Action(i))));
    for (std::size_t i=0;i<action_count;++i) {
        const auto a=Action(i);
        check(key_required(a)==(a<=Action::button2) && key_fixed(a)==(a==Action::fullscreen),"required and fixed actions");
    }
}
// The invariant every rule keeps: one action per input per column, the six
// required keys present, the fullscreen key empty.
bool valid(const Bindings& b) {
    std::set<Key> keys;std::set<PadInput> pads;
    for (std::size_t i=0;i<action_count;++i) {
        const auto a=Action(i);
        if (b.keys[a] && (!key_bindable(*b.keys[a]) || !keys.insert(*b.keys[a]).second)) return false;
        if (b.pads[a] && (*b.pads[a]>=PadInput::count || !pads.insert(*b.pads[a]).second)) return false;
        if (key_required(a) && !b.keys[a]) return false;
    }
    return !b.keys[Action::fullscreen];
}
void rules() {
    auto b=Bindings::defaults();
    auto r=assign_key(b,Action::button1,X);
    check(r.change==Change::swapped && r.other==Action::button2 && b.keys[Action::button1]==X && b.keys[Action::button2]==Z,"assigning a used key swaps");
    r=assign_key(b,Action::button1,X);check(r.change==Change::unchanged && r.other==Action::count,"own key is unchanged");
    r=assign_key(b,Action::button1,K);check(r.change==Change::set && b.keys[Action::button1]==K,"free key is set");
    r=clear_key(b,Action::power);check(r.change==Change::set && !b.keys[Action::power],"optional key clears");
    r=clear_key(b,Action::power);check(r.change==Change::unchanged,"clearing an empty key is unchanged");
    auto before=b;
    r=assign_key(b,Action::power,UP);
    check(r.change==Change::refused_in_use && r.other==Action::up && b==before,"a swap leaving a direction without a key is refused");
    r=assign_key(b,Action::power,TAB);
    check(r.change==Change::swapped && r.other==Action::speed && b.keys[Action::power]==TAB && !b.keys[Action::speed],"an optional action may lose its key in a swap");
    r=assign_key(b,Action::speed,F5);check(r.change==Change::set,"free key");
    r=assign_key(b,Action::up,F5);
    check(r.change==Change::swapped && r.other==Action::speed && b.keys[Action::speed]==UP && b.keys[Action::up]==F5,"a required action gives its key away in a swap");
    for (std::size_t i=0;i<=std::size_t(Action::button2);++i) {
        before=b;r=clear_key(b,Action(i));check(r.change==Change::refused_required && b==before,"required keys cannot be cleared");
    }
    before=b;
    check(assign_key(b,Action::fullscreen,K).change==Change::fixed && clear_key(b,Action::fullscreen).change==Change::fixed && b==before,"the fullscreen key is fixed");
    check(assign_key(b,Action::speed,LALT).change==Change::unchanged && assign_key(b,Action::speed,600).change==Change::unchanged && b==before,"a non-bindable key changes nothing");
    b=Bindings::defaults();
    r=assign_pad(b,Action::fullscreen,P::y);
    check(r.change==Change::swapped && r.other==Action::options && b.pads[Action::fullscreen]==P::y && !b.pads[Action::options],"pad swap may empty the other action");
    r=assign_pad(b,Action::options,P::left_trigger);check(r.change==Change::set && b.pads[Action::options]==P::left_trigger,"trigger is a pad input");
    r=assign_pad(b,Action::button1,P::b);
    check(r.change==Change::swapped && r.other==Action::button2 && b.pads[Action::button1]==P::b && b.pads[Action::button2]==P::a,"pad swap");
    for (std::size_t i=0;i<action_count;++i) {
        clear_pad(b,Action(i));check(!b.pads[i],"every pad binding clears");
    }
    check(clear_pad(b,Action::up).change==Change::unchanged && valid(b),"clearing an empty pad input");
    r=assign_pad(b,Action::up,P::dpad_up);check(r.change==Change::set,"pad set");

    // Every operation keeps the invariant, and a refusal changes nothing.
    std::mt19937 random(8);b=Bindings::defaults();
    unsigned counts[6]{};
    for (unsigned n=0;n<10000;++n) {
        const auto a=Action(random()%action_count);before=b;
        const auto op=random()%4;
        if (op==0) {
            const Key k=random()%8 ? key_table[random()%key_table_size].key : Key(random()%key_slots);
            r=assign_key(b,a,k);
            if (r.change==Change::set || r.change==Change::swapped) check(b.keys[a]==k,"assigned key held");
            if (r.change==Change::swapped) check(b.keys[r.other]==before.keys[a],"other action receives the previous key");
        } else if (op==1) {
            const auto p=PadInput(random()%pad_input_count);r=assign_pad(b,a,p);
            check(b.pads[a]==p,"assigned pad input held");
            if (r.change==Change::swapped) check(b.pads[r.other]==before.pads[a],"other action receives the previous pad input");
        } else r=op==2 ? clear_key(b,a) : clear_pad(b,a);
        ++counts[int(r.change)];
        if (r.change==Change::refused_in_use || r.change==Change::refused_required || r.change==Change::fixed || r.change==Change::unchanged)
            check(b==before,"a refused or unchanged operation changes nothing");
        check(valid(b),"bindings stay valid after operation "+std::to_string(n));
    }
    for (auto c:counts) check(c>0,"the random operations reached every outcome");
}

Bindings random_valid(std::mt19937& random) {
    Bindings b;
    std::vector<Key> keys;for (const auto& e:key_table) keys.push_back(e.key);
    std::shuffle(keys.begin(),keys.end(),random);
    std::vector<PadInput> pads;for (std::size_t i=0;i<pad_input_count;++i) pads.push_back(PadInput(i));
    std::shuffle(pads.begin(),pads.end(),random);
    for (std::size_t i=0;i<action_count;++i) {
        const auto a=Action(i);
        if (!key_fixed(a) && (key_required(a) || random()%3)) b.keys[a]=keys[i];
        if (random()%3) b.pads[a]=pads[i];
    }
    return b;
}
std::string file(std::initializer_list<std::string_view> lines,std::string_view end="\n") {
    std::string text="psmodern-controls 1";text+=end;
    for (auto line:lines) (text+=line)+=end;
    return text;
}
void file_format() {
    const auto d=Bindings::defaults();
    const auto text=format_bindings(d);
    check(text.starts_with("psmodern-controls 1\n# PSModern controls.") && text.find('\r')==std::string::npos,"header, comments, LF");
    check(text.find("up key UP\nup pad DPUP\n")!=std::string::npos && text.find("power key F5\npower pad NONE\nfullscreen pad NONE\n")!=std::string::npos &&
          text.find("fullscreen key")==std::string::npos && text.ends_with("fullscreen pad NONE\n") && text.size()<=bindings_file_limit,"written lines");
    auto loaded=parse_bindings(text);check(loaded.bindings==d && loaded.ignored==0 && !loaded.warning,"defaults round trip");
    std::mt19937 random(1);
    for (unsigned n=0;n<2000;++n) {
        const auto b=random_valid(random);check(valid(b),"random set is valid");
        loaded=parse_bindings(format_bindings(b));
        check(loaded.bindings==b && loaded.ignored==0 && !loaded.warning,"random valid set round trip "+std::to_string(n));
    }
    // CRLF, comments, blank lines, case and surrounding blanks.
    loaded=parse_bindings(file({"# a comment","","  BUTTON1   KEY   k  ","\tbutton2 Pad lefttrigger\t"," # indented comment"},"\r\n"));
    check(loaded.bindings.keys[Action::button1]==K && loaded.bindings.pads[Action::button2]==P::left_trigger && loaded.ignored==0 && !loaded.warning,"CRLF, case, blanks, comments");
    loaded=parse_bindings("\xEF\xBB\xBF"+file({"map key F1"}));
    check(loaded.bindings.keys[Action::map]==F1 && !loaded.warning,"a UTF-8 mark before the header is accepted");
    // Unknown and malformed lines are counted; the rest still applies.
    loaded=parse_bindings(file({"jump key Z","up keys UP","up key","up key UP extra","speed key LALT","speed pad LEFTX","map key F1","speed key"}));
    check(loaded.ignored==7 && loaded.bindings.keys[Action::map]==F1 && loaded.warning && loaded.warning->find("7 lines")!=std::string::npos,"unusable lines counted");
    loaded=parse_bindings(file({"speed key NONE","options pad none","map key f1","map key F2"}));
    check(!loaded.bindings.keys[Action::speed] && !loaded.bindings.pads[Action::options] && loaded.bindings.keys[Action::map]==Key(59) && loaded.ignored==0,"NONE clears; the later line wins");
    loaded=parse_bindings(file({"button1 key X"}));
    check(loaded.bindings.keys[Action::button1]==X && loaded.bindings.keys[Action::button2]==Z && loaded.ignored==0,"conflicting line resolved by swap");
    loaded=parse_bindings(file({"power key NONE","power key UP","up key NONE","fullscreen key F1","fullscreen key NONE"}));
    check(loaded.bindings.keys[Action::up]==UP && !loaded.bindings.keys[Action::power] && loaded.ignored==4 && loaded.warning->find("4 lines")!=std::string::npos,"refused lines are ignored");
    loaded=parse_bindings(file({"x"}));check(loaded.ignored==1 && loaded.warning->find("1 line ")!=std::string::npos,"one ignored line");
    for (std::string bad:{std::string(),std::string("map key F1\n"),std::string("psmodern-controls 2\nmap key F1\n"),std::string("psmodern-controls 1 \nmap key F1\n"),
                          std::string("psmodern-roms 1\n")}) {
        loaded=parse_bindings(bad);
        check(loaded.bindings==d && loaded.ignored==0 && loaded.warning && loaded.warning->find("unknown format")!=std::string::npos,"missing or other header gives defaults");
    }
    loaded=parse_bindings("psmodern-controls 1");check(loaded.bindings==d && !loaded.warning,"header alone is the defaults");
    std::string large=file({"map key F1"});large.resize(bindings_file_limit+1,'#');
    loaded=parse_bindings(large);check(loaded.bindings==d && loaded.warning,"oversize text gives defaults");
    loaded=parse_bindings(file({"map key F1"})+std::string("\0\xff\n",3));
    check(loaded.bindings.keys[Action::map]==F1 && loaded.ignored==1,"binary garbage is one ignored line");
}
void storage() {
    namespace fs=std::filesystem;
    const auto folder=fs::temp_directory_path()/("psmodern-bindings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup() { psmodern::test::remove_temporary_folder(path); } } cleanup{folder};
    auto loaded=load_bindings(folder/"missing");check(loaded.bindings==Bindings::defaults() && !loaded.warning,"missing folder gives defaults silently");
    std::mt19937 random(5);const auto b=random_valid(random);
    save_bindings(folder/"profile",b);
    check(fs::is_regular_file(folder/"profile"/bindings_file_name),"save creates the folder and controls.txt");
    loaded=load_bindings(folder/"profile");check(loaded.bindings==b && !loaded.warning,"saved bindings load back");
    std::vector<fs::path> leftovers;for (const auto& e:fs::directory_iterator(folder/"profile")) leftovers.push_back(e.path().filename());
    check(leftovers.size()==1,"atomic save leaves only controls.txt");
    save_bindings(folder/"profile",Bindings::defaults());
    check(load_bindings(folder/"profile").bindings==Bindings::defaults(),"save replaces the file");
    { std::ofstream(folder/"profile"/bindings_file_name,std::ios::binary)<<file({"map key F1","nonsense"}); }
    loaded=load_bindings(folder/"profile");
    check(loaded.bindings.keys[Action::map]==F1 && loaded.ignored==1 && loaded.warning && loaded.warning->starts_with("Controls file ") &&
          loaded.warning->find(bindings_file_name)!=std::string::npos,"damaged file keeps the usable lines and names the file");
    { std::ofstream out(folder/"profile"/bindings_file_name,std::ios::binary);out<<file({});out<<std::string(bindings_file_limit,'#'); }
    loaded=load_bindings(folder/"profile");check(loaded.bindings==Bindings::defaults() && loaded.warning,"oversize file gives defaults and a warning");
    fs::create_directories(folder/"directory"/bindings_file_name);
    loaded=load_bindings(folder/"directory");check(loaded.bindings==Bindings::defaults() && loaded.warning,"a directory named controls.txt gives defaults and a warning");
    bool refused=false;
    try { save_bindings(folder/"directory",b); } catch (const std::exception&) { refused=true; }
    check(refused && fs::is_directory(folder/"directory"/bindings_file_name),"save refuses to replace a non-file");
}

// ---- 0.2.0 reference: the samplers' tables before milestone 8, on top of the
// unchanged keyboard()/controller()/combine() from host.cpp.
struct Raw {
    std::bitset<key_slots> keys;   // Return already filtered by the sampler
    std::bitset<pad_input_count> pad;
    int x=0,y=0;
    bool focused=true,attached=true;
};
Buttons legacy_controller(const Raw& r) {
    if (!r.attached) return {};
    auto b=[&](P p) { return bool(r.pad[std::size_t(p)]); };
    return controller({{b(P::dpad_up),b(P::dpad_down),b(P::dpad_left),b(P::dpad_right),b(P::a),b(P::b)},b(P::start),b(P::back)},r.x,r.y);
}
FrameInput legacy_frame(const Raw& r) {
    Keys keys{};
    if (r.focused) keys={r.keys[UP],r.keys[DOWN],r.keys[LEFT],r.keys[RIGHT],r.keys[Z],r.keys[X],r.keys[RETURN],r.keys[BACKSPACE]};
    return combine(keyboard(keys),legacy_controller(r));
}
MenuInput legacy_menu(const Raw& r) {
    const auto pad=legacy_controller(r).pad;
    MenuInput input{pad.up,pad.down,pad.left,pad.right,pad.one,pad.two,r.attached && r.pad[std::size_t(P::y)],r.attached && r.pad[std::size_t(P::x)]};
    input.focused=r.focused;
    if (input.focused) {
        input.up|=r.keys[UP];input.down|=r.keys[DOWN];input.left|=r.keys[LEFT];input.right|=r.keys[RIGHT];
        input.accept|=r.keys[Z];input.cancel|=r.keys[X];input.toggle|=r.keys[ESCAPE];input.map|=r.keys[M];
    }
    return input;
}
MenuInput legacy_start(const Raw& r) {
    const auto buttons=legacy_controller(r);
    MenuInput input{buttons.pad.up,buttons.pad.down,buttons.pad.left,buttons.pad.right,buttons.pad.one || buttons.pause};
    input.focused=r.focused;
    if (input.focused) {
        input.up|=r.keys[UP];input.down|=r.keys[DOWN];input.left|=r.keys[LEFT];input.right|=r.keys[RIGHT];
        input.accept|=r.keys[Z] || r.keys[RETURN];
        input.cancel|=r.keys[ESCAPE];
    }
    return input;
}
bool same(const FrameInput& a,const FrameInput& b) { return a.pads==b.pads && a.pause==b.pause && a.reset==b.reset; }
bool same(const MenuInput& a,const MenuInput& b) {
    return a.up==b.up && a.down==b.down && a.left==b.left && a.right==b.right && a.accept==b.accept && a.cancel==b.cancel &&
           a.toggle==b.toggle && a.map==b.map && a.focused==b.focused;
}
// The snapshot a real sampler builds: keys only while focused, pad and
// stick only while a controller is attached.
InputSnapshot snapshot_of(const Raw& r) {
    InputSnapshot s;
    if (r.focused) s.keys=r.keys;
    if (r.attached) { s.pad=r.pad; s.stick_x=r.x; s.stick_y=r.y; }
    s.focused=r.focused; s.controller=r.attached;
    return s;
}
unsigned long long compared=0;
void compare(const Bindings& d,const Raw& r) {
    const auto s=snapshot_of(r);
    if (!same(frame_input(d,s),legacy_frame(r)) || !same(menu_input(d,s),legacy_menu(r)) || !same(start_input(s),legacy_start(r)))
        throw std::runtime_error("default bindings differ from 0.2.0: keys "+r.keys.to_string().substr(key_slots-240)+" pad "+r.pad.to_string()+
                                 " stick "+std::to_string(r.x)+","+std::to_string(r.y)+" focused "+std::to_string(r.focused)+" attached "+std::to_string(r.attached));
    ++compared;
}
void equivalence() {
    const auto d=Bindings::defaults();
    const Key console_keys[8]={UP,DOWN,LEFT,RIGHT,Z,X,RETURN,BACKSPACE};
    const P console_pads[8]={P::dpad_up,P::dpad_down,P::dpad_left,P::dpad_right,P::a,P::b,P::start,P::back};
    const Key extra_keys[4]={M,ESCAPE,TAB,F5};
    const P extra_pads[4]={P::y,P::x,P::right_shoulder,P::left_trigger};
    const int sticks[]={-32768,-8001,-8000,0,8000,8001,32767};
    auto raw=[&](unsigned keys,unsigned pads,int x,int y,bool focused,bool attached) {
        Raw r;r.x=x;r.y=y;r.focused=focused;r.attached=attached;
        for (unsigned i=0;i<8;++i) { r.keys[console_keys[i]]=keys>>i&1; r.pad[std::size_t(console_pads[i])]=pads>>i&1; }
        for (unsigned i=0;i<4;++i) { r.keys[extra_keys[i]]=keys>>(8+i)&1; r.pad[std::size_t(extra_pads[i])]=pads>>(8+i)&1; }
        return r;
    };
    // Every state of the eight default keys and pad buttons at every stick
    // value, with the non-console shortcut inputs all released and all held.
    for (unsigned extras:{0u,0xf00u})
        for (unsigned keys=0;keys<256;++keys) for (unsigned pads=0;pads<256;++pads)
            for (int x:sticks) for (int y:sticks) compare(d,raw(keys|extras,pads|extras,x,y,true,true));
    // Every state of all twelve keys and twelve pad inputs, centred stick.
    for (unsigned keys=0;keys<4096;++keys) for (unsigned pads=0;pads<4096;++pads) compare(d,raw(keys,pads,0,0,true,true));
    // Unfocused windows and a missing controller.
    for (unsigned keys=0;keys<4096;keys+=keys<255 ? 1 : 17) for (unsigned pads=0;pads<4096;pads+=pads<255 ? 1 : 17)
        for (int x:{-32768,0,8001}) for (int y:{-8001,0,32767})
            for (bool focused:{false,true}) for (bool attached:{false,true}) compare(d,raw(keys,pads,x,y,focused,attached));
    // Inputs outside the default tables never leak.
    std::mt19937 random(3);
    for (unsigned n=0;n<20000;++n) {
        Raw r=raw(random()&0xfff,random()&0xfff,0,0,true,true);
        for (unsigned k=0;k<4;++k) r.keys[key_table[random()%key_table_size].key]=true;
        r.keys[LALT]=random()&1;r.keys[RALT]=random()&1;
        r.pad[random()%pad_input_count]=true;
        compare(d,r);
    }
}
void remapped() {
    auto b=Bindings::defaults();
    assign_key(b,Action::button1,K);assign_pad(b,Action::button1,P::left_trigger);assign_key(b,Action::options,F1);
    assign_pad(b,Action::pause,P::right_trigger);
    InputSnapshot s;s.controller=true;
    s.keys[K]=true;
    check(frame_input(b,s).pads[0].one && menu_input(b,s).accept && !start_input(s).accept,"remapped key reaches the game and the menus, not the start screen");
    s.keys.reset();s.keys[Z]=true;
    check(!frame_input(b,s).pads[0].one && !menu_input(b,s).accept && start_input(s).accept,"old key no longer acts, except on the fixed start screen");
    s.keys.reset();s.pad[std::size_t(P::left_trigger)]=true;
    check(frame_input(b,s).pads[0].one && menu_input(b,s).accept,"trigger reaches button 1");
    s.pad.reset();s.pad[std::size_t(P::a)]=true;
    check(!frame_input(b,s).pads[0].one,"old pad button no longer acts");
    s.pad.reset();s.pad[std::size_t(P::right_trigger)]=true;
    check(frame_input(b,s).pause && !start_input(s).accept && !menu_input(b,s).accept,"pause on a trigger, not on the start screen");
    s.pad.reset();s.keys[F1]=true;
    check(menu_input(b,s).toggle && !start_input(s).cancel,"the options key toggles but never leaves the start screen");
    s.keys.reset();s.keys[ESCAPE]=true;
    check(!menu_input(b,s).toggle && start_input(s).cancel,"Escape always leaves the start screen");
    s.keys.reset();s.pad[std::size_t(P::y)]=true;
    check(menu_input(b,s).toggle && !start_input(s).cancel,"the options pad input never cancels the start screen");
    s.controller=false;check(!menu_input(b,s).toggle,"no controller, no pad input");
    s.pad.reset();s.keys[F1]=true;s.focused=false;
    check(!menu_input(b,s).toggle && !menu_input(b,s).focused,"unfocused keys do nothing");
}
}

int main() { try {
    names();defaults();rules();file_format();storage();equivalence();remapped();
    std::cout<<"Bindings: names, rules, controls.txt and "<<compared<<" default-equivalence states passed.\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
