// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/widescreen_mode.h"
#include "app/interface_mode.h"
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
    WidescreenMode widescreen=WidescreenMode::off;
    InterfaceMode interface_preference=InterfaceMode::original;
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
// Format 6 adds the interface preference after widescreen. Format 5 allows FILL in the widescreen field added in format 3. The
// unpublished format 4 has a retired switch after the binary widescreen
// field. Formats 1 and 2 have no widescreen field. Writes use the oldest
// format covering all records (2, 3, 5 or 6). At most 1,024 bytes.
SettingsImage parse_settings(std::string_view text);
std::string write_settings(const SettingsImage& image);
}
