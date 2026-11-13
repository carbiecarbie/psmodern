// SPDX-License-Identifier: GPL-3.0-or-later
// Exercises the actual main.cpp child-screen flow with a dummy SDL host.
#include "app/sdl_host.h"
#include "app/frame_driver.h"
#include "app/language_screen.h"
#include "app/options_menu.h"
#include "app/shared_save.h"
#include "app/localization.h"
#include "app/start_loop.h"
#include "app/windows_platform.h"
#include "replay/recording.h"
#include "temporary_cleanup.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace psmodern;
using namespace psmodern::app;
void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
enum class Hook { none,block_write,retry_write };
struct Command {MenuInput input;Hook hook=Hook::none;};
FrameDriver* tested=nullptr;
SDL_Window* focused=nullptr;
std::filesystem::path recording_path,host_folder;
std::shared_ptr<const Memory::Rom> ui_image;
Language source_language=Language::english,target_language=Language::portuguese;
unsigned mode=0,loops=0,step=0,child_frames=0,parent_frames=0;
bool opened=false,quit_sent=false,child_saved=false,failed_shown=false,finished=false;
std::vector<Command> commands;
LegacySaves prompt_saves{};
Language prompt_language=Language::english;
int prompt_response=1;
unsigned prompts=0;
const std::string legacy_raw(8192,'\0'),us_backup(8192,'U'),br_backup(8192,'B');
std::filesystem::path legacy_file(RomVersion version) {return host_folder/save_name(version);}
void write_bytes(const std::filesystem::path& path,const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(file),"write legacy fixture");
}
std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);check(bool(file),"read saved fixture");
    return {std::istreambuf_iterator<char>(file),{}};
}
int scripted_message_box(const SDL_MessageBoxData* box,int* selected) {
    ++prompts;
    check(box && selected,"migration prompt arguments");
    const bool unavailable=prompt_saves[0].state==LegacySaveState::unavailable || prompt_saves[1].state==LegacySaveState::unavailable;
    check(std::string_view(box->title)==ui_text(UiText::legacy_title,prompt_language),"migration title is localized");
    check(std::string_view(box->message)==ui_text(unavailable ? UiText::legacy_unavailable : UiText::legacy_choice,prompt_language),
          "migration explanation is localized and reflects unavailable sources");
    check(std::string_view(box->title)==(prompt_language==Language::english ? "Old saved games" : "Partidas antigas"),
          "migration title uses the requested language");
    int index=0;
    for(unsigned i=0;i<2;++i)if(prompt_saves[i].state==LegacySaveState::available) {
        check(index<box->numbuttons,"missing available source button");
        const auto& button=box->buttons[index++];
        check(button.buttonid==static_cast<int>(i),"source button chooses the correct old save");
        check(std::string_view(button.text)==ui_text(i ? UiText::legacy_br : UiText::legacy_us,prompt_language),"source button localized");
        const auto expected=prompt_language==Language::english ? (i ? "BRAZILIAN" : "AMERICAN") :
            (i ? "BRASILEIRA" : "AMERICANA");
        check(std::string_view(button.text)==expected,"source button uses requested language");
    }
    check(index+1==box->numbuttons,"unavailable source must not have a button");
    const auto& cancel=box->buttons[index];
    check(cancel.buttonid==2 && (cancel.flags&SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT) &&
          std::string_view(cancel.text)==ui_text(UiText::cancel,prompt_language),"localized Cancel is the escape action");
    *selected=prompt_response;return 0;
}
class SeededDriver : public FrameDriver {
public:
    SeededDriver(std::shared_ptr<const Memory::Rom> image,SaveStore store,bool routines,game::OptionOverrides overrides,bool original)
        :FrameDriver(std::move(image),std::move(store),routines,overrides,original) {
        if(mode==0) {
            check(prompts==1,"migration chooser was not shown exactly once");
            check(read_bytes(host_folder/"shared"/(std::string(shared_save_name)+".bak"))==br_backup,
                  "BR backup was not selected before the game started");
        }
        const auto recording=replay::read(recording_path);
        for(unsigned n=0;n<3400;++n) {const auto result=run(recording.input_at(n));check(!result.save_error,"seed save failed");}
        tested=this;
    }
};
SDL_Window* scripted_focus() {return focused;}
FrameInput scripted_input(SDL_Window*,const Controller&,const EnterKey&) {return {};}
int scripted_poll(SDL_Event* event) {
    if(step==commands.size() && opened && tested && !quit_sent) {
        if(mode==3) {
            // Quit while the native parent still owns its window/stack. The
            // preference is durable; no live ROM installation may occur.
            check(child_saved && tested->menu_active() && tested->language_pending(),"pending-exit fixture did not retain saved choice inside Options");
            check(tested->active_language()==source_language && tested->language_changes()==0,"exit installed language before native return");
            check(tested->store().path()==host_folder/"shared"/shared_save_name,"pending exit changed native save ownership");
        } else {
            if(tested->menu_active())return 0;
            check(child_saved && tested->active_language()==target_language && !tested->language_pending(),"parent close lost child Save");
            check(tested->language_changes()==1,"child Save applied more than once");
        }
        check(tested->chosen_options()==(mode==1 ? game::Options{0,100} : mode==3 ? game::Options{} : game::Options{200,100}),"parent exit saved the wrong rate draft");
        finished=quit_sent=true;*event={};event->type=SDL_QUIT;return 1;
    }
    return 0;
}
MenuInput scripted_menu(SDL_Window* window,const Controller&) {
    focused=window;
    check(++loops<4000,"language UI script timed out");
    if(!tested || !tested->menu_active()) {
        // Retry fresh toggles until the emulated field wait admits the hold.
        return !opened && loops%4==2 ? MenuInput{.toggle=true} : MenuInput{};
    }
    opened=true;
    if(step>=commands.size())return {};
    const auto command=commands[step++];
    if(command.hook==Hook::block_write)std::filesystem::create_directory(host_folder/language_file_name);
    if(command.hook==Hook::retry_write) {
        check(failed_shown && tested->active_language()==source_language && !tested->language_pending(),"failed child write was committed");
        check(std::filesystem::remove(host_folder/language_file_name),"remove failed-save fixture");
    }
    return command.input;
}
int inspected_upload(const FrameDriver& driver,const LanguageScreen& child,const OptionsMenu& parent,Language ui,
                     SDL_Texture* texture,const SDL_Rect* rect,const void* raw,int pitch) {
    if(child.visible()) {
        ++child_frames;check(driver.menu_active(),"Language child escaped native Options hold");
        if(mode==2 && std::filesystem::is_directory(host_folder/language_file_name)) {
            // Inspect the actual failure line, rather than a test callback.
            const auto pixels=static_cast<const std::uint32_t*>(raw);
            RomFont font(*ui_image);
            std::vector<std::uint32_t> expected(256*192,0);
            font.text(expected,32,160,ui_text(UiText::save_failed,ui),0xffffffff);
            bool matches=true;
            for(unsigned i=0;i<expected.size();++i)if(expected[i] && pixels[i]!=expected[i])matches=false;
            if(matches)failed_shown=true;
        }
    } else if(parent.visible()) {
        ++parent_frames;
        if(std::filesystem::is_regular_file(host_folder/language_file_name) && load_language(host_folder).preference!=LanguagePreference::native) {
            check(driver.language_pending() && driver.active_language()==source_language,"child Save did not wait for native parent close");
            check(ui==target_language,"parent UI did not localize after independent child Save");child_saved=true;
        }
    }
    return SDL_UpdateTexture(texture,rect,raw,pitch);
}
void tap(MenuInput input,Hook hook=Hook::none) {commands.push_back({input,hook});commands.push_back({{}});}
void script() {
    commands.clear();commands.push_back({{}}); // arm parent
    tap({.right=true});                      // rates draft 100 -> 200
    tap({.up=true});tap({.accept=true});     // Language child
    commands.push_back({{}});               // arm child
    const unsigned target_row=target_language==Language::english ? 1 : 2;
    for(unsigned n=0;n<target_row;++n)tap({.down=true});
    tap({.accept=true});                    // choose target, no persistence yet
    for(unsigned n=target_row;n<3;++n)tap({.down=true});
    if(mode==2) {
        tap({.accept=true},Hook::block_write);
        commands.push_back({{},Hook::retry_write});
    }
    tap({.accept=true});                    // explicit child Save
    commands.push_back({{}});               // re-arm parent on Language
    if(mode==1) {
        tap({.down=true});tap({.right=true});tap({.accept=true}); // parent Apply 200 -> 0
    } else if(mode!=3)tap({.cancel=true});   // saves parent rates; mode 3 quits with the parent held
}
}

