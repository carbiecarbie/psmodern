// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom/rom.h"
#include "rom/sha1.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace psmodern {
RomVersion identify_digest(std::string_view digest) {
    if (digest == "07fcf297be4f4c9d92cd3f119a7ac48467e06838") return RomVersion::us_rev_a;
    if (digest == "fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd") return RomVersion::brazilian;
    throw std::runtime_error("Unknown ROM dump: expected US Rev-A or Brazilian Phantasy Star.");
}
RomVersion identify_rom(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".zip") throw std::runtime_error("ZIP archive given: unzip it and provide the .sms file.");
    if (extension != ".sms") throw std::runtime_error("Expected an uncompressed .sms ROM file.");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw RomUnreadable("Cannot open ROM file.");
    if (file.tellg() != 512 * 1024) throw std::runtime_error("Wrong ROM size: expected 512 KiB.");
    file.seekg(0);
    std::vector<std::uint8_t> data(512 * 1024);
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
        throw RomUnreadable("Cannot read ROM file.");
    return identify_digest(sha1(data));
}
const char* rom_name(RomVersion version) {
    return version == RomVersion::us_rev_a ? "US Rev-A" : "Brazilian";
}
}
