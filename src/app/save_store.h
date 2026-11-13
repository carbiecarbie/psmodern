// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/memory.h"
#include "rom/rom.h"
#include "app/settings.h"
#include "app/automap_store.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <memory>
#include <string>

namespace psmodern::app {
const char* save_name(RomVersion version);
inline constexpr const char* shared_save_name="phantasy-star.sav";
// Nonblocking process lock, also used while preserving legacy save sets.
class SaveLock {
public:
    explicit SaveLock(const std::filesystem::path& lock_path);
    ~SaveLock();
    SaveLock(const SaveLock&)=delete;
    SaveLock& operator=(const SaveLock&)=delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::filesystem::path production_save_folder();
void atomic_save(const std::filesystem::path& path,const Memory::CartridgeRam& image);
// Atomically replaces a small text file (settings, remembered ROMs), creating
// its folder; refuses a destination that exists and is not a regular file.
void atomic_text(const std::filesystem::path& path,const std::string& text);
inline void atomic_settings(const std::filesystem::path& path,const std::string& text) { atomic_text(path,text); }
// Reads a regular file of at most limit bytes; throws otherwise. what names
// the file kind in the messages.
std::string read_text_file(const std::filesystem::path& path,std::size_t limit,const char* what);
class SaveStore {
public:
    using Writer=std::function<void(const std::filesystem::path&,const Memory::CartridgeRam&)>;
    using SettingsWriter=std::function<void(const std::filesystem::path&,const std::string&)>;
    SaveStore(std::filesystem::path folder,RomVersion version,Writer writer=atomic_save,SettingsWriter settings_writer=atomic_settings,
              SettingsWriter automap_writer=atomic_text);
    // The shared directory must already have been published by open_shared_save.
    static SaveStore shared(const std::filesystem::path& folder);
    void acquire_lock() const { ensure_lock(); }
    ~SaveStore();
    SaveStore(SaveStore&&) noexcept;
    SaveStore& operator=(SaveStore&&) noexcept;
    SaveStore(const SaveStore&)=delete;
    SaveStore& operator=(const SaveStore&)=delete;
    Memory::CartridgeRam load() const;
    game::Options load_preferences(const Memory::CartridgeRam& image);
    // Apply: stores the routine options and the automap switch (the form
    // without the switch keeps the stored one).
    std::optional<std::string> choose(const game::Options& options,const Memory::CartridgeRam& image);
    std::optional<std::string> choose(const game::Options& options,bool automap,const Memory::CartridgeRam& image);
    const game::Options& preferences() const { return preferences_; }
    bool automap_preference() const { return automap_preference_; }
    const std::optional<std::string>& settings_warning() const { return settings_warning_; }
    std::filesystem::path settings_path() const { auto p=path_;p+=".options";return p; }
    // The automap companion <save>.automap (milestone 7). Only a tracking
    // driver calls these; recording mode and --no-routines never read or write
    // the file. A missing file is an empty image. An unusable one (unsupported
    // version, corrupt, too large, not a regular file) gives a warning and an
    // empty map; at the next save it is kept as .automap.bak before the new
    // file replaces it (a file that cannot be copied is never replaced).
    std::filesystem::path automap_path() const { auto p=path_;p+=".automap";return p; }
    void load_automap();
    const std::optional<std::string>& automap_warning() const { return automap_warning_; }
    // The record of a game block's digest (staged or on disk), or nothing.
    const game::Exploration* automap_record(std::string_view digest);
    // Stages a record (at 00:1D2D); it is written at the next flush, before
    // the cartridge image, with its own retry flag.
    void stage_automap(std::string digest,const game::Exploration& exploration);
    bool automap_pending() const { return automap_dirty_; }
    std::uint64_t automap_writes() const { return automap_writes_; }
    // Failure retains the dirty state and a pending retry, even across power cycle.
    // An unchanged, clean image never creates a file, including at shutdown.
    // A staged automap record is written first (a .automap.bak once per
    // session, then the image pruned to the used slots on disk and in the image
    // being written); its failure is reported once and never prevents the
    // cartridge write. A failed automap write is retried with a backoff: after
    // 60 flushes (one second of frames), then twice as many after each further
    // failure, up to 3,600 (a minute). Staging a new record (the next save) or
    // `retry_now` (exit, power cycle) retries at once.
    std::optional<std::string> flush(Memory& memory,bool retry_now=false);
    const std::filesystem::path& path() const { return path_; }
    std::uint64_t writes() const { return writes_; }
private:
    void ensure_lock() const;
    void ensure_backup();
    void stage(const Memory::CartridgeRam& image,const game::Options& preferences);
    std::optional<std::string> flush_automap(const Memory::CartridgeRam& image);
    mutable std::unique_ptr<SaveLock> lock_;
    std::filesystem::path path_;
    Writer writer_;
    SettingsWriter settings_writer_,automap_writer_;
    SettingsImage settings_;
    game::Options preferences_;
    bool automap_preference_=false;
    AutomapImage automap_;
    std::optional<std::string> automap_warning_;
    bool automap_loaded_=false,automap_blocked_=false,automap_dirty_=false,automap_reported_=false,automap_backup_done_=false;
    std::uint64_t automap_writes_=0;
    unsigned automap_wait_=0,automap_backoff_=0;   // flushes to skip before the next retry; the last backoff
    std::optional<SettingsRecord> session_backup_;
    std::optional<std::string> settings_warning_;
    bool settings_loaded_=false,settings_blocked_=false;
    bool retry_=false, backup_done_=false;
    std::uint64_t writes_=0;
};
}