// Use production play(), screen dispatch, persistence and error paths. Replace
// only physical host input and the ROM's initial position, as options_session.
#define utf8_arguments(argc,argv) std::vector<std::string>(argv,argv+argc)
#define attach_parent_console()
#define run_launcher(...) std::optional<psmodern::app::SessionRoms>()
#define main language_scripted_app_main
#define FrameDriver SeededDriver
#define SDL_PollEvent scripted_poll
#define SDL_GetKeyboardFocus scripted_focus
#define SDL_ShowMessageBox scripted_message_box
#define sample_input scripted_input
#define sample_menu_input scripted_menu
#define SDL_UpdateTexture(t,r,p,pitch) inspected_upload(driver,language_screen,menu,language,t,r,p,pitch)
#include "../src/app/main.cpp"
#undef main
#undef utf8_arguments
#undef attach_parent_console
#undef run_launcher
#undef FrameDriver
#undef SDL_PollEvent
#undef SDL_GetKeyboardFocus
#undef SDL_ShowMessageBox
#undef sample_input
#undef sample_menu_input
#undef SDL_UpdateTexture

void prompt_fixtures() {
    for(const auto language:{Language::english,Language::portuguese}) {
        prompt_language=language;
        prompt_saves={};prompt_saves[0].state=prompt_saves[1].state=LegacySaveState::available;
        prompt_response=0;
        check(choose_legacy_save(prompt_saves,language,nullptr)==RomVersion::us_rev_a,"US button selects US");
        prompt_response=1;
        check(choose_legacy_save(prompt_saves,language,nullptr)==RomVersion::brazilian,"BR button selects BR");
        prompt_response=2;
        check(!choose_legacy_save(prompt_saves,language,nullptr),"Cancel declines migration");
        for(unsigned unavailable=0;unavailable<2;++unavailable) {
            prompt_saves={};prompt_saves[unavailable].state=LegacySaveState::unavailable;
            prompt_saves[1-unavailable].state=LegacySaveState::available;
            prompt_response=static_cast<int>(1-unavailable);
            check(choose_legacy_save(prompt_saves,language,nullptr)==(unavailable ? RomVersion::us_rev_a : RomVersion::brazilian),
                  "available source remains an explicit choice");
            prompt_response=2;
            check(!choose_legacy_save(prompt_saves,language,nullptr),"unavailable source prompt can be cancelled");
        }
    }
}

