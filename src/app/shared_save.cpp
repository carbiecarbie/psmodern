// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/shared_save.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace psmodern::app {
namespace {
constexpr std::array<const char*,6> suffixes{"",".bak",".options",".options.bak",".automap",".automap.bak"};
constexpr std::uintmax_t import_file_limit=16*1024*1024;
using Snapshot=std::array<std::optional<std::string>,suffixes.size()>;
std::filesystem::file_status artifact_status(const std::filesystem::path& path) {
    std::error_code error;auto result=std::filesystem::symlink_status(path,error);
    if(result.type()==std::filesystem::file_type::not_found)return result;
    if(error)throw std::system_error(error,"Inspecting save storage");
    return result;
}
Snapshot snapshot(const std::filesystem::path& folder,RomVersion version) {
    Snapshot result;
    for(std::size_t i=0;i<suffixes.size();++i) {
        const auto name=std::string(save_name(version))+suffixes[i];
        auto path=folder/name;
        try {
        const auto state=artifact_status(path);
        if(state.type()==std::filesystem::file_type::not_found)continue;
        if(!std::filesystem::is_regular_file(state))throw std::runtime_error("Legacy save artifact is not a regular file.");
        const auto size=std::filesystem::file_size(path);
        if(size>import_file_limit || (i==0 && size!=Memory::CartridgeRam{}.size()))
            throw std::runtime_error("Legacy save artifact has an unsupported size.");
        std::ifstream file(path,std::ios::binary);
        if(!file)throw std::runtime_error("Cannot open legacy save artifact.");
        std::string bytes(static_cast<std::size_t>(size),'\0');
        if(!file.read(bytes.data(),static_cast<std::streamsize>(size)) || file.peek()!=std::char_traits<char>::eof())
            throw std::runtime_error("Cannot read complete legacy save artifact.");
        result[i]=std::move(bytes);
        } catch(const std::exception& error) {throw std::runtime_error(name+": "+error.what());}
    }
    return result;
}
struct Stage {
    std::filesystem::path path;
    explicit Stage(const std::filesystem::path& folder) {
        static std::atomic<unsigned> sequence=0;
#ifdef _WIN32
        const auto pid=GetCurrentProcessId();
#else
        const auto pid=getpid();
#endif
        for(unsigned attempt=0;attempt<100;++attempt) {
            auto candidate=folder/(".shared-import-"+std::to_string(pid)+"-"+std::to_string(++sequence));
            if(std::filesystem::create_directory(candidate)) {path=std::move(candidate);return;}
        }
        throw std::runtime_error("Cannot allocate shared-save staging directory.");
    }
    ~Stage() noexcept {
        if(path.empty())return; // Published storage is never a cleanup target.
        try {
            std::error_code error;
            for(unsigned attempt=0;attempt<6;++attempt) {
                std::filesystem::remove_all(path,error);
                if(!error)return;
                // A host handle can briefly outlive the failed write.
                if(attempt<5)std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            std::cerr<<"Warning: shared-save staging cleanup failed; the leftover stage will be ignored: "<<error.message()<<'\n';
        } catch(...) { /* Cleanup must not hide the original import failure. */ }
    }
    void publish(const std::filesystem::path& target) {
#ifdef _WIN32
        // No REPLACE_EXISTING: even an independently created empty directory
        // must never be overwritten. Staged files were flushed individually.
        if(!MoveFileExW(path.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(),std::system_category(),"Publishing shared saves");
        path.clear();
#else
        (void)target;
        throw std::runtime_error("Shared-save publication currently requires Windows.");
#endif
    }
};
}
std::optional<SaveStore> open_shared_save(const std::filesystem::path& folder,
    const ChooseLegacySave& choose,const ImportObserver& observer) {
    std::filesystem::create_directories(folder);
    SaveLock import_lock(folder/"shared-import.lock");
    const auto canonical=folder/"shared";
    const auto existing=artifact_status(canonical);
    if(existing.type()!=std::filesystem::file_type::not_found) {
        if(!std::filesystem::is_directory(existing))throw std::runtime_error("Shared save storage is not a directory.");
        auto store=SaveStore::shared(folder);store.load();return store;
    }
    LegacySaves candidates;
    std::array<Snapshot,2> images;
    std::array<std::unique_ptr<SaveLock>,2> locks;
    for(unsigned i=0;i<2;++i) {
        const auto version=i ? RomVersion::brazilian : RomVersion::us_rev_a;
        try {
            locks[i]=std::make_unique<SaveLock>(folder/(std::string(save_name(version))+".lock"));
            images[i]=snapshot(folder,version);
            for(const auto& file:images[i])if(file) {candidates[i].state=LegacySaveState::available;break;}
        } catch(const std::exception& error) {candidates[i]={LegacySaveState::unavailable,error.what()};}
    }
    const auto available=[&](unsigned i){return candidates[i].state==LegacySaveState::available;};
    const auto absent=[&](unsigned i){return candidates[i].state==LegacySaveState::absent;};
    std::optional<unsigned> selected;
    if(available(0) && (absent(1) || (available(1) && images[0]==images[1])))selected=0;
    else if(available(1) && absent(0))selected=1;
    else if(!absent(0) || !absent(1)) {
        if(!available(0) && !available(1)) {
            std::string detail="Legacy saved games are unavailable; no shared save was created.";
            for(unsigned i=0;i<2;++i)if(!candidates[i].error.empty())
                detail+=std::string("\n")+(i ? "BR: " : "US: ")+candidates[i].error;
            throw std::runtime_error(detail);
        }
        if(!choose)throw std::runtime_error("Choosing legacy saved games is required before creating shared storage.");
        const auto choice=choose(candidates);if(!choice)return {};
        if(*choice!=RomVersion::us_rev_a && *choice!=RomVersion::brazilian)throw std::runtime_error("Invalid legacy save choice.");
        selected=*choice==RomVersion::us_rev_a ? 0u : 1u;
        if(!available(*selected))throw std::runtime_error("Chosen legacy saved games are unavailable.");
    }
    const auto observe=[&](ImportStep step){if(observer)observer(step);};
    Stage stage(folder);observe(ImportStep::stage_created);
    if(selected)for(std::size_t i=0;i<suffixes.size();++i)if(const auto& bytes=images[*selected][i]) {
        // atomic_text writes arbitrary bytes with a flush, without interpreting
        // or normalizing the native save, companions or backups.
        atomic_text(stage.path/(std::string(shared_save_name)+suffixes[i]),*bytes);
        observe(ImportStep::file_copied);
    }
    observe(ImportStep::before_publish);stage.publish(canonical);observe(ImportStep::after_publish);
    auto store=SaveStore::shared(folder);store.load();return store;
}
}
