// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rom_import.h"

namespace psmodern::app {
namespace {
std::filesystem::path import_folder(const std::filesystem::path& root) {
    auto folder=std::filesystem::absolute(root)/"roms";
    std::filesystem::create_directories(folder);
    return folder;
}
}
RomImport::RomImport(const std::filesystem::path& app_folder)
    :folder_(import_folder(app_folder)),lock_(folder_/"import.lock") {
    for (auto suffix:{".sms",".zip"}) std::filesystem::remove(staging_file(suffix));
}
RomImport::~RomImport() {
    std::error_code ignored;
    for (auto suffix:{".sms",".zip"}) std::filesystem::remove(staging_file(suffix),ignored);
}
std::filesystem::path RomImport::staging_file(std::string_view suffix) const {
    if (suffix==".sms") return folder_/".import.sms";
    if (suffix==".zip") return folder_/".import.zip";
    throw std::runtime_error("Expected a .sms ROM file.");
}
ImportedRom RomImport::publish(const std::filesystem::path& staged) {
    if (staged!=staging_file(".sms") && staged!=staging_file(".zip"))
        throw std::invalid_argument("Expected an app-local picker staging file.");
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(staged)))
        throw RomUnreadable("Cannot read staged ROM.");
    const auto version=identify_rom(staged);
    const auto path=folder_/(version==RomVersion::us_rev_a ? "phantasy-star-us-rev-a.sms" : "phantasy-star-brazil.sms");
    std::filesystem::rename(staged,path);
#ifndef _WIN32
    sync_directory(folder_);
#endif
    return {version,path};
}
}
