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
namespace {
std::optional<CartridgeKind> find_cartridge(std::string_view digest) {
    if (digest == "07fcf297be4f4c9d92cd3f119a7ac48467e06838") return CartridgeKind::us_rev_a;
    if (digest == "fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd") return CartridgeKind::brazilian;
    if (digest == "c9a40ddd217c58dddcd6b5c0fe66c3a50d3e68e4") return CartridgeKind::japanese_sound;
    return {};
}
}
CartridgeKind identify_cartridge_digest(std::string_view digest) {
    if (const auto kind=find_cartridge(digest)) return *kind;
    throw std::runtime_error("Unknown ROM dump: expected US Rev-A, Brazilian or Japanese Phantasy Star.");
}
RomVersion identify_digest(std::string_view digest) {
    const auto kind=find_cartridge(digest);
    if (!kind) throw std::runtime_error("Unknown ROM dump: expected US Rev-A or Brazilian Phantasy Star.");
    if (const auto version=playable_version(*kind)) return *version;
    throw std::runtime_error("The Japanese ROM supplies FM sound only. Start the game with a US Rev-A or Brazilian ROM.");
}
std::vector<std::uint8_t> read_rom_image(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".zip") throw std::runtime_error("ZIP archive given: unzip it and provide the .sms file.");
    if (extension != ".sms") throw std::runtime_error("Expected an uncompressed .sms ROM file.");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path,error)) throw RomUnreadable("Cannot open ROM file.");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw RomUnreadable("Cannot open ROM file.");
    constexpr std::streamoff rom_size=512 * 1024;
    const auto size=file.tellg();
    if (size<0) throw RomUnreadable("Cannot read ROM size.");
    if (size!=rom_size) throw std::runtime_error("Wrong ROM size: expected 512 KiB.");
    file.seekg(0);
    std::vector<std::uint8_t> data(rom_size);
    if (!file.read(reinterpret_cast<char*>(data.data()),rom_size) ||
        file.peek()!=std::char_traits<char>::eof() || file.bad())
        throw RomUnreadable("Cannot read ROM file.");
    return data;
}
CartridgeKind identify_cartridge(const std::filesystem::path& path) {
    return identify_cartridge_digest(sha1(read_rom_image(path)));
}
RomVersion identify_rom(const std::filesystem::path& path) {
    return identify_digest(sha1(read_rom_image(path)));
}
const char* rom_name(RomVersion version) {
    return version == RomVersion::us_rev_a ? "US Rev-A" : "Brazilian";
}
}
