// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/start_screen.h"
#include "PSModernVersion.h"
#include <algorithm>
#include <stdexcept>

namespace psmodern::app {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
// A ROM that cannot be remembered can still be played in this session.
void remember(LauncherPlatform& platform,const RomList& list,const RomList& unreadable) {
    RomList saved=list;
    for (const auto version:{RomVersion::us_rev_a,RomVersion::brazilian})
        if (!saved.get(version) && unreadable.get(version)) saved.set(version,*unreadable.get(version));
    try { platform.save_list(saved); }
    catch (const std::exception& e) { platform.show_message(std::string("PSModern cannot remember the chosen ROMs: ")+e.what()); }
}
}
StartScreen::StartScreen(const RomList& list,StartAction selected) {
    if (list.us) entries_.push_back(StartAction::play_us);
    if (list.br) entries_.push_back(StartAction::play_br);
    entries_.insert(entries_.end(),{StartAction::choose,StartAction::open_saves,StartAction::quit});
    const auto found=std::find(entries_.begin(),entries_.end(),selected);
    if (found!=entries_.end()) selected_=static_cast<unsigned>(found-entries_.begin());
}
StartAction StartScreen::update(const MenuInput& input,double now) {
    // The arming rule of OptionsMenu: a button held when the screen appears
    // (the dialog's Enter, the accept that came back here) does nothing.
    if (!input.focused) { armed_=false;previous_=input;return StartAction::none; }
    if (!armed_) { if (!input.any()) armed_=true;previous_=input;return StartAction::none; }
    const bool cancel=input.cancel && !previous_.cancel;
    const bool accept=input.accept && !previous_.accept;
    const bool fresh=(input.up && !previous_.up) || (input.down && !previous_.down);
    const bool move=input.up!=input.down && (fresh || now>=next_repeat_);
    previous_=input;
    if (cancel) return StartAction::quit;
    if (move) {
        next_repeat_=now+(fresh ? .35 : .12);
        const auto count=static_cast<unsigned>(entries_.size());
        selected_=(selected_+(input.down ? 1 : count-1))%count;
    }
    return accept ? entries_[selected_] : StartAction::none;
}
void StartScreen::render(std::span<std::uint32_t> pixels,const RomFont& font) const {
    if (pixels.size()!=256*192) throw std::invalid_argument("Expected a 256 by 192 picture.");
    std::fill(pixels.begin(),pixels.end(),0xff000000);
    // Entries every 16 lines between a blank tile row above and below, text
    // two tiles in from the left border as in the Options window.
    const int count=static_cast<int>(entries_.size()),width=224,height=16*count+24;
    const int x=(256-width)/2,y=(192-height)/16*8;
    font.window(pixels,x,y,width,height,y+16+16*static_cast<int>(selected_));
    for (int i=0;i<count;++i) font.text(pixels,x+16,y+16+16*i,label(entries_[i]),0xffffffff);
    const auto version=footer();
    font.text(pixels,(256-8*static_cast<int>(version.size()))/2,176,version,0xffaaaaaa);
}
const char* StartScreen::label(StartAction action) {
    switch (action) {
    case StartAction::play_us: return "PLAY PHANTASY STAR (US)";
    case StartAction::play_br: return "PLAY PHANTASY STAR (BR)";
    case StartAction::choose: return "CHOOSE ANOTHER ROM";
    case StartAction::open_saves: return "OPEN SAVES FOLDER";
    case StartAction::quit: return "QUIT";
    case StartAction::none: break;
    }
    return "";
}
std::string StartScreen::footer() { return std::string("PSMODERN ")+PSMODERN_VERSION; }
std::optional<RomList> prepare_launcher(LauncherPlatform& platform,RomList* unreadable_out) {
    auto [list,warning]=platform.load_list();
    if (warning) platform.show_message(*warning);
    std::string forgotten,unreadable_text;
    RomList unreadable;
    for (const auto version:{RomVersion::us_rev_a,RomVersion::brazilian}) {
        const auto path=list.get(version);
        if (!path) continue;
        std::string reason;
        try { if (platform.identify(*path)!=version) reason="it is now the other version of the game."; }
        catch (const RomUnreadable& e) {
            unreadable_text+="\n\n"+utf8(*path)+"\n"+e.what();
            unreadable.set(version,*path);list.clear(version);
            continue;
        }
        catch (const std::exception& e) { reason=e.what(); }
        if (!reason.empty()) { forgotten+="\n\n"+utf8(*path)+"\n"+reason; list.clear(version); }
    }
    std::string message;
    if (!forgotten.empty()) message="PSModern forgot these ROMs because they are missing or have changed:"+forgotten;
    if (!unreadable_text.empty())
        message+=(message.empty() ? "" : "\n\n")+std::string("These ROMs could not be read now; still remembered:")+unreadable_text;
    if (!message.empty()) platform.show_message(message);
    if (!forgotten.empty()) remember(platform,list,unreadable);
    if (list.empty() && !choose_rom(platform,list,true,unreadable)) return {};
    if (unreadable_out) *unreadable_out=unreadable;
    return list;
}
std::optional<RomVersion> choose_rom(LauncherPlatform& platform,RomList& list,bool first_run,const RomList& unreadable) {
    for (;;) {
        const auto path=platform.pick_rom();
        if (!path) return {};
        RomVersion version{};
        try {
            version=platform.identify(*path);
            list.set(version,std::filesystem::absolute(*path));
        } catch (const std::exception& e) {
            if (platform.ask_retry(utf8(path->filename())+" cannot be used: "+e.what()+
                "\n\nPSModern needs your own uncompressed .sms dump of Phantasy Star, either:"
                "\n- the US Rev-A cartridge\n- the Brazilian Tec Toy cartridge",first_run ? "Quit" : "Back")) continue;
            return {};
        }
        remember(platform,list,unreadable);
        return version;
    }
}
const std::filesystem::path& font_rom(const RomList& list) {
    if (list.empty()) throw std::logic_error("The start screen needs a remembered ROM.");
    return list.us ? *list.us : *list.br;
}
}
