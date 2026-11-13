// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_store.h"
#include "rom/sha256.h"
#include <algorithm>
#include <atomic>
#include <span>
#include <fstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

namespace psmodern::app {
const char* save_name(RomVersion version) {
    return version==RomVersion::us_rev_a ? "phantasy-star-us-rev-a.sav" : "phantasy-star-brazil.sav";
}
std::filesystem::path production_save_folder() {
#ifdef _WIN32
    const auto length=GetEnvironmentVariableW(L"APPDATA",nullptr,0);
    if (length==0) throw std::runtime_error("Cannot resolve %APPDATA% for saves.");
    std::wstring value(length,L'\0');
    const auto written=GetEnvironmentVariableW(L"APPDATA",value.data(),length);
    if (!written || written>=length) throw std::runtime_error("Cannot resolve %APPDATA% for saves.");
    value.resize(written);
    std::filesystem::path folder(value);
    if (!folder.is_absolute()) throw std::runtime_error("%APPDATA% must be an absolute folder.");
    return folder/"PSModern";
#else
    throw std::runtime_error("Production save location currently requires Windows %APPDATA%.");
#endif
}
SaveStore::SaveStore(std::filesystem::path folder,RomVersion version,Writer writer,SettingsWriter settings_writer,SettingsWriter automap_writer)
    :path_(std::move(folder)/save_name(version)),writer_(std::move(writer)),settings_writer_(std::move(settings_writer)),
     automap_writer_(std::move(automap_writer)) {}
SaveStore SaveStore::shared(const std::filesystem::path& folder) {
    SaveStore store(folder/"shared",RomVersion::us_rev_a);
    store.path_=folder/"shared"/shared_save_name;
    return store;
}
struct SaveLock::Impl {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~Impl() { if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
    int fd=-1;
    ~Impl() { if (fd>=0) close(fd); }
#endif
};
SaveLock::SaveLock(const std::filesystem::path& lock_path):impl_(std::make_unique<Impl>()) {
    auto& lock=impl_;
#ifdef _WIN32
    lock->handle=CreateFileW(lock_path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (lock->handle==INVALID_HANDLE_VALUE) {
        auto error=GetLastError();
        if (error==ERROR_SHARING_VIOLATION || error==ERROR_LOCK_VIOLATION)
            throw std::runtime_error("Save is already open.");
        throw std::system_error(error,std::system_category(),"Opening save lock");
    }
#else
    lock->fd=open(lock_path.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if (lock->fd<0) throw std::system_error(errno,std::generic_category(),"Opening save lock");
    if (flock(lock->fd,LOCK_EX|LOCK_NB)) {
        if (errno==EWOULDBLOCK) throw std::runtime_error("Save is already open.");
        throw std::system_error(errno,std::generic_category(),"Locking save");
    }
#endif
}
SaveLock::~SaveLock()=default;
SaveStore::~SaveStore()=default;
SaveStore::SaveStore(SaveStore&&) noexcept=default;
SaveStore& SaveStore::operator=(SaveStore&&) noexcept=default;
void SaveStore::ensure_lock() const {
    if (lock_) return;
    std::filesystem::create_directories(path_.parent_path());
    auto lock_path=path_;lock_path += ".lock";
    lock_=std::make_unique<SaveLock>(lock_path);
}
Memory::CartridgeRam SaveStore::load() const {
    ensure_lock();
    Memory::CartridgeRam image{};
    const auto status=std::filesystem::status(path_);
    if (status.type()==std::filesystem::file_type::not_found) return image;
    if (!std::filesystem::is_regular_file(status) || std::filesystem::file_size(path_)!=image.size())
        throw std::runtime_error("Refusing save (expected a regular 8192-byte file): "+path_.string());
    std::ifstream file(path_,std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(image.data()),image.size()) || file.peek()!=std::char_traits<char>::eof())
        throw std::runtime_error("Cannot read complete save; it will not be overwritten: "+path_.string());
    return image;
}
static void publish_bytes(const std::filesystem::path& path,std::span<const std::uint8_t> image,bool validate_destination) {
    std::filesystem::create_directories(path.parent_path());
    // Refuse a newly malformed destination too (e.g. external replacement).
    if (validate_destination && std::filesystem::exists(path) && (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)!=image.size()))
        throw std::runtime_error("Refusing to replace malformed save: "+path.string());
#ifdef _WIN32
    std::filesystem::path temp;
    HANDLE handle=INVALID_HANDLE_VALUE;
    for (unsigned attempt=0;attempt<100 && handle==INVALID_HANDLE_VALUE;++attempt) {
        static std::atomic<unsigned> sequence=0;   // several drivers may save at once (tests)
        temp=path; temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence);
        handle=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (handle==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_EXISTS)
            throw std::system_error(GetLastError(),std::system_category(),"Creating temporary save");
    }
    if (handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot allocate temporary save.");
    try {
        DWORD written=0;
        if (!WriteFile(handle,image.data(),static_cast<DWORD>(image.size()),&written,nullptr) || written!=image.size())
            throw std::system_error(GetLastError(),std::system_category(),"Writing temporary save");
        if (!FlushFileBuffers(handle)) throw std::system_error(GetLastError(),std::system_category(),"Flushing temporary save");
        const auto closed=CloseHandle(handle); handle=INVALID_HANDLE_VALUE;
        if (!closed) throw std::system_error(GetLastError(),std::system_category(),"Closing temporary save");
        if (!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(),std::system_category(),"Replacing save");
    } catch (...) {
        if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);
        DeleteFileW(temp.c_str()); throw;
    }
#else
    auto temp=path.string()+".tmp-XXXXXX";
    int fd=mkstemp(temp.data());
    if (fd<0) throw std::system_error(errno,std::generic_category(),"Creating temporary save");
    try {
        std::size_t offset=0;
        while (offset<image.size()) {
            const auto n=::write(fd,image.data()+offset,image.size()-offset);
            if (n<=0) throw std::system_error(errno,std::generic_category(),"Writing temporary save");
            offset+=n;
        }
        if (fsync(fd)) throw std::system_error(errno,std::generic_category(),"Flushing temporary save");
        const auto closed=close(fd); fd=-1;
        if (closed) throw std::system_error(errno,std::generic_category(),"Closing temporary save");
        std::filesystem::rename(temp,path);
    } catch (...) { if (fd>=0) close(fd); unlink(temp.c_str()); throw; }
#endif
}
void atomic_save(const std::filesystem::path& path,const Memory::CartridgeRam& image) {
    publish_bytes(path,image,true);
}
void atomic_text(const std::filesystem::path& path,const std::string& text) {
    if(std::filesystem::exists(path) && !std::filesystem::is_regular_file(path))
        throw std::runtime_error("Refusing to replace non-file: "+path.string());
    publish_bytes(path,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()},false);
}
std::string read_text_file(const std::filesystem::path& path,std::size_t limit,const char* what) {
    if(!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)>limit)
        throw std::runtime_error(std::string("Expected a regular ")+what+" of at most "+std::to_string(limit)+" bytes.");
    std::ifstream file(path,std::ios::binary);
    std::string text(limit+1,'\0');file.read(text.data(),text.size());
    const auto count=file.gcount();
    if(file.bad() || static_cast<std::size_t>(count)>limit || !file.eof())throw std::runtime_error(std::string("Cannot read bounded ")+what+".");
    text.resize(static_cast<std::size_t>(count));
    return text;
}
namespace {
std::string read_settings_file(const std::filesystem::path& path) { return read_text_file(path,1024,"settings file"); }
}
game::Options SaveStore::load_preferences(const Memory::CartridgeRam& image) {
    ensure_lock();settings_loaded_=true;settings_blocked_=false;settings_warning_.reset();preferences_={};automap_preference_=false;
    const auto digest=sha256(image);settings_={{digest,{}},{},{}};
    if(!std::filesystem::exists(settings_path()))return preferences_;
    try {
        settings_=parse_settings(read_settings_file(settings_path()));
        const auto selected=settings_.select(digest);
        if(!selected)throw std::runtime_error("Settings do not match the cartridge image.");
        preferences_=selected->options;automap_preference_=selected->automap;
    } catch(const std::exception& e) {
        settings_blocked_=true;
        settings_warning_="Using original settings; companion preserved until explicit Apply: "+settings_path().string()+": "+e.what();
    }
    return preferences_;
}
void SaveStore::ensure_backup() {
    if(backup_done_)return;
    const auto original=load();
    if(!settings_loaded_)load_preferences(original);
    const auto digest=sha256(original);
    session_backup_=settings_blocked_ ? SettingsRecord{digest,{}} : settings_.select(digest).value_or(SettingsRecord{digest,{}});
    session_backup_->digest=digest;
    // Publish the companion snapshot first. It is deliberately independent of
    // raw backup publication; digest selection prevents a mismatched restore.
    if(!settings_blocked_ && std::filesystem::exists(settings_path())) {
        auto backup=settings_path();backup+=".bak";
        settings_writer_(backup,read_settings_file(settings_path()));
    } else if(!settings_blocked_) {
        auto backup=settings_path();backup+=".bak";
        settings_writer_(backup,write_settings(SettingsImage{*session_backup_,{},settings_.backup}));
    }
    if(std::filesystem::exists(path_)) {
        auto backup=path_;backup+=".bak";publish_bytes(backup,original,false);
    }
    backup_done_=true;
}
void SaveStore::stage(const Memory::CartridgeRam& image,const game::Options& preferences) {
    const auto disk=load();
    const auto digest=sha256(disk);
    auto committed=settings_.select(digest).value_or(SettingsRecord{digest,{}});committed.digest=digest;
    SettingsImage next{committed,SettingsRecord{sha256(image),preferences,automap_preference_},
        session_backup_ ? session_backup_ : settings_.backup};
    settings_writer_(settings_path(),write_settings(next));
    settings_=std::move(next);
}
std::optional<std::string> SaveStore::choose(const game::Options& options,const Memory::CartridgeRam& image) {
    game::validate(options);
    try { ensure_lock();if(!settings_loaded_)load_preferences(load()); }
    catch(const std::exception& e) { return "Settings write failed; choices were not applied: "+std::string(e.what()); }
    return choose(options,automap_preference_,image);
}
std::optional<std::string> SaveStore::choose(const game::Options& options,bool automap,const Memory::CartridgeRam& image) {
    game::validate(options);
    try {
        ensure_lock();if(!settings_loaded_)load_preferences(load());
        ensure_backup();
        const auto disk=load();
        SettingsImage next{{sha256(disk),options,automap},SettingsRecord{sha256(image),options,automap},
            session_backup_ ? session_backup_ : settings_.backup};
        settings_writer_(settings_path(),write_settings(next));settings_=std::move(next);
        preferences_=options;automap_preference_=automap;settings_blocked_=false;settings_warning_.reset();return {};
    } catch(const std::exception& e) { return "Settings write failed; choices were not applied: "+std::string(e.what()); }
}
void SaveStore::load_automap() {
    if(automap_loaded_)return;
    ensure_lock();
    automap_loaded_=true;
    try {
        const auto status=std::filesystem::status(automap_path());
        if(status.type()==std::filesystem::file_type::not_found)return;
        automap_=parse_automap(read_text_file(automap_path(),automap_max_bytes,"automap file"));
    } catch(const std::exception& e) {
        automap_={};automap_blocked_=true;
        automap_warning_="Automap file unusable; maps start empty, and the file is kept as .automap.bak at the next save: "+
            automap_path().string()+": "+e.what();
    }
}
const game::Exploration* SaveStore::automap_record(std::string_view digest) {
    load_automap();
    return automap_.find(digest);
}
void SaveStore::stage_automap(std::string digest,const game::Exploration& exploration) {
    load_automap();
    automap_.put(std::move(digest),exploration);automap_dirty_=true;
    automap_wait_=automap_backoff_=0;   // a new record is tried at the next flush
}
std::optional<std::string> SaveStore::flush_automap(const Memory::CartridgeRam& image) {
    try {
        load_automap();
        // Keep the records of every used slot of the image on disk (the old
        // save) and of the image about to be written (the new one): a crash
        // between this write and the cartridge write loses neither.
        auto keep=used_slot_digests(load());keep.merge(used_slot_digests(image));
        auto next=automap_;next.prune(keep);
        const auto text=write_automap(next);
        // Once per session, before anything is written, the current file
        // becomes .automap.bak, an unusable one byte for byte (any size). A
        // file that cannot be preserved (not a regular file, unreadable) is
        // never replaced: the write fails, is reported and retried.
        if(!automap_backup_done_) {
            const auto status=std::filesystem::symlink_status(automap_path());
            if(status.type()!=std::filesystem::file_type::not_found) {
                if(!std::filesystem::is_regular_file(status))
                    throw std::runtime_error("cannot preserve the existing automap file as a backup: not a regular file");
                std::ifstream file(automap_path(),std::ios::binary);
                std::string bytes(std::istreambuf_iterator<char>(file),{});
                if(file.bad() || bytes.size()!=std::filesystem::file_size(automap_path()))
                    throw std::runtime_error("cannot preserve the existing automap file as a backup: read failed");
                auto backup=automap_path();backup+=".bak";
                automap_writer_(backup,bytes);
            }
            automap_backup_done_=true;
        }
        automap_writer_(automap_path(),text);
        automap_=std::move(next);automap_dirty_=automap_reported_=automap_blocked_=false;++automap_writes_;
        automap_wait_=automap_backoff_=0;
        return {};
    } catch(const std::exception& e) {
        automap_backoff_=automap_backoff_ ? std::min(automap_backoff_*2,3600u) : 60u;
        automap_wait_=automap_backoff_;
        if(automap_reported_)return {};
        automap_reported_=true;
        return "Automap write failed; retrying later and at the next save: "+automap_path().string()+": "+e.what();
    }
}
std::optional<std::string> SaveStore::flush(Memory& memory,bool retry_now) {
    const bool raw=retry_ || memory.cartridge_ram_dirty();
    if (!raw && !automap_dirty_) return {};
    std::optional<std::string> map_error;
    if (automap_dirty_) {
        if (retry_now || !automap_wait_) map_error=flush_automap(memory.cartridge_ram());
        else --automap_wait_;
    }
    if (!raw) return map_error;
    retry_=true;
    try {
        ensure_lock();
        if(!settings_loaded_)load_preferences(load());
        ensure_backup();
        // An unrecognized companion is evidence. Automatic SRAM writes leave
        // it untouched; only the user's explicit Apply may resolve it.
        if(!settings_blocked_)stage(memory.cartridge_ram(),preferences_);
        writer_(path_,memory.cartridge_ram());
        backup_done_=true;
        memory.clear_cartridge_ram_dirty(); retry_=false; ++writes_; return map_error;
    } catch (const std::exception& e) {
        return "Save write failed; retrying next frame: "+path_.string()+": "+e.what()+(map_error ? "\n"+*map_error : std::string());
    }
}
}
