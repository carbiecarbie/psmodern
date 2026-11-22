// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "app/rom_import.h"
#include "app/session_roms.h"
#include "app/sound_preference.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace psmodern;
using namespace psmodern::app;
namespace {
constexpr auto jp_digest="c9a40ddd217c58dddcd6b5c0fe66c3a50d3e68e4";
void check(bool ok,const char* why) {
    if (!ok) throw std::runtime_error(why);
}
template<class F> void refuses(F action,const char* why) {
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(why);
}
struct Temporary {
    fs::path path=fs::temp_directory_path()/("psmodern-sound-storage-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { check(fs::create_directory(path),"Expected a unique test folder."); }
    ~Temporary() { psmodern::test::remove_temporary_folder(path); }
};
std::string read(const fs::path& path) {
    return read_text_file(path,rom_list_limit,"test file");
}
void preference_storage(const fs::path& folder) {
    check(load_sound_preference(folder).preference==SoundPreference::psg &&
          !load_sound_preference(folder).warning,"Default sound is PSG.");
    for (auto value:{SoundPreference::fm,SoundPreference::psg}) {
        save_sound_preference(folder,value);
        const auto saved=load_sound_preference(folder);
        check(saved.preference==value && !saved.warning,"Sound choice round trips.");
    }
    check(parse_sound_preference("psmodern-sound 1\r\nfm\r\n")==SoundPreference::fm,"Accept CRLF.");
    for (auto bad:{"psmodern-sound 2\nfm\n","psmodern-sound 1\nFM\n","psmodern-sound 1\nfm",
                  "psmodern-sound 1\nfm\nextra\n"})
        refuses([&]{parse_sound_preference(bad);},"Malformed sound preference accepted.");
    const auto path=folder/sound_file_name;
    const auto corrupt=std::string(sound_file_limit+1,'x');
    atomic_text(path,corrupt);
    const auto fallback=load_sound_preference(folder);
    check(fallback.preference==SoundPreference::psg && fallback.warning && read(path)==corrupt,
          "A malformed preference is preserved and uses PSG.");
    save_sound_preference(folder,SoundPreference::fm);
    check(effective_sound_preference(load_sound_preference(folder).preference,false)==SoundPreference::psg &&
          load_sound_preference(folder).preference==SoundPreference::fm,
          "Losing the Japanese ROM leaves the stored FM choice intact.");
    check(effective_sound_preference(SoundPreference::fm,true)==SoundPreference::fm,
          "FM is effective when the sound engine is available.");
    refuses([&]{save_sound_preference(folder,static_cast<SoundPreference>(-1));},"Invalid preference saved.");
    check(read(path)==format_sound_preference(SoundPreference::fm),"Rejected choice left the preference unchanged.");
#ifdef _WIN32
    {
        const SaveLock blocked(path);
        refuses([&]{save_sound_preference(folder,SoundPreference::psg);},"An open sound preference was replaced.");
    }
    check(read(path)==format_sound_preference(SoundPreference::fm),"Replacing a blocked file preserved the previous sound choice.");
#endif
    const auto held=folder/"held-sound.txt";
    fs::rename(path,held);
    fs::create_directory(path);
    refuses([&]{save_sound_preference(folder,SoundPreference::psg);},"Non-file sound preference was overwritten.");
    check(fs::is_directory(path) && read(held)==format_sound_preference(SoundPreference::fm),
          "Failed publication preserved the destination and previous choice.");
}
void separate_path_storage(const fs::path& folder) {
    constexpr auto playable_jp_tag="psmodern-roms 1\njp ";
    check(!load_sound_rom_path(folder).path && !load_sound_rom_path(folder).warning,"Sound ROM is initially absent.");
    const auto us=folder/"first us.sms", br=folder/"first br.sms", jp=folder/fs::path(u8"\u65e5\u672c.sms");
    const RomList initial{us,br};
    save_rom_list(folder,initial);
    const auto list_file=folder/rom_list_name;
    const auto before=read(list_file);
    save_sound_rom_path(folder,jp);
    check(read(list_file)==before && load_rom_list(folder).list==initial && load_sound_rom_path(folder).path==jp,
          "Adding Japanese sound preserves the original v1 list and remembers UTF-8 paths.");
    refuses([&]{parse_rom_list(std::string(playable_jp_tag)+us.generic_string()+'\n');},
            "The v1 reader must still reject a JP entry.");
    // A v1 edit updates only the playable record, as release 1.2.0 does.
    const RomList old_build_edit{{},br};
    const auto sound_before=read(folder/sound_rom_path_name);
    atomic_text(list_file,format_rom_list(old_build_edit));
    check(load_rom_list(folder).list==old_build_edit && load_sound_rom_path(folder).path==jp &&
          read(folder/sound_rom_path_name)==sound_before,"A v1 downgrade edit preserves the sound record.");
    refuses([&]{save_sound_rom_path(folder,"relative.sms");},"Relative JP path accepted.");
    const auto replacement=folder/"second jp.sms";
    save_sound_rom_path(folder,replacement);
    check(load_sound_rom_path(folder).path==replacement && load_rom_list(folder).list==old_build_edit,
          "Replacing a sound ROM preserves the playable list.");
    const std::string invalid="psmodern-sound-rom 1\nrelative.sms\n";
    atomic_text(folder/sound_rom_path_name,invalid);
    const auto bad=load_sound_rom_path(folder);
    check(!bad.path && bad.warning && read(folder/sound_rom_path_name)==invalid && load_rom_list(folder).list==old_build_edit,
          "An invalid sound path file is preserved and does not affect the playable list.");
}
int actual_roms(const fs::path& us,const fs::path& br,const fs::path& jp) {
    if (!fs::is_regular_file(us) || !fs::is_regular_file(br) || !fs::is_regular_file(jp)) return 77;
    Temporary temp;
    RomImport importer(temp.path);
    const auto stage=importer.staging_file(".sms");
    auto install=[&](const fs::path& source,CartridgeKind kind,const char* name) {
        fs::copy_file(source,stage);
        const auto imported=importer.publish(stage);
        check(imported.kind==kind && imported.path.filename()==name && !fs::exists(stage),
              "Each cartridge publishes to its reserved path.");
        return imported.path;
    };
    const auto japanese=install(jp,CartridgeKind::japanese_sound,"phantasy-star-japan.sms");
    save_sound_rom_path(temp.path,japanese);
    check(load_rom_list(temp.path).list.empty() && !fs::exists(temp.path/"shared"),"Japanese import creates no playable entry or save.");
    refuses([&]{load_session_roms(japanese,{},LanguagePreference::native);},"Japanese CLI primary accepted.");
    const auto brazilian=install(br,CartridgeKind::brazilian,"phantasy-star-brazil.sms");
    RomList playable{{},brazilian};
    save_rom_list(temp.path,playable);
    auto lookup=[&](const fs::path& primary,LanguagePreference language,bool restricted=false) {
        return load_session_roms(primary,load_rom_list(temp.path).list,language,restricted,load_sound_rom_path(temp.path).path);
    };
    const auto first=lookup(brazilian,LanguagePreference::native);
    check(first.source_version==RomVersion::brazilian && first.active==Language::portuguese &&
          !first.available(Language::english) && first.warnings.empty() && first.sound_rom &&
          sha1(*first.sound_rom)==jp_digest,"Brazilian play can use the full Japanese sound image.");
    const auto american=install(us,CartridgeKind::us_rev_a,"phantasy-star-us-rev-a.sms");
    playable.us=american;
    save_rom_list(temp.path,playable);
    const auto replaced=install(jp,CartridgeKind::japanese_sound,"phantasy-star-japan.sms");
    check(replaced==japanese && identify_rom(american)==RomVersion::us_rev_a && identify_rom(brazilian)==RomVersion::brazilian,
          "Replacing an imported Japanese file leaves both playable files alone.");
    const auto all=lookup(american,LanguagePreference::portuguese);
    check(all.source_version==RomVersion::us_rev_a && all.active==Language::portuguese && all.sound_rom &&
          all.available(Language::english) && all.available(Language::portuguese) && all.warnings.empty(),
          "Three registered images serve independent roles.");
    const auto restricted=lookup(american,LanguagePreference::portuguese,true);
    check(restricted.restricted && !restricted.sound_rom && !restricted.available(Language::portuguese) && restricted.warnings.empty(),
          "Original-only and recording sessions ignore optional images.");
    fs::remove(japanese);
    const auto missing=lookup(american,LanguagePreference::native);
    check(!missing.sound_rom && missing.warnings.size()==1 && load_sound_rom_path(temp.path).path==japanese,
          "A missing sound image falls back without forgetting its path.");
    fs::create_directory(japanese);
    const auto unreadable=lookup(american,LanguagePreference::portuguese);
    check(!unreadable.sound_rom && unreadable.warnings.size()==1 && unreadable.active==Language::portuguese &&
          load_sound_rom_path(temp.path).path==japanese,"A temporarily inaccessible sound image is retained for the next startup.");
    check(sha1(*all.sound_rom)==jp_digest,"Existing sessions own the verified image.");
    fs::remove(japanese);
    fs::copy_file(br,japanese);
    const auto wrong=lookup(american,LanguagePreference::native);
    check(!wrong.sound_rom && wrong.warnings.size()==1 && wrong.active==Language::english,
          "A different real cartridge replacing the Japanese image cannot enable sound.");
    fs::copy_file(jp,japanese,fs::copy_options::overwrite_existing);
    check(bool(lookup(american,LanguagePreference::native).sound_rom),"Restoring the sound image enables it again.");
    return 0;
}
}
int main(int argc,char** argv) {
    try {
        if (argc==4) return actual_roms(argv[1],argv[2],argv[3]);
        check(argc==1,"Expected either three real ROM paths or no arguments.");
        check(identify_cartridge_digest(jp_digest)==CartridgeKind::japanese_sound &&
              !playable_version(CartridgeKind::japanese_sound),"Only the cartridge picker classifies the Japanese dump.");
        refuses([]{identify_digest(jp_digest);},"Japanese digest was accepted for gameplay.");
        refuses([]{identify_cartridge_digest("c9a40ddd217c58dddcd6b5c0fe66c3a50d3e68e5");},
                "An almost matching digest qualified.");
        Temporary temp;
        preference_storage(temp.path/"preference");
        separate_path_storage(temp.path/"inventory");
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
