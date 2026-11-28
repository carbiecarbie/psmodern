// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/settings.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <charconv>

namespace psmodern::app {
namespace {
void valid(const SettingsRecord& record) {
    if(record.digest.size()!=64 || record.digest.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid settings SRAM SHA-256.");
    game::validate(record.options);
    if(record.interface_preference!=InterfaceMode::original && record.interface_preference!=InterfaceMode::enhanced)
        throw std::runtime_error("Invalid interface preference.");
    if(record.widescreen<WidescreenMode::off || record.widescreen>WidescreenMode::fill)
        throw std::runtime_error("Invalid widescreen mode.");
}
}
std::optional<SettingsRecord> SettingsImage::select(std::string_view digest) const {
    if(pending && pending->digest==digest)return pending;
    if(committed.digest==digest)return committed;
    if(backup && backup->digest==digest)return backup;
    return {};
}
std::optional<game::Options> SettingsImage::matching(std::string_view digest) const {
    if(const auto record=select(digest))return record->options;
    return {};
}
SettingsImage parse_settings(std::string_view text) {
    if(text.empty() || text.size()>1024)throw std::runtime_error("Invalid settings file size.");
    std::istringstream input{std::string(text)};std::string line;
    if(!std::getline(input,line) || (line!="psmodern-options 1" && line!="psmodern-options 2" && line!="psmodern-options 3" &&
        line!="psmodern-options 4" && line!="psmodern-options 5" && line!="psmodern-options 6"))
        throw std::runtime_error("Unsupported settings format or version.");
    const int version=line.back()-'0';
    auto record=[&](std::string_view name,bool optional)->std::optional<SettingsRecord> {
        if(!std::getline(input,line))throw std::runtime_error("Incomplete settings file.");
        std::istringstream row(line);std::string label,digest,extra;SettingsRecord result;
        if(!(row>>label>>digest) || label!=name)throw std::runtime_error("Invalid settings record.");
        if(optional && digest=="-") {if(row>>extra)throw std::runtime_error("Invalid empty settings record.");return {};}
        result.digest=digest;
        std::string encounter,experience,automap,widescreen;
        if(!(row>>encounter>>experience))throw std::runtime_error("Invalid settings percentages.");
        if(version>=2) {
            if(!(row>>automap) || (automap!="0" && automap!="1"))throw std::runtime_error("Invalid settings automap token.");
            result.automap=automap=="1";
        }
        if(version>=3) {
            if(!(row>>widescreen) || (widescreen!="0" && widescreen!="1" && !(version>=5 && widescreen=="2")))
                throw std::runtime_error("Invalid settings widescreen token.");
            result.widescreen=static_cast<WidescreenMode>(widescreen[0]-'0');
        }
        if(version==4) {
            // The milestone 15 play-test appended a cartridge-fix switch.
            std::string retired;
            if(!(row>>retired) || (retired!="0" && retired!="1"))
                throw std::runtime_error("Invalid legacy settings switch.");
        }
        if(version>=6) {
            std::string token;
            if(!(row>>token) || (token!="0" && token!="1"))
                throw std::runtime_error("Invalid settings interface token.");
            result.interface_preference=token=="1" ? InterfaceMode::enhanced : InterfaceMode::original;
        }
        if(row>>extra)throw std::runtime_error("Trailing settings record data.");
        auto percent=[](const std::string& text) {
            unsigned value=0;
            if(text.empty() || text.find_first_not_of("0123456789")!=std::string::npos ||
               std::from_chars(text.data(),text.data()+text.size(),value).ec!=std::errc{})
                throw std::runtime_error("Invalid settings percentage token.");
            return value;
        };
        result.options={percent(encounter),percent(experience)};
        valid(result);return result;
    };
    SettingsImage result;result.committed=*record("committed",false);
    result.pending=record("pending",true);result.backup=record("backup",true);
    if(std::getline(input,line))throw std::runtime_error("Trailing settings data.");
    return result;
}
std::string write_settings(const SettingsImage& image) {
    const auto widest=std::max({image.committed.widescreen,
        image.pending ? image.pending->widescreen : WidescreenMode::off,
        image.backup ? image.backup->widescreen : WidescreenMode::off});
    const bool enhanced=image.committed.interface_preference==InterfaceMode::enhanced ||
        (image.pending && image.pending->interface_preference==InterfaceMode::enhanced) ||
        (image.backup && image.backup->interface_preference==InterfaceMode::enhanced);
    const int version=enhanced ? 6 : widest==WidescreenMode::fill ? 5 : uses_wide_canvas(widest) ? 3 : 2;
    std::string result="psmodern-options "+std::to_string(version)+"\n";
    auto record=[&](std::string_view name,const std::optional<SettingsRecord>& value) {
        result+=name;result+=' ';
        if(!value){result+="-\n";return;}
        valid(*value);result+=value->digest+" "+std::to_string(value->options.encounter_percent)+" "+
            std::to_string(value->options.experience_percent)+" "+(value->automap ? "1" : "0");
        if(version>=3)result+=" "+std::to_string(static_cast<int>(value->widescreen));
        if(version==6)result+=value->interface_preference==InterfaceMode::enhanced ? " 1" : " 0";
        result+='\n';
    };
    record("committed",image.committed);record("pending",image.pending);record("backup",image.backup);return result;
}
}
