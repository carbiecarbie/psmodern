// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rom_list.h"
#include "app/save_store.h"
#include <stdexcept>

namespace psmodern::app {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto text=path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
std::filesystem::path from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
bool clean(std::string_view text) {
    for (const unsigned char c:text) if (c<0x20 || c==0x7f) return false;
    return true;
}
}
RomList parse_rom_list(std::string_view text) {
    if (text.size()>rom_list_limit) throw std::runtime_error("ROM list is too large.");
    auto next=[&](std::string_view& line)->bool {
        const auto end=text.find('\n');
        if (end==std::string_view::npos) { if (!text.empty()) throw std::runtime_error("ROM list is truncated."); return false; }
        line=text.substr(0,end);text.remove_prefix(end+1);
        if (!line.empty() && line.back()=='\r') line.remove_suffix(1);
        return true;
    };
    std::string_view line;
    if (!next(line) || line!="psmodern-roms 1") throw std::runtime_error("Unsupported ROM list format or version.");
    RomList result;int last=-1;
    while (next(line)) {
        const auto space=line.find(' ');
        const auto tag=line.substr(0,space);
        const int index=tag=="us" ? 0 : tag=="br" ? 1 : -1;
        if (index<0 || space==std::string_view::npos) throw std::runtime_error("Unknown or malformed ROM list line.");
        if (index<=last) throw std::runtime_error("Duplicate or out-of-order ROM list line.");
        last=index;
        const auto value=line.substr(space+1);
        if (value.empty() || !clean(value)) throw std::runtime_error("Invalid ROM path in list.");
        auto path=from_utf8(value);
        if (!path.is_absolute()) throw std::runtime_error("ROM path in list is not absolute.");
        (index==0 ? result.us : result.br)=std::move(path);
    }
    return result;
}
std::string format_rom_list(const RomList& list) {
    std::string result="psmodern-roms 1\n";
    for (const auto version:{RomVersion::us_rev_a,RomVersion::brazilian}) {
        const auto& path=list.get(version);
        if (!path) continue;
        const auto text=utf8(*path);
        if (text.empty() || !clean(text) || !path->is_absolute()) throw std::runtime_error("Cannot remember this ROM path: "+text);
        result+=(version==RomVersion::us_rev_a ? "us " : "br ")+text+'\n';
    }
    if (result.size()>rom_list_limit) throw std::runtime_error("ROM list is too large.");
    return result;
}
RomListLoad load_rom_list(const std::filesystem::path& folder) {
    const auto path=folder/rom_list_name;
    std::error_code error;
    const auto status=std::filesystem::status(path,error);
    if (status.type()==std::filesystem::file_type::not_found) return {};
    try {
        if (error) throw std::runtime_error(error.message());
        return {parse_rom_list(read_text_file(path,rom_list_limit,"ROM list file")),{}};
    } catch (const std::exception& e) {
        return {{},"Ignoring the remembered ROM list; it will be replaced on the next save: "+utf8(path)+": "+e.what()};
    }
}
void save_rom_list(const std::filesystem::path& folder,const RomList& list) {
    atomic_text(folder/rom_list_name,format_rom_list(list));
}
}
