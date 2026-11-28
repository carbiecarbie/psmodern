// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the actual main.cpp Options flow of the Sound row (milestone 17)
// with a dummy SDL host: the switch heard at once (samples queued to SDL) and
// saved at once, a failed save and its retry, a session without the Japanese
// ROM, one whose FM engine was refused, FM stopping while Options is open,
// and the row's labels following a language change.
#include "oracle/native_machine.h"
#include "app/sdl_host.h"
#include "app/frame_driver.h"
#include "app/options_menu.h"
#include "app/shared_save.h"
#include "app/localization.h"
#include "app/sound_preference.h"
#include "app/start_loop.h"
#include "app/platform.h"
#include "replay/recording.h"
#include "temporary_cleanup.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
using namespace psmodern;
using namespace psmodern::app;
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
// Mode 0: the Japanese ROM, switches and a language change. Mode 1: the first
// save fails and is retried. Mode 2: no Japanese ROM, FM saved before. Mode 3:
// a sound image the engine refuses, FM saved before. Mode 4: FM chosen, then
// its engine stops inside Options; a power cycle brings it back.
constexpr unsigned modes=5;
enum class Hook { none,expect_fm,expect_psg,retry_write,expect_unchanged,inject_failure,expect_failure };
struct Command {MenuInput input;Hook hook=Hook::none;};
FrameDriver* tested=nullptr;
SDL_Window* focused=nullptr;
std::filesystem::path recording_path,host_folder;
std::shared_ptr<const Memory::Rom> ui_image;
std::shared_ptr<Memory::Rom> damaged; // mode 4: test-owned backing of the Japanese image
// The port operand of the FM register-select output in the Japanese driver's
// write helper (JP:0C:8272), reached whenever the music writes the chip.
constexpr std::size_t damaged_port=12*0x4000+0x273;
std::uint8_t damaged_good=0;
unsigned mode=0,loops=0,step=0,sound_frames=0,parent_frames=0;
bool opened=false,panel_ready=false,quit_sent=false,finished=false,failed_shown=false,explained=false,fallback_checked=false;
std::array<bool,2> label_seen{};
std::vector<Command> commands;
Audio::Counters queued;                  // the host's audio counters at the last upload
std::optional<Audio::Counters> baseline; // at the first visit of a waiting hook
std::uint64_t psg_samples(const Audio::Counters& c) {return c.submitted-c.fm_submitted;}
std::filesystem::path sound_file() {return host_folder/sound_file_name;}
std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);check(bool(file),"read the sound preference file");
    return {std::istreambuf_iterator<char>(file),{}};
}
void write_text(const std::filesystem::path& path,const std::string& text) {
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);
    file<<text;check(bool(file),"write the sound preference fixture");
}
bool fm_saved() {return std::filesystem::is_regular_file(sound_file()) && read_text(sound_file())==format_sound_preference(SoundPreference::fm);}
bool psg_saved() {return std::filesystem::is_regular_file(sound_file()) && read_text(sound_file())==format_sound_preference(SoundPreference::psg);}
class SeededDriver : public FrameDriver {
public:
    SeededDriver(std::shared_ptr<const Memory::Rom> image,SaveStore store,game::OptionOverrides overrides,bool original,
                 std::shared_ptr<const Memory::Rom> sound_rom)
        :FrameDriver(std::move(image),std::move(store),overrides,original,std::move(sound_rom)) {
        const auto recording=replay::read(recording_path);
        for(unsigned n=0;n<3400;++n) {const auto result=run(recording.input_at(n));check(!result.save_error,"seed save failed");}
        tested=this;
    }
};
SDL_Window* scripted_focus() {return focused;}
FrameInput scripted_input(SDL_Window*,const Controller&,const EnterKey&) {return {};}
int scripted_poll(SDL_Event* event) {
    if(step==commands.size() && opened && tested && !quit_sent) {
        if(tested->menu_active())return 0; // the native Options close first
        // After Options: what is heard and what is saved.
        if(mode<2) check(tested->fm_selected() && fm_saved(),"the Sound row's last choice is not heard and saved after Options");
        else check(!tested->fm_selected() && fm_saved(),"FM played without its engine, or the saved FM was lost");
        if(mode==0) check(load_language(host_folder).preference==LanguagePreference::portuguese,"the language change was not saved");
        if(mode==4) {
            // A power cycle with the image repaired rebuilds the engine with the kept FM choice.
            damaged->at(damaged_port)=damaged_good;
            check(!tested->power_cycle() && tested->fm_selected() && fm_saved(),"a power cycle did not restore the kept FM choice");
        }
        finished=quit_sent=true;*event={};event->type=SDL_QUIT;return 1;
    }
    return 0;
}
// Waiting hooks hold the script until the host has queued audio from the
// source expected since their first visit: FM samples that are not silent, or
// PSG samples, and none from the other source.
bool heard(bool fm) {
    if(!baseline)baseline=queued;
    const bool fm_more=queued.fm_submitted>baseline->fm_submitted,psg_more=psg_samples(queued)>psg_samples(*baseline);
    if(fm) {
        check(!psg_more,"PSG samples were queued while FM was selected");
        return queued.fm_nonzero>baseline->fm_nonzero;
    }
    check(!fm_more,"FM samples were queued while PSG was heard");
    return psg_more;
}
MenuInput scripted_menu(SDL_Window* window,const Controller&) {
    focused=window;
    check(++loops<6000,"Sound UI script timed out");
    if(!tested || !tested->menu_active()) {
        // Retry fresh toggles until the emulated field wait admits the hold.
        return !opened && loops%4==2 ? MenuInput{.toggle=true} : MenuInput{};
    }
    opened=true;
    if (!panel_ready) return {};
    if(step>=commands.size())return {};
    const auto command=commands[step];
    switch(command.hook) {
    case Hook::none:break;
    case Hook::expect_fm:
        check(tested->menu_active() && tested->fm_selected() && fm_saved(),"FM was not selected and saved at once inside Options");
        if(!heard(true))return {};
        break;
    case Hook::expect_psg:
        check(tested->menu_active() && !tested->fm_selected() && psg_saved(),"PSG was not selected and saved at once inside Options");
        if(!heard(false))return {};
        break;
    case Hook::retry_write:
        check(!tested->fm_selected() && std::filesystem::is_directory(sound_file()),"a failed sound save was committed");
        if(!heard(false))return {}; // the previous source until the retry
        check(failed_shown,"the failed save was not shown");
        check(std::filesystem::remove(sound_file()),"remove the failed-save fixture");break;
    case Hook::expect_unchanged:
        check(!tested->fm_selected() && fm_saved(),"the Sound row changed what it cannot offer");
        if(!heard(false))return {};
        break;
    case Hook::inject_failure:
        damaged_good=damaged->at(damaged_port);damaged->at(damaged_port)=0xf2;break;
    case Hook::expect_failure:
        if(tested->fm_available())return {}; // until the engine reaches the damaged operand
        check(!tested->fm_selected() && fm_saved(),"the engine's failure changed the saved choice");
        if(!fallback_checked)return {};      // until a frame is drawn after the failure
        break;
    }
    ++step;baseline.reset();
    return command.input;
}
// Every lit pixel of the text drawn alone is in the uploaded frame, in that colour.
bool drawn(const std::uint32_t* pixels,const RomFont& font,int column,int row,std::string_view text,std::uint32_t color) {
    std::vector<std::uint32_t> expected(256*192,0);
    menu_style::text(expected,font,column,row,text,color);
    for(unsigned i=0;i<expected.size();++i)if(expected[i] && pixels[i]!=expected[i])return false;
    return true;
}
int inspected_upload(const FrameDriver& driver,const OptionsMenu& parent,Language ui,const Audio& audio,
                     SDL_Texture* texture,const SDL_Rect* rect,const void* raw,int pitch) {
    namespace l=options_layout;
    queued=audio.counters();
    if(parent.visible()) {
        ++parent_frames;check(driver.menu_active(),"Options escaped the native Options hold");
        const auto pixels=static_cast<const std::uint32_t*>(raw);
        RomFont font(*ui_image);
        // Start this audio/label test once the initial Options explanation
        // has reached the state row. The opening frames are covered separately.
        if (!panel_ready)
            panel_ready=drawn(pixels,font,l::explanation.column+1,l::state_row,parent.state(ui),
                              parent.state_quiet() ? menu_style::grey : menu_style::white);
        if(parent.selected()==l::sound_row) {
            ++sound_frames;
            // Inspect the actual frame rather than a test callback.
            if(drawn(pixels,font,l::list.column+1,l::line_row(l::sound_row-parent.first_visible()),ui_text(UiText::sound,ui),menu_style::white))
                label_seen[ui==Language::english ? 0 : 1]=true;
            // The value shown is the sound heard, on every frame.
            const std::string_view value=driver.fm_selected() ? "FM" : "PSG";
            check(drawn(pixels,font,l::list.column+l::list.width-1-int(value.size()),l::line_row(l::sound_row-parent.first_visible()),value,menu_style::grey),
                "the Sound row does not show the sound heard");
            const auto third=mode==2 ? ui_text(UiText::fm_needs_rom,ui) : mode==3 ? ui_text(UiText::fm_unavailable,ui) :
                OptionsMenu::explanation(l::sound_row,ui)[2];
            if(drawn(pixels,font,l::explanation.column+1,l::explanation.row+1+2*l::line_step,third,menu_style::white))explained=true;
            if(drawn(pixels,font,l::explanation.column+1,l::state_row,ui_text(UiText::save_failed,ui),menu_style::white))failed_shown=true;
            if(mode==4 && !driver.fm_available() && !fallback_checked) {
                // The first frame drawn after FM stopped: PSG chosen, FM dark, why on line 3.
                const auto window=parent.values_window(ui);
                check(window && drawn(pixels,font,window->column+window->width-4,window->row+1,"PSG",menu_style::yellow) &&
                    drawn(pixels,font,window->column+window->width-3,window->row+1+l::line_step,"FM",menu_style::dim),
                    "the values window still offers FM after it stopped");
                check(drawn(pixels,font,l::explanation.column+1,l::explanation.row+1+2*l::line_step,ui_text(UiText::fm_unavailable,ui),menu_style::white),
                    "the explanation does not say FM stopped");
                fallback_checked=true;
            }
        }
    }
    return SDL_UpdateTexture(texture,rect,raw,pitch);
}
void tap(MenuInput input) {commands.push_back({input});commands.push_back({{}});}
void expect(Hook hook) {commands.push_back({{},hook});}
void script() {
    commands.clear();commands.push_back({{}}); // arm Options
    tap({.up=true});                           // the Sound row
    if(mode==0) {
        tap({.right=true});expect(Hook::expect_fm);    // PSG to FM, heard and saved at once
        tap({.left=true});expect(Hook::expect_psg);    // and back
        tap({.right=true});expect(Hook::expect_fm);
        tap({.up=true});tap({.left=true});             // Language: AUTO to PORTUGUES, labels at once
        tap({.down=true});                             // Sound again, in Portuguese
    } else if(mode==1) {
        tap({.right=true});expect(Hook::retry_write);  // the save fails; PSG is still heard
        tap({.right=true});expect(Hook::expect_fm);    // the retry saves and switches
    } else if(mode==4) {
        tap({.right=true});expect(Hook::expect_fm);
        expect(Hook::inject_failure);expect(Hook::expect_failure);
        tap({.left=true});tap({.right=true});expect(Hook::expect_unchanged); // neither direction overwrites the kept FM
    } else {
        tap({.right=true});tap({.left=true});expect(Hook::expect_unchanged);
    }
    tap({.cancel=true});
}
std::shared_ptr<Memory::Rom> read_image(const char* path) {
    std::ifstream file(path,std::ios::binary);check(bool(file),"read the Japanese ROM");
    return std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}
}

