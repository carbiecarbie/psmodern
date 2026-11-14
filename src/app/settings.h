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
    bool widescreen=false;   // milestone 10; never reaches register_routines
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
// Format 3 (records "<label> <sha256> <encounter> <experience> <automap>
// <widescreen>", both switches 0 or 1), format 2 (no widescreen field, read as
// off) and format 1 (no automap field either, both off) are read. A file is
// written as format 3 only when some record has widescreen on; otherwise it is
// written as format 2, so release 0.4.0 keeps reading it. At most 1,024 bytes.
SettingsImage parse_settings(std::string_view text);
std::string write_settings(const SettingsImage& image);
}
