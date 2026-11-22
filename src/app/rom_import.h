// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/save_store.h"

namespace psmodern::app {
struct ImportedRom {
    CartridgeKind kind;
    std::filesystem::path path;
};

// Owns the two reserved picker staging names under app_folder/roms. Construct
// once at startup to sweep interrupted imports, and for the duration of each
// pick. The platform streams a bounded document to the matching path, closes
// it, then publishes it. Destroying a cancelled pick discards its partial copy.
class RomImport {
public:
    explicit RomImport(const std::filesystem::path& app_folder);
    ~RomImport();
    std::filesystem::path staging_file(std::string_view suffix) const;
    ImportedRom publish(const std::filesystem::path& staged);
private:
    std::filesystem::path folder_;
    SaveLock lock_;
};
}