int main(int argc,char** argv) {try {
    if(argc!=5)return 1;
    for(unsigned i=1;i<=2;++i)if(!*argv[i] || !std::filesystem::is_regular_file(argv[i]))return 77;
    const auto us=load_rom(argv[1]),br=load_rom(argv[2]);
    const bool brazil=std::string_view(argv[4])=="br";
    check(brazil || std::string_view(argv[4])=="us","expected us or br");
    SessionRoms session;session.source_version=brazil ? RomVersion::brazilian : RomVersion::us_rev_a;
    session.images={us.rom,br.rom};session.active=source_language=native_language(session.source_version);
    target_language=source_language==Language::english ? Language::portuguese : Language::english;
    ui_image=session.image(session.active);
    recording_path=std::filesystem::path(argv[3])/(brazil ? "br" : "us")/"first-battle.psrec";
    const auto root=std::filesystem::temp_directory_path()/("psmodern-language-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    prompt_fixtures();
    for(mode=0;mode<4;++mode) {
        const auto folder=root/std::to_string(mode);std::filesystem::create_directories(folder);
        SDL_setenv("APPDATA",folder.string().c_str(),1);host_folder=folder/"PSModern";
        tested=nullptr;focused=nullptr;loops=step=child_frames=parent_frames=prompts=0;
        opened=quit_sent=child_saved=failed_shown=finished=false;script();
        if(mode==0) {
            prompt_language=source_language;prompt_response=1;prompt_saves={};
            prompt_saves[0].state=prompt_saves[1].state=LegacySaveState::available;
            for(auto version:{RomVersion::us_rev_a,RomVersion::brazilian}) {
                const auto path=legacy_file(version);
                write_bytes(path,legacy_raw);
                write_bytes(path.string()+".bak",version==RomVersion::us_rev_a ? us_backup : br_backup);
            }
        }
        check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)==0,"start dummy SDL host");
        const auto result=play(session,nullptr,{},true,{});
        check(result==0 && finished && child_frames>0 && parent_frames>0,"actual Language child flow did not finish");
        check(load_language(host_folder).preference==(target_language==Language::english ? LanguagePreference::english : LanguagePreference::portuguese),"global child preference did not persist");
        check(mode!=2 || failed_shown,"failed Save was not visibly reported before retry");
        {
            auto saved=SaveStore::shared(host_folder);
            check(saved.load_preferences(saved.load())==(mode==1 ? game::Options{0,100} : mode==3 ? game::Options{} : game::Options{200,100}),"parent exit persisted the wrong rates");
        }
        if(mode==0) {
            check(prompts==1 && std::filesystem::is_directory(host_folder/"shared"),"migration chooser repeated or shared store missing");
            for(auto version:{RomVersion::us_rev_a,RomVersion::brazilian}) {
                const auto path=legacy_file(version);
                check(read_bytes(path)==legacy_raw && read_bytes(path.string()+".bak")==
                      (version==RomVersion::us_rev_a ? us_backup : br_backup),"legacy saves or backups changed after play");
            }
            auto reopened=open_shared_save(host_folder,[](const auto&)->std::optional<RomVersion> {
                throw std::runtime_error("shared store asked for migration again");
            });
            check(reopened && reopened->path()==host_folder/"shared"/shared_save_name && prompts==1,
                  "reopening shared storage should not prompt");
        } else {
            const auto foreign=host_folder/save_name(language_rom(target_language));
            check(!std::filesystem::exists(foreign) && !std::filesystem::exists(foreign.string()+".options") && !std::filesystem::exists(foreign.string()+".automap"),"child Save created the other profile");
            check(prompts==0,"empty startup unexpectedly requested migration choice");
        }
        if(mode==3) {
            RomList remembered;remembered.set(RomVersion::us_rev_a,argv[1]);remembered.set(RomVersion::brazilian,argv[2]);
            const auto restarted=load_session_roms(brazil ? argv[2] : argv[1],remembered,load_language(host_folder).preference);
            check(restarted.source_version==session.source_version && restarted.active==target_language,"restart did not resolve durable preference for the original profile");
            FrameDriver fresh(restarted.image(restarted.active),SaveStore::shared(host_folder));fresh.configure_languages(restarted);
            const Machine power_on(restarted.image(restarted.active),fresh.store().load());
            check(fresh.active_language()==target_language && !fresh.language_pending() && fresh.language_changes()==0 && !fresh.menu_active(),"restart replayed an unsafe pending transition");
            check(fresh.machine().equivalent(power_on),"restart retained the closed session's native window or stack");
            check(fresh.store().path()==host_folder/"shared"/shared_save_name,"restart moved progress to the presentation-language profile");
        }
        SDL_Quit();
    }
    test::remove_temporary_folder(root);
    std::cout<<"Actual main UI: child Save with parent save on exit, failed Save retry, pending exit with clean restart and fixed profile ownership passed.\n";return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}}
