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
// Strict UTF-8: no overlong forms, surrogates or code points past U+10FFFF.
// Windows refuses such text when converting it to a UTF-16 path; POSIX paths
// are plain bytes and would take it, so every platform checks it here.
bool valid_utf8(std::string_view text) {
    for (std::size_t i=0;i<text.size();) {
        const auto c=static_cast<unsigned char>(text[i]);
        std::size_t length; char32_t code;
        if (c<0x80) { ++i; continue; }
        else if (c>=0xc2 && c<=0xdf) { length=2; code=c&0x1f; }
        else if (c>=0xe0 && c<=0xef) { length=3; code=c&0x0f; }
        else if (c>=0xf0 && c<=0xf4) { length=4; code=c&0x07; }
        else return false;
        if (text.size()-i<length) return false;
        for (std::size_t k=1;k<length;++k) {
            const auto next=static_cast<unsigned char>(text[i+k]);
            if ((next&0xc0)!=0x80) return false;
            code=(code<<6)|(next&0x3f);
        }
        if ((length==3 && code<0x800) || (length==4 && (code<0x10000 || code>0x10ffff)) || (code>=0xd800 && code<=0xdfff)) return false;
        i+=length;
    }
    return true;
}
std::filesystem::path from_utf8(std::string_view text) {
    if (!valid_utf8(text)) throw std::runtime_error("ROM path is not valid UTF-8.");
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
namespace {
constexpr std::string_view sound_rom_header="psmodern-sound-rom 1";
std::filesystem::path parse_sound_rom_path(std::string_view text) {
    auto read_line=[&] {
        const auto end=text.find('\n');
        if (end==std::string_view::npos) throw std::runtime_error("Sound ROM path file is truncated.");
        auto line=text.substr(0,end);
        text.remove_prefix(end+1);
        if (!line.empty() && line.back()=='\r') line.remove_suffix(1);
        return line;
    };
    if (read_line()!=sound_rom_header) throw std::runtime_error("Unsupported sound ROM path file format or version.");
    const auto value=read_line();
    if (!text.empty() || value.empty() || !clean(value)) throw std::runtime_error("Invalid sound ROM path.");
    auto path=from_utf8(value);
    if (!path.is_absolute()) throw std::runtime_error("Sound ROM path is not absolute.");
    return path;
}
}
SoundRomPathLoad load_sound_rom_path(const std::filesystem::path& folder) {
    const auto path=folder/sound_rom_path_name;
    std::error_code error;
    const auto status=std::filesystem::status(path,error);
    if (status.type()==std::filesystem::file_type::not_found) return {};
    try {
        if (error) throw std::runtime_error(error.message());
        return {parse_sound_rom_path(read_text_file(path,rom_list_limit,"sound ROM path file")),{}};
    } catch (const std::exception& e) {
        return {{}, "Ignoring the remembered sound ROM path: "+utf8(path)+": "+e.what()};
    }
}
void save_sound_rom_path(const std::filesystem::path& folder,const std::filesystem::path& path) {
    const auto text=utf8(path);
    if (text.empty() || !clean(text) || !path.is_absolute())
        throw std::runtime_error("Cannot remember this sound ROM path: "+text);
    const auto record=std::string(sound_rom_header)+'\n'+text+'\n';
    if (record.size()>rom_list_limit) throw std::runtime_error("Sound ROM path file is too large.");
    atomic_text(folder/sound_rom_path_name,record);
}
}
