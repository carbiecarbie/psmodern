// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/memory.h"
#include "rom/rom.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <memory>
#include <string>

namespace psmodern::app {
const char* save_name(RomVersion version);
std::filesystem::path production_save_folder();
void atomic_save(const std::filesystem::path& path,const Memory::CartridgeRam& image);
class SaveStore {
public:
    using Writer=std::function<void(const std::filesystem::path&,const Memory::CartridgeRam&)>;
    SaveStore(std::filesystem::path folder,RomVersion version,Writer writer=atomic_save);
    ~SaveStore();
    SaveStore(SaveStore&&) noexcept;
    SaveStore& operator=(SaveStore&&) noexcept;
    SaveStore(const SaveStore&)=delete;
    SaveStore& operator=(const SaveStore&)=delete;
    Memory::CartridgeRam load() const;
    // Failure retains the dirty state and a pending retry, even across power cycle.
    // An unchanged, clean image never creates a file, including at shutdown.
    std::optional<std::string> flush(Memory& memory);
    const std::filesystem::path& path() const { return path_; }
    std::uint64_t writes() const { return writes_; }
private:
    struct Lock;
    void ensure_lock() const;
    mutable std::unique_ptr<Lock> lock_;
    std::filesystem::path path_;
    Writer writer_;
    bool retry_=false, backup_done_=false;
    std::uint64_t writes_=0;
};
}
