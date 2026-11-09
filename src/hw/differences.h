// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace psmodern {
struct FieldDifference {
    std::string field, original, replacement;
    bool can_carry_rom;
};
// Sensitive values are never serialized, even into the in-memory report.
class DifferenceList {
public:
    std::vector<FieldDifference> fields;
    // Defaulted component equality catches future fields omitted from the list.
    // A listed difference already makes the component unequal; avoid duplicates.
    template<class T> void backstop(const std::string& component, const T& a, const T& b) {
        const auto prefix=component+".";
        for (const auto& field:fields) if (field.field.starts_with(prefix)) return;
        if (!(a==b)) fields.push_back({component+" (unlisted field)","","",true});
    }
    template<class T> void add(const std::string& name, T a, T b, bool rom=false) {
        if (a==b) return;
        fields.push_back({name,rom ? "" : value(a),rom ? "" : value(b),rom});
    }
    template<class T, std::size_t N>
    void add(const std::string& name, const std::array<T,N>& a, const std::array<T,N>& b, bool rom=false) {
        if (a==b) return;
        for (std::size_t i=0;i<N;++i) add(name+"["+std::to_string(i)+"]",a[i],b[i],rom);
    }
    template<class T>
    void add(const std::string& name, const std::vector<T>& a, const std::vector<T>& b, bool rom=false) {
        add(name+".size",a.size(),b.size());
        for (std::size_t i=0;i<a.size() && i<b.size();++i) add(name+"["+std::to_string(i)+"]",a[i],b[i],rom);
    }
private:
    template<class T> static std::string value(T v) {
        if constexpr (std::is_enum_v<T>) return value(static_cast<std::underlying_type_t<T>>(v));
        else if constexpr (std::is_arithmetic_v<T>) return std::to_string(v);
        else return v ? value(*v) : "none";
    }
};
}
