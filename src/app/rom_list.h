// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rom/rom.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace psmodern::app {
// The ROM files the player has picked, one slot per game version. Stored as
// roms.txt in the save folder: the line "psmodern-roms 1", then optional
// "us <path>" and "br <path>" lines in that order (UTF-8, absolute, LF).
struct RomList {
    std::optional<std::filesystem::path> us,br;
    const std::optional<std::filesystem::path>& get(RomVersion version) const { return version==RomVersion::us_rev_a ? us : br; }
    void set(RomVersion version,std::filesystem::path path) { (version==RomVersion::us_rev_a ? us : br)=std::move(path); }
    void clear(RomVersion version) { (version==RomVersion::us_rev_a ? us : br).reset(); }
    bool empty() const { return !us && !br; }
    bool operator==(const RomList&) const = default;
};
inline constexpr const char* rom_list_name="roms.txt";
inline constexpr std::size_t rom_list_limit=8192;
// Throws std::runtime_error on anything but a well-formed list (CRLF accepted).
RomList parse_rom_list(std::string_view text);
std::string format_rom_list(const RomList& list);
struct RomListLoad {
    RomList list;
    std::optional<std::string> warning;
};
// A missing file gives an empty list; an unreadable, oversize, non-regular or
// corrupt one gives an empty list and one warning (the next save replaces it).
RomListLoad load_rom_list(const std::filesystem::path& folder);
// Atomically writes folder/roms.txt, creating the folder; throws on failure.
void save_rom_list(const std::filesystem::path& folder,const RomList& list);

// JP sound sources have a separate file. Older releases continue reading and
// writing the playable list without affecting the remembered sound ROM.
inline constexpr const char* sound_rom_path_name="sound-rom.txt";
struct SoundRomPathLoad {
    std::optional<std::filesystem::path> path;
    std::optional<std::string> warning;
};
SoundRomPathLoad load_sound_rom_path(const std::filesystem::path& folder);
void save_sound_rom_path(const std::filesystem::path& folder,const std::filesystem::path& path);
}