// Use production play(), screen dispatch, persistence and error paths. Replace
// only physical host input and the ROM's initial position, as options_session.
#define utf8_arguments(argc,argv) std::vector<std::string>(argv,argv+argc)
#define attach_parent_console()
#define run_launcher(...) std::optional<psmodern::app::SessionRoms>()
#define main sound_scripted_app_main
#define FrameDriver SeededDriver
#define SDL_PollEvent scripted_poll
#define SDL_GetKeyboardFocus scripted_focus
#define sample_input scripted_input
#define sample_menu_input scripted_menu
#define SDL_UpdateTexture(t,r,p,pitch) inspected_upload(driver,menu,language,audio,t,r,p,pitch)
#include "../src/app/main.cpp"
#undef main
#undef utf8_arguments
#undef attach_parent_console
#undef run_launcher
#undef FrameDriver
#undef SDL_PollEvent
#undef SDL_GetKeyboardFocus
#undef sample_input
#undef sample_menu_input
#undef SDL_UpdateTexture

int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
    if(argc!=5)return 1;
    for(int i=1;i<=3;++i)if(!*argv[i] || !std::filesystem::is_regular_file(argv[i]))return 77;
    const auto us=load_rom(argv[1]),br=load_rom(argv[2]);
    const std::shared_ptr<const Memory::Rom> japanese=read_image(argv[3]);
    recording_path=std::filesystem::path(argv[4])/"us"/"first-battle.psrec";
    ui_image=us.rom;
    const auto root=std::filesystem::temp_directory_path()/("psmodern-sound-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    for(mode=0;mode<modes;++mode) {
        const auto folder=root/std::to_string(mode);std::filesystem::create_directories(folder);
        SDL_setenv("APPDATA",folder.string().c_str(),1);host_folder=folder/"PSModern";
        tested=nullptr;focused=nullptr;loops=step=sound_frames=parent_frames=0;label_seen={};queued={};baseline.reset();
        opened=panel_ready=quit_sent=finished=failed_shown=explained=fallback_checked=false;script();
        SessionRoms session;session.source_version=RomVersion::us_rev_a;
        session.images={us.rom,br.rom};session.active=Language::english;
        if(mode==4)damaged=read_image(argv[3]);
        session.sound_rom=mode<2 ? japanese : mode==3 ? std::shared_ptr<const Memory::Rom>(us.rom) :
            mode==4 ? std::shared_ptr<const Memory::Rom>(damaged) : nullptr;
        if(mode==1)std::filesystem::create_directories(sound_file()); // the first save cannot replace a folder
        if(mode==2 || mode==3)write_text(sound_file(),format_sound_preference(SoundPreference::fm));
        check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)==0,"start dummy SDL host");
        const auto result=play(session,nullptr,{},{});
        check(result==0 && finished && sound_frames>0 && parent_frames>0,"actual Sound row flow did not finish");
        check(label_seen[0] && explained,"the Sound row and its explanation were not shown");
        check(mode!=0 || label_seen[1],"the Sound row did not follow the language change");
        check(mode!=1 || failed_shown,"the failed save was not shown before the retry");
        check(mode!=4 || fallback_checked,"no frame was drawn after FM stopped");
        SDL_Quit();
    }
    test::remove_temporary_folder(root);
    std::cout<<"Actual main UI: the Sound row heard (queued samples) and saved at once, failed save retry, no Japanese ROM, "
               "refused engine, FM stopping inside Options with power-cycle recovery, and language change passed.\n";return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}}
