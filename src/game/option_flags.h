// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The two optional-feature flags, shared by psmodern.exe and psmodern_replay.
// Header-only on purpose: no new library target. It sits beside game::Options so that
// the replay runner (below the app) and the app both reach it without an upward include.
#include "game/options.h"
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace psmodern::game {
class OptionFlags {
public:
    // If argv[i] is --encounter-rate or --experience-rate, reads its value
    // (argv[i+1], advancing i) and returns true. Any other argument returns
    // false and changes nothing. A bad flag throws std::runtime_error.
    bool take(int argc,const char* const* argv,int& i) {
        const std::string_view arg=argv[i];
        if (arg=="--encounter-rate")
            return read(argc,argv,i,arg,encounter_given_,game::encounter_percent_choices,options.encounter_percent);
        if (arg=="--experience-rate")
            return read(argc,argv,i,arg,experience_given_,game::experience_percent_choices,options.experience_percent);
        return false;
    }
    // A flag given explicitly with the original value (100) is original mode:
    // it is accepted wherever leaving the flag out is, and changes nothing.
    // Only values that differ from the original are refused by this check.
    // Throws when a flag has a non-default value, naming it and the flag it
    // cannot be combined with.
    void refuse_with(std::string_view other_flag,std::string_view reason) const {
        const auto refuse=[&](std::string_view flag,unsigned value,unsigned original) {
            throw std::runtime_error(std::string(flag)+" "+std::to_string(value)+" cannot be combined with "+
                std::string(other_flag)+": "+std::string(reason)+" (a value of "+
                std::to_string(original)+" is accepted).");
        };
        if (options.encounter_percent!=game::encounter_percent_choices.original)
            refuse("--encounter-rate",options.encounter_percent,game::encounter_percent_choices.original);
        if (options.experience_percent!=game::experience_percent_choices.original)
            refuse("--experience-rate",options.experience_percent,game::experience_percent_choices.original);
    }
    game::Options options;
    OptionOverrides overrides() const {
        OptionOverrides result;
        if(encounter_given_)result.encounter=options.encounter_percent;
        if(experience_given_)result.experience=options.experience_percent;
        return result;
    }
    static std::string usage() {
        return "[--encounter-rate <percent: "+game::encounter_percent_choices.text()+">] [--experience-rate <percent: "+
            game::experience_percent_choices.text()+">]";
    }
private:
    static bool read(int argc,const char* const* argv,int& i,std::string_view flag,bool& given,
                     const game::PercentChoices& choices,unsigned& target) {
        const auto range_text=choices.text();
        if (given) throw std::runtime_error(std::string(flag)+" was given more than once.");
        if (i+1>=argc) throw std::runtime_error(std::string(flag)+" needs a value: a whole number of percent, one of "+range_text+".");
        const std::string_view text=argv[++i];
        const auto shown="\""+std::string(text)+"\"";
        if (text.empty() || text.find_first_not_of("0123456789")!=std::string_view::npos)
            throw std::runtime_error(std::string(flag)+" must be a whole number of percent, one of "+range_text+
                " (digits only, no sign or spaces), got "+shown+".");
        unsigned long long value=0;
        const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
        if (result.ec!=std::errc{} || value>0xffffffffull || !choices.contains(static_cast<unsigned>(value)))
            throw std::runtime_error(std::string(flag)+" must be one of "+range_text+" percent, got "+shown+".");
        target=static_cast<unsigned>(value); given=true;
        return true;
    }
    bool encounter_given_=false, experience_given_=false;
};
}
