// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <optional>

namespace psmodern::game {
// An option given as a percentage of the original: the short list of values it
// accepts and the value that leaves the original behavior untouched.
struct PercentChoices {
    std::array<unsigned,5> values;
    std::size_t count;
    unsigned original;
    constexpr bool contains(unsigned percent) const {
        for (std::size_t i=0;i<count;++i) if (values[i]==percent) return true;
        return false;
    }
    // "0, 25, 50, 100 or 200": the allowed values, for messages.
    std::string text() const {
        std::string out;
        for (std::size_t i=0;i<count;++i) {
            if (i) out+=i+1==count ? " or " : ", ";
            out+=std::to_string(values[i]);
        }
        return out;
    }
};
// Maintainer decision (2026-11-11): fixed values, not ranges.
// Random encounters: 0 turns them off, 25 and 50 reduce them to a quarter and a half, 200 doubles them.
inline constexpr PercentChoices encounter_percent_choices{{0,25,50,100,200},5,100};
// Experience gained: the original reward, or two or four times as much.
inline constexpr PercentChoices experience_percent_choices{{100,200,400},3,100};
// The recommended pace (Maintainer decision, 2026-11-13): a quarter of the
// encounters with four times the experience, as the Switch release's easy
// mode; the in-game selection shows them next to the default (100 for both).
inline constexpr unsigned switch_like_encounter_percent=25;
inline constexpr unsigned switch_like_experience_percent=400;

// Optional features, all off by default (original mode).
struct Options {
    unsigned encounter_percent=encounter_percent_choices.original;
    unsigned experience_percent=experience_percent_choices.original;
    bool operator==(const Options&) const = default;
};
constexpr bool original_mode(const Options& options) { return options==Options{}; }

// Omitted CLI fields inherit the cartridge profile; explicit 100 overrides it.
struct OptionOverrides {
    std::optional<unsigned> encounter,experience;
    OptionOverrides()=default;
    OptionOverrides(const Options& values):encounter(values.encounter_percent),experience(values.experience_percent) {}
    Options apply(Options base) const {
        if(encounter)base.encounter_percent=*encounter;
        if(experience)base.experience_percent=*experience;
        return base;
    }
};

// Throws std::invalid_argument naming the first option that is not an allowed value.
inline void validate(const Options& options) {
    auto check=[](const char* name, unsigned value, const PercentChoices& choices) {
        if (!choices.contains(value)) throw std::invalid_argument(std::string(name)+" must be one of "+
            choices.text()+" percent, got "+std::to_string(value));
    };
    check("Encounter rate",options.encounter_percent,encounter_percent_choices);
    check("Experience rate",options.experience_percent,experience_percent_choices);
}
}
