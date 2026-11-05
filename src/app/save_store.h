// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/memory.h"
#include "rom/rom.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace psmodern::app {
const char* save_name(RomVersion version);
std::filesystem::path production_save_folder();
void atomic_save(const std::filesystem::path& path,const Memory::CartridgeRam& image);
class SaveStore {
public:
    using Writer=std::function<void(const std::filesystem::path&,const Memory::CartridgeRam&)>;
    SaveStore(std::filesystem::path folder,RomVersion version,Writer writer=atomic_save);
    Memory::CartridgeRam load() const;
    // Failure retains the dirty state and a pending retry, even across power cycle.
    // An unchanged, clean image never creates a file, including at shutdown.
    std::optional<std::string> flush(Memory& memory);
    const std::filesystem::path& path() const { return path_; }
    std::uint64_t writes() const { return writes_; }
private:
    std::filesystem::path path_;
    Writer writer_;
    bool retry_=false;
    std::uint64_t writes_=0;
};
}
