// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/language.h"
#include "app/save_store.h"
#include <stdexcept>

namespace psmodern::app {
LanguagePreference parse_language(std::string_view text) {
    if(text.size()>language_file_limit)throw std::runtime_error("Language file is too large.");
    auto line=[&]() {
        const auto end=text.find('\n');
        if(end==std::string_view::npos)throw std::runtime_error("Language file is truncated.");
        auto value=text.substr(0,end);text.remove_prefix(end+1);
        if(!value.empty() && value.back()=='\r')value.remove_suffix(1);
        return value;
    };
    if(line()!="psmodern-language 1")throw std::runtime_error("Unsupported language file format or version.");
    const auto value=line();
    if(!text.empty())throw std::runtime_error("Unexpected language file content.");
    if(value=="native")return LanguagePreference::native;
    if(value=="en")return LanguagePreference::english;
    if(value=="pt-br")return LanguagePreference::portuguese;
    throw std::runtime_error("Unknown language preference.");
}
std::string format_language(LanguagePreference preference) {
    std::string_view token;
    switch(preference) {
    case LanguagePreference::native: token="native";break;
    case LanguagePreference::english: token="en";break;
    case LanguagePreference::portuguese: token="pt-br";break;
    default: throw std::invalid_argument("Unknown language preference.");
    }
    return "psmodern-language 1\n"+std::string(token)+'\n';
}
LanguageLoad load_language(const std::filesystem::path& folder) {
    const auto path=folder/language_file_name;
    std::error_code error;
    const auto status=std::filesystem::status(path,error);
    if(status.type()==std::filesystem::file_type::not_found)return {};
    try {
        if(error)throw std::runtime_error(error.message());
        return {parse_language(read_text_file(path,language_file_limit,"language file")),{}};
    } catch(const std::exception& e) {
        const auto utf8=path.u8string();
        const std::string name(reinterpret_cast<const char*>(utf8.data()),utf8.size());
        return {LanguagePreference::native,"Using native language; preference file preserved until explicit Save: "+name+": "+e.what()};
    }
}
void save_language(const std::filesystem::path& folder,LanguagePreference preference) {
    atomic_text(folder/language_file_name,format_language(preference));
}
}
