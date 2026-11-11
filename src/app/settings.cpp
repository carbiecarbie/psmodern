// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/settings.h"
#include <sstream>
#include <stdexcept>
#include <charconv>

namespace psmodern::app {
namespace {
void valid(const SettingsRecord& record) {
    if(record.digest.size()!=64 || record.digest.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::runtime_error("Invalid settings SRAM SHA-256.");
    game::validate(record.options);
}
}
std::optional<game::Options> SettingsImage::matching(std::string_view digest) const {
    if(pending && pending->digest==digest)return pending->options;
    if(committed.digest==digest)return committed.options;
    if(backup && backup->digest==digest)return backup->options;
    return {};
}
SettingsImage parse_settings(std::string_view text) {
    if(text.empty() || text.size()>1024)throw std::runtime_error("Invalid settings file size.");
    std::istringstream input{std::string(text)};std::string line;
    if(!std::getline(input,line) || line!="psmodern-options 1")throw std::runtime_error("Unsupported settings format or version.");
    auto record=[&](std::string_view name,bool optional)->std::optional<SettingsRecord> {
        if(!std::getline(input,line))throw std::runtime_error("Incomplete settings file.");
        std::istringstream row(line);std::string label,digest,extra;SettingsRecord result;
        if(!(row>>label>>digest) || label!=name)throw std::runtime_error("Invalid settings record.");
        if(optional && digest=="-") {if(row>>extra)throw std::runtime_error("Invalid empty settings record.");return {};}
        result.digest=digest;
        std::string encounter,experience;
        if(!(row>>encounter>>experience) || (row>>extra))
            throw std::runtime_error("Invalid settings percentages.");
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
    std::string result="psmodern-options 1\n";
    auto record=[&](std::string_view name,const std::optional<SettingsRecord>& value) {
        result+=name;result+=' ';
        if(!value){result+="-\n";return;}
        valid(*value);result+=value->digest+" "+std::to_string(value->options.encounter_percent)+" "+
            std::to_string(value->options.experience_percent)+"\n";
    };
    record("committed",image.committed);record("pending",image.pending);record("backup",image.backup);return result;
}
}
