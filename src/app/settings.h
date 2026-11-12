// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/options.h"
#include <optional>
#include <string>
#include <string_view>

namespace psmodern::app {
// One profile of the .options companion, keyed by the SHA-256 of a cartridge
// image. The automap switch sits beside the routine options, never inside
// them: it never reaches register_routines and never makes a change pending.
struct SettingsRecord {
    std::string digest;
    game::Options options;
    bool automap=false;
    bool operator==(const SettingsRecord&) const = default;
};
struct SettingsImage {
    SettingsRecord committed;
    std::optional<SettingsRecord> pending,backup;
    bool operator==(const SettingsImage&) const = default;
    // The record for this digest: pending first, then committed, then backup.
    std::optional<SettingsRecord> select(std::string_view digest) const;
    std::optional<game::Options> matching(std::string_view digest) const;
};
// Format 2 ("psmodern-options 2", records "<label> <sha256> <encounter>
// <experience> <automap>", automap 0 or 1) and format 1 (no automap field,
// read as automap off) are read; files are always written as format 2. At most
// 1,024 bytes.
SettingsImage parse_settings(std::string_view text);
std::string write_settings(const SettingsImage& image);
}
