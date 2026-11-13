// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/language.h"
#include "app/language_screen.h"
#include "app/localization.h"
#include "app/bindings.h"
#include "app/start_screen.h"
#include "app/controls_screen.h"
#include "app/automap_screen.h"
#include "temporary_cleanup.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool value,const char* reason) { if(!value)throw std::runtime_error(reason); }
template<class F> void refuses(F function) {
    bool refused=false;try { function(); } catch(const std::exception&) { refused=true; }
    check(refused,"invalid language input was accepted");
}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/std::filesystem::path(
        u8"psmodern-language-\u00e7\u00e3o-\u6e2c-"+std::u8string(reinterpret_cast<const char8_t*>(
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()).c_str())));
    Temporary() { check(std::filesystem::create_directory(path),"create temporary folder"); }
    ~Temporary() { test::remove_temporary_folder(path); }
};
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path,std::string_view value) {
    std::ofstream file(path,std::ios::binary);file<<value;check(bool(file),"write fixture");
}
void storage() {
    using P=LanguagePreference;
    for(const auto preference:{P::native,P::english,P::portuguese}) {
        const auto text=format_language(preference);
        check(parse_language(text)==preference,"language format round trip");
        for(std::size_t length=0;length<text.size();++length)refuses([&] {parse_language(text.substr(0,length));});
    }
    check(parse_language("psmodern-language 1\r\npt-br\r\n")==P::portuguese,"CRLF read");
    for(const auto bad:{"psmodern-language 2\nen\n","psmodern-language 1\nEN\n","psmodern-language 1\npt\n",
        "psmodern-language 1\nen\n\n","psmodern-language 1\nen \n","psmodern-language 1\nen\npt-br\n"})
        refuses([&] {parse_language(bad);});
    refuses([&] {parse_language(std::string(language_file_limit+1,'x'));});
    refuses([&] {format_language(static_cast<P>(99));});
    Temporary folder;
    check(load_language(folder.path).preference==P::native && !load_language(folder.path).warning,"missing preference native");
    check(std::filesystem::is_empty(folder.path),"reading default writes nothing");
    save_language(folder.path,P::portuguese);
    check(load_language(folder.path).preference==P::portuguese && !load_language(folder.path).warning,"global Portuguese persists");
    save_language(folder.path,P::english);
    check(load_language(folder.path).preference==P::english,"atomic replacement persists");
    check(std::distance(std::filesystem::directory_iterator(folder.path),{})==1,"only language.txt created");
    const std::string broken="not a language preference\n";
    write(folder.path/language_file_name,broken);
    auto loaded=load_language(folder.path);
    check(loaded.preference==P::native && loaded.warning && read(folder.path/language_file_name)==broken,"bad preference warned and preserved");
    save_language(folder.path,P::native);
    check(!load_language(folder.path).warning,"explicit Save repairs file");
    write(folder.path/language_file_name,std::string(language_file_limit+1,'x'));
    check(load_language(folder.path).warning.has_value(),"oversize file bounded");
    const auto nested=folder.path/"nested";
    save_language(nested,P::portuguese);check(load_language(nested).preference==P::portuguese,"Save creates folder");
    Temporary directory;
    std::filesystem::create_directory(directory.path/language_file_name);
    check(load_language(directory.path).warning.has_value(),"directory warning");
    refuses([&] {save_language(directory.path,P::english);});
    check(std::filesystem::is_directory(directory.path/language_file_name),"directory never replaced");
    write(directory.path/"blocked","keep");
    refuses([&] {save_language(directory.path/"blocked",P::english);});
    check(read(directory.path/"blocked")=="keep","failed Save preserves blocking file");
    check(resolve_language(P::native,RomVersion::us_rev_a)==Language::english && resolve_language(P::native,RomVersion::brazilian)==Language::portuguese,"native follows fixed profile");
    check(resolve_language(P::portuguese,RomVersion::us_rev_a)==Language::portuguese && resolve_language(P::english,RomVersion::brazilian)==Language::english,"global preference crosses profiles");
}
struct Rig {
    LanguageScreen screen;
    double now=0;
    Rig(LanguagePreference preference=LanguagePreference::native,bool us=true,bool br=true) {screen.open(preference,us,br);step({});}
    LanguageAction step(MenuInput input,double elapsed=.02) {now+=elapsed;return screen.update(input,now);}
    LanguageAction tap(MenuInput input) {step({});auto result=step(input);step({});return result;}
    void down() {tap({.down=true});}
    void up() {tap({.up=true});}
};
void navigation() {
    using P=LanguagePreference;using A=LanguageAction;
    LanguageScreen opening;opening.open(P::native,true,true);
    check(opening.update({.accept=true},0)==A::none && opening.update({.down=true},1)==A::none && opening.selected()==0,"opening buttons consumed");
    opening.update({},2);opening.update({.down=true},3);
    check(opening.selected()==1,"focused release arms");
    opening.update({.down=true},3.1);check(opening.selected()==1,"first repeat delay");
    opening.update({.down=true},3.36);check(opening.selected()==2,"held direction repeats");
    opening.update({.down=true,.focused=false},4);opening.update({.down=true},5);
    check(opening.selected()==2,"focus return does not move held direction");
    opening.update({},6);opening.update({.down=true},7);check(opening.selected()==3,"focus rearmed");
    Rig rig;
    rig.down();rig.down();check(rig.screen.selected()==2,"Portuguese row");
    check(rig.tap({.accept=true})==A::none && rig.screen.draft()==P::portuguese && rig.screen.visible(),"language selection only changes draft");
    rig.down();check(rig.tap({.accept=true})==A::save && rig.screen.visible(),"Save handed to owner");
    rig.screen.failed();check(rig.screen.visible() && rig.screen.draft()==P::portuguese,"failed Save keeps draft for retry");
    check(rig.tap({.accept=true})==A::save,"Save retries");rig.screen.saved();check(!rig.screen.visible(),"successful Save closes");
    Rig cancel;cancel.up();check(cancel.screen.selected()==4 && cancel.tap({.accept=true})==A::close && !cancel.screen.visible(),"wrap to Cancel discards");
    Rig toggle;check(toggle.tap({.toggle=true})==A::close,"Options shortcut cancels");
    Rig blocked(P::english,false,true);
    check(!blocked.screen.available(P::english) && blocked.screen.available(P::portuguese),"availability uses verified images");
    blocked.down();blocked.tap({.accept=true});check(blocked.screen.draft()==P::portuguese,"available language replaces missing preference");
    Rig missing(P::portuguese,true,false);
    missing.tap({.accept=true});missing.down();check(missing.tap({.accept=true})==A::none && missing.screen.visible(),"unavailable retained preference cannot Save");
    missing.up();missing.up();missing.tap({.accept=true});check(missing.screen.draft()==P::english,"explicit available selection accepted");
    Rig none(P::native,false,false);none.down();none.down();none.down();check(none.tap({.accept=true})==A::none,"no image cannot Save");
    none.tap({.cancel=true});check(!none.screen.visible(),"unavailable languages never trap Cancel");
}
using Frame=std::vector<std::uint32_t>;
void preview(const std::filesystem::path& output,std::string_view name,Language language,const Frame& pixels) {
    if(output.empty())return;
    std::filesystem::create_directories(output);
    const auto suffix=language==Language::english ? "-en.ppm" : "-pt.ppm";
    std::ofstream file(output/(std::string(name)+suffix),std::ios::binary);
    file<<"P6\n256 192\n255\n";for(auto pixel:pixels)for(int shift:{16,8,0})file.put(char(pixel>>shift));
    check(bool(file),"write localized screen preview");
}
bool shows(const Frame& pixels,const RomFont& font,int x,int y,std::string_view text,std::uint32_t color=0xffffffff) {
    check(x>=8 && x+int(text.size())*8<=248 && y>=8 && y+8<=184,"localized label fits inside border");
    Frame expected(256*192,0);font.text(expected,x,y,text,color);
    bool drawn=false;
    for(std::size_t i=0;i<expected.size();++i)if(expected[i]) {drawn=true;if(pixels[i]!=expected[i])return false;}
    return drawn;
}
void localized_screens(const RomFont& font,Language language,const std::filesystem::path& output) {
    RomList list;list.us="C:/us.sms";list.br="C:/br.sms";
    StartScreen start(list);start.update({},0);
    check(start.entries().size()==6,"launcher has six entries with both ROMs");
    for(const auto preference:{LanguagePreference::native,LanguagePreference::english,LanguagePreference::portuguese}) {
        Frame pixels(256*192);start.render(pixels,font,language,preference);
        check(shows(pixels,font,32,96,std::string(ui_text(UiText::language,language))+" "+std::string(language_label(preference,language))),"all global preference labels fit launcher");
    }
    for(unsigned row=0;row<start.entries().size();++row) {
        Frame pixels(256*192);start.render(pixels,font,language,LanguagePreference::portuguese);
        // Six entries: window at (16,32), first label at (32,48).
        check(shows(pixels,font,32,48,ui_text(UiText::play,language)),"launcher play label localized");
        check(shows(pixels,font,32,64,ui_text(UiText::choose_rom,language)),"launcher add ROM label localized");
        check(shows(pixels,font,32,96,std::string(ui_text(UiText::language,language))+" "+std::string(language_label(LanguagePreference::portuguese,language))),"launcher shows the presentation language");
        Frame cursor(256*192);font.window(cursor,16,32,224,120,48+16*int(row));
        for(int y=32;y<152;++y)for(int x=16;x<24;++x)
            check(pixels[y*256+x]==cursor[y*256+x],"launcher cursor matches selected row");
        if(row==0)preview(output,"start",language,pixels);
        start.update({.down=true},2*row+1);start.update({},2*row+2);
    }
    OptionsMenu menu;menu.open({25,400},false,false,false,true);menu.update({},0);
    for(unsigned row=0;row<5;++row) {
        Frame pixels(256*192,0xff123456);menu.render(pixels,font,language);
        check(menu.selected()>=menu.first_visible() && menu.selected()<menu.first_visible()+4,"Options cursor remains visible");
        Frame cursor(256*192);font.window(cursor,24,24,208,152,48+32*int(menu.selected()-menu.first_visible()));
        for(int y=32;y<168;++y)for(int x=24;x<32;++x)check(pixels[y*256+x]==cursor[y*256+x],"Options cursor matches visible selected row");
        for(int y=0;y<192;++y)for(int x=0;x<256;++x)
            if(x<24 || x>=232 || y<24 || y>=176)check(pixels[y*256+x]==0xff123456,"localized Options preserves outside frame");
        if(row==0)preview(output,"options",language,pixels);
        if(row==4) {
            check(shows(pixels,font,40,144,ui_text(UiText::language,language)),"Language action visible after scroll");
            preview(output,"options-language",language,pixels);
        }
        menu.update({.down=true},2*row+1);menu.update({},2*row+2);
    }
    menu.failed();Frame failed(256*192);menu.render(failed,font,language);
    check(shows(failed,font,40,152,ui_text(UiText::save_failed,language)),"localized save failure fits Options");
    ControlsScreen controls;controls.open(Bindings::defaults());controls.update({},0);
    for(unsigned row=0;row<controls_layout::rows;++row) {
        Frame pixels(256*192);controls.render(pixels,font,language);
        const auto title=ui_text(UiText::controls,language);
        check(shows(pixels,font,(256-int(title.size())*8)/2,8,title),"Controls title localized");
        check(shows(pixels,font,112,24,ui_text(UiText::keyboard,language)),"Controls keyboard header localized");
        check(shows(pixels,font,language==Language::portuguese ? 184 : 192,24,ui_text(UiText::gamepad,language)),"Controls gamepad header stays inside border");
        if(row==0)preview(output,"controls",language,pixels);
        if(row==controls_layout::save_row)preview(output,"controls-save",language,pixels);
        controls.update({},2*row+.2);InputSnapshot down;down.keys[81]=true;controls.update(down,2*row+1);
    }
    controls.open(Bindings::defaults());controls.update({},0);
    InputSnapshot accept;accept.keys[40]=true;controls.update(accept,.1);
    const auto capture=std::string(ui_text(UiText::press_key_for,language))+std::string(ui_action_label(Action::up,language));
    check(controls.localized_status(language)==capture,"capture status resolves language dynamically");
    Frame capture_pixels(256*192);controls.render(capture_pixels,font,language);
    check(shows(capture_pixels,font,16,168,capture),"localized capture status is fully rendered");
    preview(output,"controls-capture",language,capture_pixels);
    controls.failed();check(controls.localized_status(language)==ui_text(UiText::save_failed,language),"failure status resolves language dynamically");
    game::MapView view{};view.floor_level=-12;view.party_ahead=-16;
    Frame map(256*192);render_automap(map,font,view,language);
    check(shows(map,font,24,16,ui_text(UiText::map,language)),"automap title localized");
    check(shows(map,font,168,16,floor_label_text(-12,language)),"localized floor label fits");
    constexpr UiText legend[]{UiText::you,UiText::chest,UiText::empty,UiText::up,UiText::down,UiText::exit,UiText::door,UiText::locked,UiText::magic,UiText::pit};
    for(unsigned i=0;i<std::size(legend);++i)check(shows(map,font,180,32+14*int(i),ui_text(legend[i],language)),"localized map legend fits");
    preview(output,"map",language,map);
}
void rom(const std::filesystem::path& path,const std::filesystem::path& output) {
    std::ifstream file(path,std::ios::binary);
    Memory::Rom bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    RomFont font(bytes);
    for(const auto language:{Language::english,Language::portuguese}) {
        for(unsigned i=0;i<unsigned(UiText::choose_again);++i)
            for(const char c:ui_text(UiText(i),language))check(font.supports(c),"catalog label requires unsupported ROM glyph");
        for(unsigned i=0;i<unsigned(Action::count);++i) {
            const auto label=ui_action_label(Action(i),language);
            check(label.size()<=11,"action fits Controls column");
            for(const char c:label)check(font.supports(c),"action uses ROM glyph");
            for(const auto prefix:{UiText::press_key_for,UiText::press_button_for,UiText::swapped_with,UiText::key_used_by,UiText::button_used_by})
                check(ui_text(prefix,language).size()+label.size()<=28,"localized Controls status fits");
        }
        for(const auto entry:key_table)check(ui_key_label(entry.key,language).size()<=9,"key label fits Controls column");
        for(unsigned i=0;i<unsigned(PadInput::count);++i)check(ui_pad_label(PadInput(i),language).size()<=7,"localized pad label fits Controls column");
        localized_screens(font,language,output);
        for(bool us:{false,true})for(bool br:{false,true})for(auto preference:{LanguagePreference::native,LanguagePreference::english,LanguagePreference::portuguese}) {
            Rig rig(preference,us,br);
            for(unsigned i=0;i<5;++i) {
                std::vector<std::uint32_t> pixels(256*192,0xff123456);
                rig.screen.render(pixels,font,language);
                for(int y=0;y<192;++y)for(int x=0;x<256;++x)
                    if(x<16 || x>=240 || y<16 || y>=176)check(pixels[y*256+x]==0xff123456,"language screen preserves outside frame");
                rig.down();
            }
        }
        if(!output.empty()) {
            Rig rig;rig.down();rig.down();rig.tap({.accept=true});
            std::vector<std::uint32_t> pixels(256*192,0xff000000);rig.screen.render(pixels,font,language);
            preview(output,"language",language,pixels);
        }
    }
}
}
int main(int argc,char** argv) {try {
    if(argc>1) {
        if(std::string_view(argv[1]).empty() || !std::filesystem::exists(argv[1]))return 77;
        rom(argv[1],argc>2 ? argv[2] : std::filesystem::path{});
    } else {storage();navigation();}
    std::cout<<"Language checks passed.\n";return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
