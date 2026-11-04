// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <string_view>

namespace psmodern {
enum class RomVersion { us_rev_a, brazilian };
// Classify an already computed digest; throws if it is not supported.
RomVersion identify_digest(std::string_view digest);
RomVersion identify_rom(const std::filesystem::path& path);
const char* rom_name(RomVersion version);
}
