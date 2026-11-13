// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/shared_save.h"
#include "temporary_cleanup.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
using namespace psmodern;
using namespace psmodern::app;
namespace fs=std::filesystem;
namespace {
constexpr std::array suffixes{"", ".bak", ".options", ".options.bak", ".automap", ".automap.bak"};
void check(bool value,const std::string& why) { if(!value) throw std::runtime_error(why); }
template<class F> void throws(F f,const char* why) { bool threw=false; try { f(); } catch(const std::exception&) { threw=true; } check(threw,why); }
void write(const fs::path& path,const std::string& bytes) {
    fs::create_directories(path.parent_path()); std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());check(bool(out),"write fixture");
}
std::string read(const fs::path& path) { std::ifstream in(path,std::ios::binary);check(bool(in),"read fixture");return {std::istreambuf_iterator<char>(in),{}}; }
fs::path legacy(const fs::path& dir,RomVersion v,unsigned i=0) { return dir/(std::string(save_name(v))+suffixes[i]); }
fs::path shared_path(const fs::path& dir,unsigned i=0) {return dir/"shared"/(std::string(shared_save_name)+suffixes[i]);}
std::string bytes(unsigned seed) { return std::string(8192,static_cast<char>(seed)); }
struct Temp {
    fs::path path;
    Temp() {
#ifdef _WIN32
        auto id=GetCurrentProcessId();
#else
        auto id=getpid();
#endif
        static unsigned seq=0;
        path=fs::temp_directory_path()/(fs::path(u8"psmodern-migration-ç-日本-")+=std::to_string(id)+"-"+std::to_string(++seq));
        check(fs::create_directories(path),"unique temporary folder");
    }
    ~Temp(){test::remove_temporary_folder(path);}
};
void complete(const fs::path& root,RomVersion v,unsigned seed) {
    write(legacy(root,v),bytes(seed));for(unsigned i=1;i<suffixes.size();++i)write(legacy(root,v,i),"opaque-"+std::to_string(seed)+"-"+std::to_string(i)+std::string("\0\xff",2));
}
void equal_set(const fs::path& root,RomVersion v) {
    for(unsigned i=0;i<suffixes.size();++i) {
        check(fs::exists(shared_path(root,i))==fs::exists(legacy(root,v,i)),"copied presence differs");
        if(fs::exists(shared_path(root,i)))check(read(shared_path(root,i))==read(legacy(root,v,i)),"copied bytes differ");
    }
}
void first_and_single() {
    {Temp t;auto store=open_shared_save(t.path);check(store && fs::is_directory(t.path/"shared") && !fs::exists(store->path()),"empty first launch");check(store->load()==Memory::CartridgeRam{},"empty image");}
    for(auto v:{RomVersion::us_rev_a,RomVersion::brazilian}) {
        Temp t;complete(t.path,v,42);std::array<fs::file_time_type,6> mtimes;
        for(unsigned i=0;i<6;++i)mtimes[i]=fs::last_write_time(legacy(t.path,v,i));
        auto store=open_shared_save(t.path);check(bool(store),"single candidate imported");equal_set(t.path,v);
        store->load_preferences(store->load());store->load_automap();
        check(store->settings_warning().has_value() && store->automap_warning().has_value(),
              "malformed companions remain available for normal warning handling");
        equal_set(t.path,v);
        Memory::CartridgeRam changed{};changed.fill(93);atomic_save(store->path(),changed);
        for(unsigned i=1;i<6;++i)atomic_text(shared_path(t.path,i),"shared changes");
        for(unsigned i=0;i<6;++i) {
            check(fs::last_write_time(legacy(t.path,v,i))==mtimes[i],"legacy timestamp changed");
            check(read(legacy(t.path,v,i))==(i ? "opaque-42-"+std::to_string(i)+std::string("\0\xff",2) : bytes(42)),"legacy content changed");
        }
    }
    {Temp t;write(legacy(t.path,RomVersion::brazilian,4),"invalid map is retained");auto store=open_shared_save(t.path);check(bool(store),"companion only imports");equal_set(t.path,RomVersion::brazilian);}
    {Temp t;complete(t.path,RomVersion::us_rev_a,8);complete(t.path,RomVersion::brazilian,8);auto store=open_shared_save(t.path,[](const auto&)->std::optional<RomVersion>{throw std::runtime_error("identical sets asked");});check(bool(store),"identical import");equal_set(t.path,RomVersion::us_rev_a);}
}
void choices() {
    for(unsigned variant=0;variant<3;++variant) {
        Temp t;complete(t.path,RomVersion::us_rev_a,7);complete(t.path,RomVersion::brazilian,variant==2?8:7);
        if(variant==0)write(legacy(t.path,RomVersion::brazilian,2),"different companion");
        if(variant==1)fs::remove(legacy(t.path,RomVersion::brazilian,5));
        unsigned calls=0;
        auto choose=[&](const LegacySaves& c)->std::optional<RomVersion>{++calls;check(c[0].state==LegacySaveState::available && c[1].state==LegacySaveState::available,"both candidates available");return {};};
        auto cancelled=open_shared_save(t.path,choose);check(!cancelled && calls==1 && !fs::exists(t.path/"shared"),"cancel retains both candidates");
        for(auto v:{RomVersion::us_rev_a,RomVersion::brazilian})for(unsigned i=0;i<6;++i)
            if(fs::exists(legacy(t.path,v,i))) {
                const auto seed=v==RomVersion::us_rev_a || variant!=2 ? 7 : 8;
                const auto expected=i==0 ? bytes(seed) : (v==RomVersion::brazilian && variant==0 && i==2 ?
                    std::string("different companion") : "opaque-"+std::to_string(seed)+"-"+std::to_string(i)+std::string("\0\xff",2));
                check(read(legacy(t.path,v,i))==expected,"legacy bytes unchanged after cancel");
            }
        auto selected=open_shared_save(t.path,[](const auto&)->std::optional<RomVersion>{return RomVersion::brazilian;});check(bool(selected),"explicit BR selection");equal_set(t.path,RomVersion::brazilian);
    }
}
void invalid_and_locks() {
    for(unsigned variant=0;variant<4;++variant) {
        Temp t;write(legacy(t.path,RomVersion::brazilian),bytes(55));
        const auto us=legacy(t.path,RomVersion::us_rev_a);
        std::unique_ptr<SaveLock> lock;
        if(variant==0)write(us,"short");
        if(variant==1)fs::create_directory(us);
        if(variant==2){std::ofstream f(legacy(t.path,RomVersion::us_rev_a,2),std::ios::binary);f.seekp(16*1024*1024);f.put('x');}
        if(variant==3){write(us,bytes(11));lock=std::make_unique<SaveLock>(t.path/(std::string(save_name(RomVersion::us_rev_a))+".lock"));}
        unsigned calls=0;
        auto chooser=[&](const LegacySaves& c)->std::optional<RomVersion>{++calls;check(c[0].state==LegacySaveState::unavailable && c[1].state==LegacySaveState::available,"invalid candidate reported unavailable");return RomVersion::brazilian;};
        throws([&]{open_shared_save(t.path,[](const auto&)->std::optional<RomVersion>{return RomVersion::us_rev_a;});},"unavailable candidate cannot be selected");
        check(!fs::exists(t.path/"shared"),"invalid choice did not publish");
        auto store=open_shared_save(t.path,chooser);check(store && calls==1,"valid other selected explicitly");equal_set(t.path,RomVersion::brazilian);
    }
    {Temp t;write(legacy(t.path,RomVersion::us_rev_a),"bad");throws([&]{open_shared_save(t.path);},"invalid only must fail");check(!fs::exists(t.path/"shared"),"invalid did not publish");}
    {
        Temp t;write(legacy(t.path,RomVersion::us_rev_a),"bad");
        fs::create_directory(legacy(t.path,RomVersion::brazilian,2));
        std::string diagnostic;
        try {open_shared_save(t.path);} catch(const std::exception& error) {diagnostic=error.what();}
        check(diagnostic.find("US: phantasy-star-us-rev-a.sav: ")!=std::string::npos &&
              diagnostic.find("BR: phantasy-star-brazil.sav.options: ")!=std::string::npos &&
              diagnostic.find("unsupported size")!=std::string::npos && diagnostic.find("not a regular file")!=std::string::npos,
              "unavailable-only error preserves both labelled artifact failures");
        check(!fs::exists(t.path/"shared"),"diagnostic failure did not publish");
    }
    {Temp t;SaveLock lock(t.path/"shared-import.lock");throws([&]{open_shared_save(t.path);},"import lock excludes another initializer");check(!fs::exists(t.path/"shared"),"locked import did not publish");}
    {Temp t;SaveLock lock(t.path/(std::string(save_name(RomVersion::us_rev_a))+".lock"));throws([&]{open_shared_save(t.path);},"locked unknown legacy state is not absent");check(!fs::exists(t.path/"shared"),"unknown legacy state did not publish");}
    {Temp t;fs::create_directory(t.path/"shared");SaveLock lock(shared_path(t.path).string()+".lock");throws([&]{open_shared_save(t.path);},"canonical lock excludes another session");}
}
void authority_and_failure() {
    {Temp t;write(legacy(t.path,RomVersion::us_rev_a),bytes(1));fs::create_directory(t.path/"shared");auto store=open_shared_save(t.path);check(store && !fs::exists(store->path()),"empty canonical is authoritative");}
    {Temp t;write(legacy(t.path,RomVersion::us_rev_a),bytes(1));write(shared_path(t.path),"bad");throws([&]{open_shared_save(t.path);},"invalid canonical fails");check(read(shared_path(t.path))=="bad","invalid canonical untouched");}
    {Temp t;write(legacy(t.path,RomVersion::us_rev_a),bytes(1));write(t.path/"shared","not a directory");throws([&]{open_shared_save(t.path);},"non-directory canonical fails");check(read(t.path/"shared")=="not a directory","canonical object untouched");}
    for(unsigned phase=0;phase<9;++phase) {
        Temp t;complete(t.path,RomVersion::us_rev_a,12);unsigned copied=0;
        auto observer=[&](ImportStep step){if((phase==0 && step==ImportStep::stage_created) || (phase>=1 && phase<=6 && step==ImportStep::file_copied && ++copied==phase) || (phase==7 && step==ImportStep::before_publish) || (phase==8 && step==ImportStep::after_publish))throw std::runtime_error("injected failure");};
        throws([&]{open_shared_save(t.path,{},observer);},"injected failure propagated");
        check(fs::exists(t.path/"shared")==(phase==8),"failure exposes only fully published canonical storage");
        for(const auto& file:fs::directory_iterator(t.path))
            check(!file.path().filename().string().starts_with(".shared-import-"),"failed operation left its own staging directory at phase "+std::to_string(phase));
        auto recovered=open_shared_save(t.path);check(bool(recovered),"failure recovery opens store");equal_set(t.path,RomVersion::us_rev_a);
    }
}
void cleanup_locks() {
#ifdef _WIN32
    for(bool persistent:{false,true}) {
        Temp t;const auto original=bytes(17);write(legacy(t.path,RomVersion::us_rev_a),original);
        HANDLE handle=INVALID_HANDLE_VALUE;fs::path staged;std::thread release;bool locked=false;
        auto observer=[&](ImportStep step) {
            if(step!=ImportStep::file_copied)return;
            for(const auto& entry:fs::directory_iterator(t.path))
                if(entry.path().filename().string().starts_with(".shared-import-"))staged=entry.path();
            check(!staged.empty(),"find own stage for a host-lock fixture");
            handle=CreateFileW((staged/shared_save_name).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            check(handle!=INVALID_HANDLE_VALUE,"open staged file without delete sharing");
            locked=true;
            if(!persistent)release=std::thread([h=handle] {std::this_thread::sleep_for(std::chrono::milliseconds(80));CloseHandle(h);});
            throw std::runtime_error("injected failure with host handle still open");
        };
        throws([&]{open_shared_save(t.path,{},observer);},"locked-stage failure propagated");
        if(release.joinable())release.join();
        if(persistent)CloseHandle(handle);
        check(locked,"the cleanup fixture actually acquired a non-delete-sharing handle");
        check(!fs::exists(t.path/"shared") && read(legacy(t.path,RomVersion::us_rev_a))==original,"cleanup never publishes or edits progress");
        check(fs::exists(staged)==persistent,"transient cleanup retries; persistent stage remains harmless");
        auto recovered=open_shared_save(t.path);
        check(recovered && read(recovered->path())==original,"retry ignores an unremoved stage and imports complete progress");
    }
#endif
}
void signal(const fs::path& path) {write(path,"ready");}
void wait_file(const fs::path& path) {for(unsigned n=0;n<3000 && !fs::exists(path);++n)std::this_thread::sleep_for(std::chrono::milliseconds(10));check(fs::exists(path),"coordination timed out");}
int worker(int argc,char** argv) {
    check(argc>=4,"worker arguments");const std::string mode=argv[1];const fs::path folder=fs::path(argv[2]);const std::string phase=argv[3];
    if(mode=="--lock") {SaveLock lock(folder/fs::path(phase));signal(folder/"ready");wait_file(folder/"release");return 0;}
    unsigned copies=0;
    auto observer=[&](ImportStep step) {
        if(mode=="--hold" && step==ImportStep::before_publish) {signal(folder/"ready");wait_file(folder/"release");}
        const bool stop=(phase=="stage" && step==ImportStep::stage_created) || (phase=="before" && step==ImportStep::before_publish) || (phase=="after" && step==ImportStep::after_publish) ||
            (step==ImportStep::file_copied && phase=="copy"+std::to_string(++copies));
        if(stop)std::_Exit(23);
    };
    auto store=open_shared_save(folder,[](const auto&)->std::optional<RomVersion>{return RomVersion::us_rev_a;},observer);
    return store ? 0 : 2;
}
}
int main(int argc,char** argv) {try {
    if(argc>1)return worker(argc,argv);
    first_and_single();choices();invalid_and_locks();authority_and_failure();cleanup_locks();
    std::cout<<"Shared save import fixtures passed.\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
