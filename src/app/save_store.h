// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/memory.h"
#include "rom/rom.h"
#include "app/settings.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <memory>
#include <string>

namespace psmodern::app {
const char* save_name(RomVersion version);
std::filesystem::path production_save_folder();
void atomic_save(const std::filesystem::path& path,const Memory::CartridgeRam& image);
void atomic_settings(const std::filesystem::path& path,const std::string& text);
class SaveStore {
public:
    using Writer=std::function<void(const std::filesystem::path&,const Memory::CartridgeRam&)>;
    using SettingsWriter=std::function<void(const std::filesystem::path&,const std::string&)>;
    SaveStore(std::filesystem::path folder,RomVersion version,Writer writer=atomic_save,SettingsWriter settings_writer=atomic_settings);
    ~SaveStore();
    SaveStore(SaveStore&&) noexcept;
    SaveStore& operator=(SaveStore&&) noexcept;
    SaveStore(const SaveStore&)=delete;
    SaveStore& operator=(const SaveStore&)=delete;
    Memory::CartridgeRam load() const;
    game::Options load_preferences(const Memory::CartridgeRam& image);
    std::optional<std::string> choose(const game::Options& options,const Memory::CartridgeRam& image);
    const game::Options& preferences() const { return preferences_; }
    const std::optional<std::string>& settings_warning() const { return settings_warning_; }
    std::filesystem::path settings_path() const { auto p=path_;p+=".options";return p; }
    // Failure retains the dirty state and a pending retry, even across power cycle.
    // An unchanged, clean image never creates a file, including at shutdown.
    std::optional<std::string> flush(Memory& memory);
    const std::filesystem::path& path() const { return path_; }
    std::uint64_t writes() const { return writes_; }
private:
    struct Lock;
    void ensure_lock() const;
    void ensure_backup();
    void stage(const Memory::CartridgeRam& image,const game::Options& preferences);
    mutable std::unique_ptr<Lock> lock_;
    std::filesystem::path path_;
    Writer writer_;
    SettingsWriter settings_writer_;
    SettingsImage settings_;
    game::Options preferences_;
    std::optional<SettingsRecord> session_backup_;
    std::optional<std::string> settings_warning_;
    bool settings_loaded_=false,settings_blocked_=false;
    bool retry_=false, backup_done_=false;
    std::uint64_t writes_=0;
};
}
