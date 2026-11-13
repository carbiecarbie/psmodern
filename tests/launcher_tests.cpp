// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "app/rom_list.h"
#include "app/start_screen.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
template<class F> void refuses(F f,const char* text) {
    bool refused=false; try { f(); } catch (const std::exception&) { refused=true; }
    check(refused,text);
}
// The folder name is non-ASCII on purpose: the production folder is under the user's profile.
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/std::filesystem::path(
        u8"psmodern-launcher-\u00e7\u00e3o-\u65e5\u672c-"+std::u8string(reinterpret_cast<const char8_t*>(
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()).c_str())));
    Temporary() { check(std::filesystem::create_directory(path),"unique temporary folder"); }
    ~Temporary() { test::remove_temporary_folder(path); }
};
std::filesystem::path unicode_rom() { return std::filesystem::path(u8"C:/Jogos/\u00e7\u00e3o/\u65e5\u672c/ps.sms"); }
void write_file(const std::filesystem::path& path,const std::string& text) {
    std::ofstream file(path,std::ios::binary); file.write(text.data(),static_cast<std::streamsize>(text.size()));
    check(bool(file),"writing a test file");
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}
const std::string header="psmodern-roms 1\n";

void rom_list_round_trip() {
    RomList both;both.set(RomVersion::us_rev_a,"C:/Games/ps us.sms");both.set(RomVersion::brazilian,"D:/br.sms");
    const auto text=format_rom_list(both);
    check(text=="psmodern-roms 1\nus C:/Games/ps us.sms\nbr D:/br.sms\n","format is header, us, br, LF");
    check(parse_rom_list(text)==both,"round trip with both versions");
    RomList only_br;only_br.set(RomVersion::brazilian,"D:/br.sms");
    check(format_rom_list(only_br)=="psmodern-roms 1\nbr D:/br.sms\n" && parse_rom_list(format_rom_list(only_br))==only_br,"one version");
    check(format_rom_list({})==header && parse_rom_list(header).empty(),"empty list is just the header");
    check(both.get(RomVersion::us_rev_a)==both.us && both.get(RomVersion::brazilian)==both.br,"get picks the slot");
    both.clear(RomVersion::us_rev_a);check(!both.us && both.br,"clear empties one slot");
}
void rom_list_rejects() {
    refuses([] { parse_rom_list(""); },"empty text");
    refuses([] { parse_rom_list("psmodern-roms 2\n"); },"wrong version");
    refuses([] { parse_rom_list("psmodern-options 1\n"); },"wrong header");
    refuses([] { parse_rom_list("psmodern-roms 1"); },"header without newline is truncated");
    refuses([] { parse_rom_list(header+"us C:/a.sms"); },"last line without newline is truncated");
    refuses([] { parse_rom_list(header+"us C:/a.sms\nus C:/b.sms\n"); },"duplicate tag");
    refuses([] { parse_rom_list(header+"xx C:/a.sms\n"); },"unknown tag");
    refuses([] { parse_rom_list(header+"br C:/a.sms\nus C:/b.sms\n"); },"out of order");
    refuses([] { parse_rom_list(header+"us a.sms\n"); },"relative path");
    refuses([] { parse_rom_list(header+"us ../a.sms\n"); },"relative path with dots");
    refuses([] { parse_rom_list(header+"us \n"); },"empty path");
    refuses([] { parse_rom_list(header+"us\n"); },"tag without path");
    refuses([] { parse_rom_list(header+"us C:/a\tb.sms\n"); },"control character");
    refuses([] { parse_rom_list(header+"us C:/a\rb.sms\n"); },"carriage return inside a path");
    refuses([] { parse_rom_list(header+"us C:/a.sms\n\n"); },"trailing blank line");
    refuses([] { parse_rom_list(header+"us C:/a.sms\nzzz"); },"trailing data");
    refuses([] { parse_rom_list(header+"us C:/"+std::string(rom_list_limit,'a')+"\n"); },"oversize");
    RomList bad;bad.set(RomVersion::us_rev_a,"relative.sms");
    refuses([&] { format_rom_list(bad); },"format refuses a relative path");
    bad.set(RomVersion::us_rev_a,"C:/a\nb.sms");
    refuses([&] { format_rom_list(bad); },"format refuses control characters");
    RomList limit;limit.set(RomVersion::us_rev_a,"C:/"+std::string(rom_list_limit,'a'));
    refuses([&] { format_rom_list(limit); },"format refuses an oversize list");
}
void rom_list_crlf() {
    const auto list=parse_rom_list("psmodern-roms 1\r\nus C:/a.sms\r\nbr D:/b.sms\r\n");
    check(list.us==std::filesystem::path("C:/a.sms") && list.br==std::filesystem::path("D:/b.sms"),"CRLF accepted");
}
void rom_list_unicode() {
    RomList list;list.set(RomVersion::us_rev_a,unicode_rom());list.set(RomVersion::brazilian,unicode_rom().parent_path()/u8"\u00e9.sms");
    const auto text=format_rom_list(list);
    check(text.find("C:/Jogos/\xc3\xa7\xc3\xa3o/\xe6\x97\xa5\xe6\x9c\xac/ps.sms")!=std::string::npos,"paths are written as UTF-8");
    check(parse_rom_list(text)==list,"non-ASCII round trip through format and parse");
    Temporary folder;
    save_rom_list(folder.path,list);
    const auto loaded=load_rom_list(folder.path);
    check(!loaded.warning && loaded.list==list,"non-ASCII round trip through save and load in a non-ASCII folder");
    check(read_file(folder.path/rom_list_name)==text,"the file holds exactly the formatted text");
}
void rom_list_storage() {
    Temporary folder;
    auto loaded=load_rom_list(folder.path);
    check(loaded.list.empty() && !loaded.warning,"a missing file is empty without a warning");
    loaded=load_rom_list(folder.path/"not-created");
    check(loaded.list.empty() && !loaded.warning,"a missing folder is empty without a warning");
    const auto nested=folder.path/"a"/"b";
    RomList list;list.set(RomVersion::us_rev_a,"C:/a.sms");
    save_rom_list(nested,list);
    check(load_rom_list(nested).list==list,"save creates the folder");
    list.set(RomVersion::brazilian,"D:/b.sms");save_rom_list(nested,list);
    check(load_rom_list(nested).list==list,"save replaces the file");
    for (const auto& entry:std::filesystem::directory_iterator(nested)) check(entry.path().filename()==rom_list_name,"no temporary file is left behind");
}
void rom_list_corrupt() {
    Temporary folder;
    const auto file=folder.path/rom_list_name;
    for (const auto& text:{std::string("garbage"),header+"us relative\n",header+"us C:/a.sms\nus C:/b.sms\n",std::string(rom_list_limit+1,'x')}) {
        write_file(file,text);
        const auto loaded=load_rom_list(folder.path);
        check(loaded.list.empty() && loaded.warning && !loaded.warning->empty(),"a corrupt or oversize file loads empty with one warning");
    }
    write_file(file,header+"us C:/\xff\xfe.sms\n");
    const auto invalid=load_rom_list(folder.path);
    check(invalid.list.empty() && invalid.warning && !invalid.warning->empty(),"invalid UTF-8 loads empty with one warning");
    RomList list;list.set(RomVersion::us_rev_a,"C:/a.sms");
    save_rom_list(folder.path,list);
    const auto fixed=load_rom_list(folder.path);
    check(fixed.list==list && !fixed.warning,"the next save replaces a corrupt file");
}
void rom_list_directory() {
    Temporary folder;
    std::filesystem::create_directory(folder.path/rom_list_name);
    const auto loaded=load_rom_list(folder.path);
    check(loaded.list.empty() && loaded.warning,"a directory named roms.txt is refused with a warning");
    RomList list;list.set(RomVersion::us_rev_a,"C:/a.sms");
    refuses([&] { save_rom_list(folder.path,list); },"saving never replaces a directory");
    check(std::filesystem::is_directory(folder.path/rom_list_name),"the directory is untouched");
}
RomList rom_list(std::optional<std::filesystem::path> us,std::optional<std::filesystem::path> br) { RomList list;list.us=us;list.br=br;return list; }
using A=StartAction;
MenuInput neutral() { return {}; }
MenuInput press(bool MenuInput::*button) { MenuInput input;input.*button=true;return input; }
void start_screen_entries() {
    auto expect=[&](const RomList& list,std::vector<A> entries,const char* text) {
        entries.insert(entries.end(),{A::choose,A::controls,A::open_saves,A::quit});check(StartScreen(list).entries()==entries,text);
    };
    expect({},{},"no remembered ROM: choose, controls, saves and quit");
    expect(rom_list("C:/us.sms",{}),{A::play_us},"US only");
    expect(rom_list({},"C:/br.sms"),{A::play_br},"BR only");
    expect(rom_list("C:/us.sms","C:/br.sms"),{A::play_us,A::play_br},"US before BR");
    check(StartScreen({}).selected()==0,"cursor starts on the first entry");
    check(StartScreen(rom_list("C:/us.sms","C:/br.sms"),A::play_br).selected()==1,"cursor can start on a given entry");
    check(StartScreen(rom_list("C:/us.sms","C:/br.sms"),A::controls).selected()==3,"the cursor can come back to CONTROLS");
    check(StartScreen(rom_list("C:/us.sms",{}),A::play_br).selected()==0,"an absent entry leaves the cursor on the first");
    check(std::string(StartScreen::label(A::play_us))=="PLAY PHANTASY STAR (US)" && std::string(StartScreen::label(A::play_br))=="PLAY PHANTASY STAR (BR)" &&
          std::string(StartScreen::label(A::choose))=="CHOOSE ANOTHER ROM" && std::string(StartScreen::label(A::controls))=="CONTROLS" && std::string(StartScreen::label(A::open_saves))=="OPEN SAVES FOLDER" &&
          std::string(StartScreen::label(A::quit))=="QUIT" && StartScreen::footer()=="PSMODERN 0.2.0","labels and footer");
}
void start_screen_input() {
    const auto both=rom_list("C:/us.sms","C:/br.sms");
    {
        StartScreen screen(both);
        check(screen.update(press(&MenuInput::accept),0)==A::none,"accept held when the screen appears is ignored");
        check(screen.update(press(&MenuInput::accept),1)==A::none,"still held: still ignored");
        check(screen.update(neutral(),2)==A::none,"neutral input arms");
        check(screen.update(press(&MenuInput::accept),3)==A::play_us,"Z, Enter, A or Start accepts the selected entry");
        check(screen.update(press(&MenuInput::accept),4)==A::none,"a held accept is taken once");
    }
    // Every entry returns its own action; down moves one entry per press.
    for (unsigned target=0;target<6;++target) {
        StartScreen screen(both);screen.update(neutral(),0);
        double now=1;
        for (unsigned i=0;i<target;++i) { screen.update(press(&MenuInput::down),now);screen.update(neutral(),now+.01);now+=1; }
        check(screen.selected()==target,"down moves one entry per press");
        check(screen.update(press(&MenuInput::accept),now)==screen.entries()[target],"accept returns the selected entry");
    }
    {
        StartScreen screen(both);screen.update(neutral(),0);
        screen.update(press(&MenuInput::up),1);check(screen.selected()==5,"up from the first entry wraps to QUIT");
        screen.update(neutral(),1.1);screen.update(press(&MenuInput::down),2);check(screen.selected()==0,"down from QUIT wraps to the first entry");
        screen.update(neutral(),2.1);
        auto both_ways=press(&MenuInput::up);both_ways.down=true;
        screen.update(both_ways,3);check(screen.selected()==0,"up and down together do not move");
        screen.update(neutral(),3.1);screen.update(press(&MenuInput::left),4);screen.update(press(&MenuInput::right),5);
        check(screen.selected()==0,"left and right do not move");
    }
    {
        // OptionsMenu's repeat: first move at once, the next after .35 s, then every .12 s.
        StartScreen screen(both);screen.update(neutral(),0);
        const auto down=press(&MenuInput::down);
        screen.update(down,10);check(screen.selected()==1,"fresh press moves");
        screen.update(down,10.34);check(screen.selected()==1,"no repeat before .35 s");
        screen.update(down,10.36);check(screen.selected()==2,"repeat after .35 s");
        screen.update(down,10.47);check(screen.selected()==2,"no repeat before .12 s");
        screen.update(down,10.49);check(screen.selected()==3,"repeat every .12 s");
    }
    {
        StartScreen screen(both);screen.update(neutral(),0);
        check(screen.update(press(&MenuInput::cancel),1)==A::quit,"Esc quits");
        StartScreen held(both);
        check(held.update(press(&MenuInput::cancel),0)==A::none && held.update(press(&MenuInput::cancel),1)==A::none,"Esc held at start is ignored");
        check(held.update(neutral(),2)==A::none && held.update(press(&MenuInput::cancel),3)==A::quit,"Esc after a release quits");
        StartScreen toggle(both);toggle.update(neutral(),0);
        check(toggle.update(press(&MenuInput::toggle),1)==A::none,"the Options toggle does nothing here");
    }
    {
        StartScreen screen(both);screen.update(neutral(),0);
        auto unfocused=press(&MenuInput::accept);unfocused.focused=false;
        check(screen.update(unfocused,1)==A::none,"input without focus is ignored");
        check(screen.update(press(&MenuInput::accept),2)==A::none,"a button held when focus returns is ignored");
        auto lost=neutral();lost.focused=false;
        screen.update(neutral(),3);screen.update(lost,4);
        check(screen.update(press(&MenuInput::accept),5)==A::none,"losing focus disarms even after a neutral snapshot");
        check(screen.update(neutral(),6)==A::none && screen.update(press(&MenuInput::accept),7)==A::play_us,"neutral focused input arms again");
    }
}
// The launcher's decisions against scripted dialogs and files.
struct FakePlatform : LauncherPlatform {
    RomListLoad stored;
    std::vector<std::optional<std::filesystem::path>> picks;
    std::map<std::filesystem::path,RomVersion> roms;
    std::set<std::filesystem::path> offline;
    std::vector<bool> answers;
    std::vector<std::string> messages,questions,give_ups;
    std::vector<RomList> saves;
    bool save_fails=false;
    std::size_t picked=0;
    std::optional<std::filesystem::path> pick_rom() override {
        check(picked<picks.size(),"unexpected dialog");return picks[picked++];
    }
    void show_message(const std::string& text) override { messages.push_back(text); }
    bool ask_retry(const std::string& text,const char* give_up) override {
        check(questions.size()<answers.size(),"unexpected question");
        questions.push_back(text);give_ups.push_back(give_up);return answers[questions.size()-1];
    }
    RomListLoad load_list() override { return stored; }
    void save_list(const RomList& list) override { if (save_fails) throw std::runtime_error("disk full"); saves.push_back(list); }
    RomVersion identify(const std::filesystem::path& path) override {
        if (offline.count(path)) throw RomUnreadable("Cannot open ROM file.");
        const auto found=roms.find(path);
        if (found==roms.end()) throw std::runtime_error("Unknown ROM dump.");
        return found->second;
    }
};
const std::filesystem::path us_a="C:/roms/us-a.sms",us_b="C:/roms/us-b.sms",br_a="C:/roms/br-a.sms",bad="C:/roms/other.sms";
FakePlatform platform() {
    FakePlatform fake;fake.roms={{us_a,RomVersion::us_rev_a},{us_b,RomVersion::us_rev_a},{br_a,RomVersion::brazilian}};return fake;
}
void launcher_flow() {
    {
        auto fake=platform();fake.picks={std::nullopt};
        check(!prepare_launcher(fake) && fake.saves.empty() && fake.messages.empty(),"cancelling the first dialog exits and writes nothing");
    }
    {
        auto fake=platform();fake.picks={bad};fake.answers={false};
        check(!prepare_launcher(fake) && fake.saves.empty(),"Quit after an unsupported file writes nothing");
        check(fake.give_ups==std::vector<std::string>{"Quit"},"the first run offers Quit");
    }
    {
        auto fake=platform();fake.picks={bad,us_a};fake.answers={true};
        const auto list=prepare_launcher(fake);
        check(list && *list==rom_list(us_a,{}),"choose again after an unsupported file, then a supported one");
        check(fake.saves==std::vector<RomList>{rom_list(us_a,{})},"the pick is remembered once");
        const auto& text=fake.questions.at(0);
        for (const char* part:{"other.sms","Unknown ROM dump.","uncompressed .sms","US Rev-A","Brazilian Tec Toy"})
            check(text.find(part)!=std::string::npos,"the refusal names the file, the reason and the accepted dumps");
    }
    {
        auto fake=platform();fake.stored.list=rom_list(us_a,br_a);
        check(prepare_launcher(fake)==rom_list(us_a,br_a) && fake.saves.empty() && fake.messages.empty() && !fake.picked,
              "usable remembered ROMs go straight to the start screen untouched");
    }
    {
        auto fake=platform();fake.stored.list=rom_list(us_a,br_a);fake.roms.erase(br_a);
        check(prepare_launcher(fake)==rom_list(us_a,{}) && !fake.picked,"a missing ROM is forgotten, the other kept");
        check(fake.messages.size()==1 && fake.messages[0].find("br-a.sms")!=std::string::npos,"one message names it");
        check(fake.saves==std::vector<RomList>{rom_list(us_a,{})},"and the list is rewritten");
    }
    {
        // br-a is on an unplugged drive: kept, not offered, named once, and written back by later saves.
        auto fake=platform();fake.stored.list=rom_list(us_a,br_a);fake.offline.insert(br_a);
        RomList unreadable;
        check(prepare_launcher(fake,&unreadable)==rom_list(us_a,{}) && unreadable==rom_list({},br_a) && !fake.picked,"an unreadable ROM is not offered");
        check(fake.saves.empty(),"and the list is not rewritten");
        check(fake.messages.size()==1 && fake.messages[0].find("br-a.sms")!=std::string::npos &&
              fake.messages[0].find("could not be read now; still remembered")!=std::string::npos &&
              fake.messages[0].find("forgot")==std::string::npos,"one message says it is still remembered");
        auto list=rom_list(us_a,{});fake.picks={us_b};
        check(choose_rom(fake,list,false,unreadable)==RomVersion::us_rev_a && fake.saves==std::vector<RomList>{rom_list(us_b,br_a)},
              "a later save keeps the unreadable ROM");
    }
    {
        // One message when one ROM is forgotten and the other cannot be read; the save keeps the unreadable one.
        auto fake=platform();fake.stored.list=rom_list(us_a,br_a);fake.roms.erase(us_a);fake.offline.insert(br_a);fake.picks={us_b};
        RomList unreadable;
        const auto list=prepare_launcher(fake,&unreadable);
        check(list==rom_list(us_b,{}) && unreadable==rom_list({},br_a),"with the other ROM forgotten the dialog opens and the unreadable one stays");
        check(fake.messages.size()==1 && fake.messages[0].find("us-a.sms")!=std::string::npos && fake.messages[0].find("br-a.sms")!=std::string::npos,
              "one message covers both");
        check(fake.saves==std::vector<RomList>{rom_list({},br_a),rom_list(us_b,br_a)},"every save keeps the unreadable ROM");
    }
    {
        // us-b now holds the Brazilian dump and br-a is gone: both forgotten in one message, then the dialog.
        auto fake=platform();fake.stored.list=rom_list(us_b,br_a);fake.roms[us_b]=RomVersion::brazilian;fake.roms.erase(br_a);
        fake.picks={std::nullopt};
        check(!prepare_launcher(fake),"with nothing left the dialog opens, and cancel exits");
        check(fake.messages.size()==1 && fake.messages[0].find("us-b.sms")!=std::string::npos && fake.messages[0].find("br-a.sms")!=std::string::npos,
              "one message lists every forgotten ROM");
        check(fake.saves==std::vector<RomList>{RomList{}},"the emptied list is written once, nothing after the cancel");
    }
    {
        auto fake=platform();fake.stored.warning="Ignoring the remembered ROM list";fake.picks={us_a};
        check(prepare_launcher(fake)==rom_list(us_a,{}) && fake.messages==std::vector<std::string>{"Ignoring the remembered ROM list"},
              "a corrupt list is reported and replaced by the next pick");
    }
    {
        auto fake=platform();fake.picks={us_a};fake.save_fails=true;
        check(prepare_launcher(fake)==rom_list(us_a,{}) && fake.messages.size()==1 && fake.messages[0].find("disk full")!=std::string::npos,
              "a pick that cannot be remembered is reported and still played");
    }
    {
        auto fake=platform();fake.picks={br_a};auto list=rom_list(us_a,{});
        check(choose_rom(fake,list,false)==RomVersion::brazilian && list==rom_list(us_a,br_a),"the other version is added beside");
        check(fake.saves==std::vector<RomList>{rom_list(us_a,br_a)},"and remembered");
    }
    {
        auto fake=platform();fake.picks={us_b};auto list=rom_list(us_a,br_a);
        check(choose_rom(fake,list,false)==RomVersion::us_rev_a && list==rom_list(us_b,br_a),"the same version is replaced");
    }
    {
        auto fake=platform();fake.picks={bad};fake.answers={false};auto list=rom_list(us_a,{});
        check(!choose_rom(fake,list,false) && list==rom_list(us_a,{}) && fake.saves.empty(),"Back after an unsupported file changes nothing");
        check(fake.give_ups==std::vector<std::string>{"Back"},"the start screen offers Back");
        fake.picks.push_back(std::nullopt);
        check(!choose_rom(fake,list,false) && fake.saves.empty(),"cancel from the start screen changes nothing");
    }
    {
        const std::filesystem::path relative="relative.sms";
        auto fake=platform();fake.picks={relative};fake.roms[relative]=RomVersion::us_rev_a;
        RomList list;
        check(choose_rom(fake,list,true) && list.us && list.us->is_absolute(),"the remembered path is absolute");
    }
    check(font_rom(rom_list(us_a,br_a))==us_a && font_rom(rom_list({},br_a))==br_a,"the font comes from the US ROM when there is one");
    refuses([] { font_rom({}); },"no font without a ROM");
}
// Needs the player's ROM: every label is in that ROM's font, the screen draws,
// and a copy in a non-ASCII folder is identified.
int rom_checks(const std::filesystem::path& rom,std::string_view version) {
    if (rom.empty() || !std::filesystem::exists(rom)) return 77;
    const auto expected=version=="us" ? RomVersion::us_rev_a : RomVersion::brazilian;
    const auto loaded=load_rom(rom);
    check(loaded.version==expected,"wrong ROM for this test");
    const RomFont font(*loaded.rom);
    for (const auto action:{A::play_us,A::play_br,A::choose,A::controls,A::open_saves,A::quit})
        for (const char c:std::string(StartScreen::label(action))) check(font.supports(c),"every start screen label is in the ROM font");
    for (const char c:StartScreen::footer()) check(font.supports(c),"the footer is in the ROM font");
    std::vector<std::uint32_t> pixels(256*192);
    for (const auto& list:{rom_list("C:/us.sms",{}),rom_list({},"C:/br.sms"),rom_list("C:/us.sms","C:/br.sms")}) {
        StartScreen screen(list);screen.update(neutral(),0);
        for (std::size_t i=0;i<screen.entries().size();++i) {
            std::fill(pixels.begin(),pixels.end(),0xff123456);
            screen.render(pixels,font);
            check(std::all_of(pixels.begin(),pixels.end(),[](auto p) { return p==0xff000000 || p==0xffffffff || p==0xffaaaaaa; }),
                  "the screen is black with a white window and text and a grey footer");
            check(std::count(pixels.begin(),pixels.end(),0xffaaaaaa)>0,"the footer is drawn");
            if (screen.entries().size()==6)
                check(std::all_of(pixels.begin()+152*256,pixels.begin()+176*256,[](auto p) { return p==0xff000000; }),"six entries end at y=152, above the footer");
            screen.update(press(&MenuInput::down),double(2*i+1));screen.update(neutral(),double(2*i+2));
        }
    }
    Temporary folder;
    const auto copy=folder.path/std::filesystem::path(u8"\u00e7\u00e3o \u65e5\u672c")/std::filesystem::path(u8"Phantasy Star \u00e9.sms");
    std::filesystem::create_directory(copy.parent_path());
    std::filesystem::copy_file(rom,copy);
    check(identify_rom(copy)==expected && load_rom(copy).version==expected,"a ROM in a non-ASCII folder is identified and loaded");
    std::cout<<"Start screen ("<<version<<"): labels and footer in the ROM font, 3 lists rendered at every cursor position, non-ASCII ROM path identified.\n";
    return 0;
}
}
int main(int argc,char** argv) {
    try {
        if (argc==3) return rom_checks(argv[1],argv[2]);
        rom_list_round_trip(); rom_list_rejects(); rom_list_crlf(); rom_list_unicode(); rom_list_storage(); rom_list_corrupt(); rom_list_directory();
        start_screen_entries(); start_screen_input(); launcher_flow();
        std::cout<<"Launcher: 10 test groups passed (no SDL or ROM).\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
