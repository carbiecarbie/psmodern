// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/census.h"
#include <iomanip>
#include <ostream>
#include <sstream>
namespace psmodern::replay {
namespace {
std::string name(RoutineEntry entry) {
    std::ostringstream text;
    text<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(entry.bank)
        <<':'<<std::setw(4)<<entry.address;
    return text.str();
}
}
void write_census(std::ostream& out,const RoutineRegistry& routines) {
    out<<"entry\tunit\tcpp\tfallback\tforced\tforced_failed";
    for(auto reason:routine_fallback_names) out<<'\t'<<reason;
    for(auto reason:routine_fallback_names) out<<"\tforced_"<<reason;
    out<<'\n';
    for(const auto& [entry,routine]:routines.entries()) {
        const auto& counters=routine.counters;
        out<<name(entry)<<'\t'<<name(routine.spec.unit)<<'\t'<<counters.cpp_calls<<'\t'<<counters.fallback_calls
           <<'\t'<<counters.forced_calls<<'\t'<<counters.forced_failed;
        for(auto count:counters.reasons) out<<'\t'<<count;
        for(auto count:counters.forced_reasons) out<<'\t'<<count;
        out<<'\n';
    }
}
std::vector<std::string> checkpoint_problems(const RoutineRegistry& routines,std::span<const RoutineEntry> required) {
    std::vector<std::string> problems;
    auto share_low=[](const RoutineCounters& counters) {
        return counters.cpp_calls*10<(counters.cpp_calls+counters.fallback_calls)*9;
    };
    for(const auto& [entry,routine]:routines.entries()) {
        const auto& counters=routine.counters;
        const auto calls=counters.cpp_calls+counters.fallback_calls;
        if(calls>=min_gated_calls && share_low(counters))
            problems.push_back(name(entry)+": below 90% C++ calls ("+std::to_string(counters.cpp_calls)+" of "+std::to_string(calls)+")");
        if(counters.forced_calls || counters.forced_failed)
            problems.push_back(name(entry)+": forced calls in original mode");
    }
    for(auto entry:required) {
        const auto* routine=routines.find(entry);
        if(!routine) { problems.push_back(name(entry)+": required entry is not registered"); continue; }
        const auto& counters=routine->counters;
        const auto calls=counters.cpp_calls+counters.fallback_calls;
        if(!calls) problems.push_back(name(entry)+": required entry was never called");
        else if(share_low(counters) && calls<min_gated_calls)
            problems.push_back(name(entry)+": below 90% C++ calls ("+std::to_string(counters.cpp_calls)+" of "+std::to_string(calls)+")");
    }
    return problems;
}
}
