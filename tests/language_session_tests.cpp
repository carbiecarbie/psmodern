// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "game/routines.h"
#include "game/text.h"
#include "replay/recording.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool value,const std::string& reason) {if(!value)throw std::runtime_error(reason);}
template<class F> void refuses(F function,const char* reason) {
    bool refused=false;try {function();} catch(const std::exception&) {refused=true;}
    check(refused,reason);
}
struct Temporary {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("psmodern-language-session-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() {std::filesystem::create_directories(path);}
    ~Temporary() {test::remove_temporary_folder(path);}
};
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
LanguagePreference preference(Language language) {
    return language==Language::english ? LanguagePreference::english : LanguagePreference::portuguese;
}
std::size_t resets(const std::vector<MapEvent>& events) {
    return std::count_if(events.begin(),events.end(),[](const auto& event){return event.kind==MapEvent::Kind::reset;});
}
std::uint64_t cpp_calls(const RoutineRegistry& registry) {
    std::uint64_t result=0;for(const auto& [entry,routine]:registry.entries())result+=routine.counters.cpp_calls;
    return result;
}
struct Pair {
    FrameDriver changed,control;
    const SessionRoms& session;
    std::array<Machine::RomReplacement,2> replacement;
    std::vector<MapEvent> changed_map,control_map;
    unsigned frames=0;
    Pair(const SessionRoms& images,const std::filesystem::path& root)
        :changed(images.image(images.active),SaveStore::shared(root/"changed")),
         control(images.image(images.active),SaveStore::shared(root/"control")),session(images),
         replacement{Machine::prepare_rom_replacement(images.images[0]),Machine::prepare_rom_replacement(images.images[1])} {
        changed.configure_languages(session);control.configure_languages(session);
        changed.log_map_events(&changed_map);control.log_map_events(&control_map);
    }
    void run(const FrameInput& input={}) {
        const auto a=changed.run(input),b=control.run(input);++frames;
        check(!a.save_error && !b.save_error,"paired save failure");
        check(!a.audio_error && !b.audio_error,"language transition reported an audio failure");
        check(a.writes==b.writes && a.samples==b.samples && a.playback==b.playback,
              "language transition reset or changed PSG / normal-tempo audio at frame "+std::to_string(frames));
        // The independent control changes only its ROM reference once the
        // observed return frame has finished. Later native menu redraws must
        // read the same language; retaining its old image is a different run.
        control.machine().replace_rom(replacement[changed.active_language()==Language::english ? 0 : 1]);
        Machine expected(control.machine());
        const auto differences=expected.differences(changed.machine());
        check(differences.empty(),"language transition changed live state beyond ROM at frame "+std::to_string(frames)+
              (differences.empty() ? "" : ": "+differences.front().field));
        check(changed_map==control_map,"language transition changed exploration events");
        check(*changed.exploration()==*control.exploration(),"language transition lost exploration history");
    }
    void speed(unsigned value) {changed.speed(value);control.speed(value);}
    void open() {
        for(unsigned n=0;n<120;++n) {
            run();const bool a=changed.request_menu(),b=control.request_menu();
            check(a==b,"language session changed Options admission");
            if(a) {run();check(changed.menu_active()==control.menu_active(),"final Options admission differed");if(changed.menu_active())return;}
        }
        check(false,"idle exploration did not admit a completed native Options hold");
    }
    void close() {
        changed.close_menu();control.close_menu();
        for(unsigned n=0;n<100 && changed.menu_active();++n)run();
        check(!changed.menu_active() && !control.menu_active(),"native Options return did not complete");
    }
};
void future_menu(FrameDriver& driver,const SessionRoms& session) {
    Machine target(driver.machine()),source(driver.machine());
    source.replace_rom(Machine::prepare_rom_replacement(session.image(session.active)));
    bool saw_native_wait=false,saw_picture_change=false,saw_language_picture=false;
    const auto initial=target.picture();
    const auto old_calls=cpp_calls(driver.routines());
    for(unsigned n=0;n<300;++n) {
        FrameInput input;
        if(n<8 || (n>=106 && n<114))input.pads[0].two=true;
        if(n>=90 && n<98)input.pads[0].down=true;
        target.run_frame(input);source.run_frame(input);
        const auto expected_writes=target.take_psg_writes();const auto expected_samples=target.take_samples();
        source.take_psg_writes();source.take_samples();
        const auto output=driver.run(input);
        target.memory().clear_cartridge_ram_dirty();source.memory().clear_cartridge_ram_dirty();
        check(!output.save_error && !output.audio_error,"future menu output failure");
        check(driver.machine().equivalent(target) && output.writes==expected_writes && output.samples==expected_samples,
              "future target-language menu differs from native target ROM");
        const auto pc=target.cpu().pc.uint16_value,sp=target.cpu().sp.uint16_value;
        if(pc>=0x55 && pc<=0x5b) {
            const auto ret=static_cast<unsigned>(target.peek(sp)|(target.peek(static_cast<std::uint16_t>(sp+1))<<8));
            saw_native_wait|=ret!=0x0b14 && ret!=0x0f48;
        }
        saw_picture_change|=target.picture()!=initial;saw_language_picture|=target.picture()!=source.picture();
    }
    check(saw_native_wait && saw_picture_change,"future menu did not actually open: native-wait="+std::to_string(saw_native_wait)+
          " picture-change="+std::to_string(saw_picture_change)+" mode="+std::to_string(driver.machine().peek(0xc202)));
    check(saw_language_picture,"opened future native menu never displayed distinct language artwork");
    check(cpp_calls(driver.routines())>old_calls,"future menu never exercised rewritten routines");
    // Some native field-menu labels are cached artwork. Establish the active
    // text source directly with a fresh indexed message, without assuming a
    // particular English/Portuguese sentence or committing any ROM bytes.
    bool distinct_message=false;
    const auto* indexed=driver.routines().find(game::text_indexed_entry);
    check(indexed,"indexed text routine is absent");const auto indexed_calls=indexed->counters.cpp_calls;
    for(unsigned index=0;index<256 && !distinct_message;++index) {
        Machine actual(driver.machine()),native(driver.machine()),old_source(driver.machine());
        old_source.replace_rom(Machine::prepare_rom_replacement(session.image(session.active)));
        for(auto* machine:{&actual,&native,&old_source}) {
            machine->memory().write(0xfffe,1);machine->cpu().pc.uint16_value=game::text_indexed_entry.address;
            machine->cpu().hl.uint16_value=static_cast<std::uint16_t>(index*2);machine->cpu().iff1=machine->cpu().iff2=0;
        }
        actual.step(driver.routines());
        for(auto* machine:{&native,&old_source}) {
            unsigned steps=0;while(machine->cpu().pc.uint16_value!=game::text_stream_unit.address && steps++<50)machine->step();
            check(machine->cpu().pc.uint16_value==game::text_stream_unit.address,"native indexed lookup did not reach text stream");
        }
        check(actual.equivalent(native),"new indexed message did not use the active target ROM natively");
        const auto target_pointer=actual.cpu().hl.uint16_value,source_pointer=old_source.cpu().hl.uint16_value;
        distinct_message=target_pointer!=source_pointer;
        for(unsigned i=0;i<32;++i)distinct_message|=actual.peek(static_cast<std::uint16_t>(target_pointer+i))!=old_source.peek(static_cast<std::uint16_t>(source_pointer+i));
    }
    check(distinct_message && indexed->counters.cpp_calls>indexed_calls,"fresh indexed text never read distinct target-language data");
}
void unavailable_and_restricted(const SessionRoms& session,const Machine& field,const std::filesystem::path& root) {
    auto one=session;one.images[session.active==Language::english ? 1 : 0].reset();
    const auto other=session.active==Language::english ? Language::portuguese : Language::english;
    FrameDriver missing(session.image(session.active),SaveStore::shared(root/"missing"));missing.configure_languages(one);missing.machine()=field;
    bool opened=false;for(unsigned n=0;n<120 && !opened;++n) {missing.run({});opened=missing.request_menu();}
    check(opened,"missing-companion fixture did not reach Options");missing.run({});
    refuses([&]{missing.prepare_language(preference(other));},"unavailable language accepted");missing.commit_language();
    check(!missing.language_pending(),"failed availability validation left a prepared choice");
    for(bool recording:{false,true}) {
        FrameDriver restricted(session.image(session.active),SaveStore::shared(root/(recording ? "recording" : "raw")),recording,{},recording);
        // Both targets are present: refusal must be the session restriction,
        // rather than missing data. Raw-Z80 Options is actually opened first.
        restricted.configure_languages(session);restricted.machine()=field;
        bool active=false;
        for(unsigned n=0;n<120 && !active;++n) {
            restricted.run({});const bool requested=restricted.request_menu();
            if(recording)check(!requested,"recording admitted Options");
            else if(requested) {restricted.run({});active=restricted.menu_active();}
        }
        check(recording || active,"raw-Z80 read-only Options did not open");
        refuses([&]{restricted.prepare_language(preference(other));},"restricted session accepted language");
        restricted.commit_language();check(!restricted.language_pending(),"restricted session queued language");
        check(!std::filesystem::exists(root/(recording ? "recording" : "raw")/language_file_name),"restricted session wrote global preference");
    }
}
void scenario(const SessionRoms& session,const replay::Recording& recording,unsigned at,bool dungeon,const std::string& scene) {
    Temporary folder;
    const auto other=session.active==Language::english ? Language::portuguese : Language::english;
    Memory::CartridgeRam foreign{};foreign[0]=0xa5;foreign[8191]=0x5a;
    const auto foreign_path=folder.path/"changed"/save_name(language_rom(other));atomic_save(foreign_path,foreign);
    const auto foreign_before=read(foreign_path);
    Pair pair(session,folder.path);
    refuses([&]{pair.changed.prepare_language(preference(other));},"startup language change accepted without Options");
    for(unsigned n=0;n<at;++n)pair.run(recording.input_at(n));
    check(pair.changed.machine().peek(0xc202)==(dungeon ? 11 : 9),"recording did not reach expected exploration mode");
    if(scene=="town")unavailable_and_restricted(session,pair.control.machine(),folder.path/"restrictions");
    const auto save_path=pair.changed.store().path(),settings_path=pair.changed.store().settings_path(),map_path=pair.changed.store().automap_path();
    const auto sram=pair.changed.machine().memory().cartridge_ram();
    const auto* tables=pair.changed.dungeon_tables();
    const auto reset_count=resets(pair.changed_map);
    for(unsigned speed:{1u,2u,4u}) {
        pair.speed(speed);for(unsigned n=0;n<8;++n)pair.run();pair.open();
        const auto before=pair.changed.active_language();
        const auto target=before==session.active ? other : session.active;
        const auto explored=*pair.changed.exploration();
        pair.changed.prepare_language(preference(target));
        check(!pair.changed.language_pending() && pair.changed.active_language()==before,"Prepare published an unsaved change");
        if(speed==1) {
            const auto blocked=folder.path/"failed";std::filesystem::create_directories(blocked/language_file_name);
            refuses([&]{save_language(blocked,preference(target));},"language write failure fixture succeeded");
            for(unsigned n=0;n<8;++n)pair.run();
            check(!pair.changed.language_pending() && pair.changed.active_language()==before,"failed language write applied without Commit");
            check(!pair.changed.choose_options({50,400}) && !pair.control.choose_options({50,400}),"parent rate draft could not Apply");
            check(pair.changed.options_pending() && pair.changed.effective_options()==game::Options{},"rates activated inside native hold");
        }
        save_language(folder.path/"global",preference(target));pair.changed.commit_language();
        check(pair.changed.language_pending(),"successful child Save was not queued");
        for(unsigned n=0;n<12;++n)pair.run();
        check(pair.changed.active_language()==before && pair.changed.menu_active(),"language applied before parent native return");
        // A later independent Save wins; selecting the current language cancels
        // a pending change without touching the running machine.
        pair.changed.prepare_language(preference(before));pair.changed.commit_language();
        check(!pair.changed.language_pending(),"saving current language did not replace previous pending choice");
        pair.changed.prepare_language(preference(target));save_language(folder.path/"global",preference(target));pair.changed.commit_language();
        check(*pair.changed.exploration()==explored && pair.changed.dungeon_tables()==tables,"child Save rebuilt map/tracker state");
        pair.close();
        check(pair.changed.active_language()==target && !pair.changed.language_pending(),"post-menu return did not install latest saved language");
        check(pair.changed.language_changes()==(speed==1 ? 1 : speed==2 ? 2 : 3),"transition was duplicated or lost");
        for(unsigned n=0;n<12;++n)pair.run();
        check(pair.changed.effective_options()==game::Options{50,400} && !pair.changed.options_pending(),"parent rates did not activate after return");
        check(pair.changed.store().path()==save_path && pair.changed.store().settings_path()==settings_path && pair.changed.store().automap_path()==map_path,
              "language changed save or companion ownership");
        check(pair.changed.machine().memory().cartridge_ram()==sram && read(foreign_path)==foreign_before,"language wrote SRAM or the other profile");
        check(resets(pair.changed_map)==reset_count,"language reset exploration history");
    }
    future_menu(pair.changed,session);
    const auto active=pair.changed.active_language();const auto ram=pair.changed.machine().memory().cartridge_ram();
    check(!pair.changed.power_cycle(),"power cycle flush failed");
    Machine expected(session.image(active),ram);
    check(pair.changed.machine().equivalent(expected) && pair.changed.active_language()==active,"power cycle reverted active language or lost SRAM");
    check(pair.changed.store().path()==save_path && read(foreign_path)==foreign_before,"power cycle changed profile ownership");
    check(!std::filesystem::exists(foreign_path.string()+".options") && !std::filesystem::exists(foreign_path.string()+".automap"),"language wrote other-profile companions");
    std::cout<<(session.source_version==RomVersion::us_rev_a ? "US " : "BR ")<<scene<<": bidirectional queued switching, exact state/audio, independent saves, future native target text and power cycle passed.\n";
}
}
int main(int argc,char** argv) {try {
    if(argc!=6)return 1;
    for(unsigned i=1;i<=2;++i)if(!*argv[i] || !std::filesystem::is_regular_file(argv[i]))return 77;
    const auto us=load_rom(argv[1]),br=load_rom(argv[2]);
    check(us.version==RomVersion::us_rev_a && br.version==RomVersion::brazilian,"language test ROMs reversed");
    const bool brazil=std::string_view(argv[4])=="br";const std::string scene=argv[5];
    check(brazil || std::string_view(argv[4])=="us","expected source us or br");
    check(scene=="town" || scene=="world" || scene=="dungeon","expected exploration scene");
    SessionRoms session;session.source_version=brazil ? RomVersion::brazilian : RomVersion::us_rev_a;
    session.images={us.rom,br.rom};session.active=native_language(session.source_version);
    const auto root=std::filesystem::path(argv[3])/(brazil ? "br" : "us");
    const auto recording=replay::read(root/(scene=="dungeon" ? "first-dungeon.psrec" : "first-battle.psrec"));
    check(recording.rom_sha1==sha1(*session.image(session.active)),"recording ROM mismatch");
    scenario(session,recording,scene=="town" ? 3400 : scene=="world" ? (brazil ? 11500 : 9500) : 7000,scene=="dungeon",scene);
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
