// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_roms.h"
#include "rom/sha1.h"
#include <iostream>
#include <stdexcept>

using namespace psmodern;
using namespace psmodern::app;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {try {
    if(argc!=3)throw std::runtime_error("Expected both ROM paths.");
    if(!*argv[1] || !*argv[2] || !std::filesystem::is_regular_file(argv[1]) || !std::filesystem::is_regular_file(argv[2]))return 77;
    const RomList list{std::filesystem::path(argv[1]),std::filesystem::path(argv[2])};
    for(const auto profile:{RomVersion::us_rev_a,RomVersion::brazilian}) {
        const auto other=profile==RomVersion::us_rev_a ? RomVersion::brazilian : RomVersion::us_rev_a;
        const auto selected=other==RomVersion::us_rev_a ? LanguagePreference::english : LanguagePreference::portuguese;
        const auto path=*list.get(profile);
        const auto both=load_session_roms(path,list,selected);
        check(both.source_version==profile && both.active==native_language(other),"Changing presentation keeps source save profile");
        check(both.available(Language::english) && both.available(Language::portuguese) && both.warnings.empty(),"Both loaded images available");
        check(identify_digest(sha1(*both.image(both.active)))==other,"Active image is the identified target cartridge");
        const auto automatic=load_session_roms(path,list,LanguagePreference::native);
        check(automatic.active==native_language(profile),"Automatic follows the selected save profile");
        const auto alone=load_session_roms(path,{},selected);
        check(alone.active==native_language(profile) && alone.language_unavailable && alone.preference==selected,
              "Missing companion falls back without losing saved preference");
        auto wrong=list;wrong.set(other,path);
        const auto rejected=load_session_roms(path,wrong,selected);
        check(rejected.language_unavailable && rejected.warnings.size()==1 && !rejected.available(native_language(other)),
              "Wrong-version companion cannot become an available language");
        const auto restricted=load_session_roms(path,wrong,selected,true);
        check(restricted.restricted && restricted.active==native_language(profile) && restricted.warnings.empty() &&
              !restricted.available(native_language(other)),"Restricted sessions ignore companion and preference");
        wrong.set(other,path.parent_path()/"psmodern-missing-language-rom.sms");
        const auto missing=load_session_roms(path,wrong,selected);
        check(missing.warnings.size()==1 && missing.active==native_language(profile),"Unavailable optional file never blocks primary play");
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
