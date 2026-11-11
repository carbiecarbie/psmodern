// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace psmodern {
enum class RomVersion { us_rev_a, brazilian };
// The ROM file exists but could not be opened or read (offline share, unplugged
// drive, access denied). Other identify_rom failures say the file is not a
// supported ROM.
struct RomUnreadable : std::runtime_error { using std::runtime_error::runtime_error; };
// Classify an already computed digest; throws if it is not supported.
RomVersion identify_digest(std::string_view digest);
RomVersion identify_rom(const std::filesystem::path& path);
const char* rom_name(RomVersion version);
}
