// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/options.h"
#include <optional>
#include <string>
#include <string_view>

namespace psmodern::app {
struct SettingsRecord {
    std::string digest;
    game::Options options;
    bool operator==(const SettingsRecord&) const = default;
};
struct SettingsImage {
    SettingsRecord committed;
    std::optional<SettingsRecord> pending,backup;
    bool operator==(const SettingsImage&) const = default;
    std::optional<game::Options> matching(std::string_view digest) const;
};
SettingsImage parse_settings(std::string_view text);
std::string write_settings(const SettingsImage& image);
}
