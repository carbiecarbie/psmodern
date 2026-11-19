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
    // The forced columns are kept for the census format (tools/generate_coverage.py):
    // forced admission no longer exists since milestone 14, so they are 0.
    out<<"entry\tunit\tcpp\tfallback\tforced\tforced_failed";
    for(auto reason:routine_fallback_names) out<<'\t'<<reason;
    for(auto reason:routine_fallback_names) out<<"\tforced_"<<reason;
    out<<'\n';
    for(const auto& [entry,routine]:routines.entries()) {
        const auto& counters=routine.counters;
        out<<name(entry)<<'\t'<<name(routine.spec.unit)<<'\t'<<counters.cpp_calls<<'\t'<<counters.fallback_calls<<"\t0\t0";
        for(auto count:counters.reasons) out<<'\t'<<count;
        for(std::size_t i=0;i<counters.reasons.size();++i) out<<"\t0";
        out<<'\n';
    }
}
void write_transition_census(std::ostream& out,const RoutineRegistry& routines) {
    out<<"section\tkey\tdetail\tcount\n";
    auto kind=[](RoutineExit exit) {
        return exit==RoutineExit::returned ? "returned" : exit==RoutineExit::copy_repeat ? "copy_repeat" :
            exit==RoutineExit::output_repeat ? "output_repeat" : "core_final";
    };
    auto hex=[](unsigned value) {
        std::ostringstream text; text<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(4)<<value; return text.str();
    };
    for(const auto& [entry,routine]:routines.entries()) {
        const auto& c=routine.counters;
        const auto calls=c.cpp_calls+c.fallback_calls;
        if(calls) out<<"exact\t"<<name(entry)<<'\t'<<routine.exact<<'\t'<<calls<<'\n';
        // Bodies that take no interrupt inside them (registered without a
        // register sync), whether called or not.
        if(!routine.exact) out<<"atomic\t"<<name(entry)<<"\t-\t"<<calls<<'\n';
        for(std::size_t i=0;i<routine.rendered.size();++i) if(routine.rendered[i]) {
            const auto exit=i<routine.spec.exits.size() ? routine.spec.exits[i] : SegmentExit{RoutineExit::returned,0};
            out<<"rendered\t"<<name(entry)<<'\t'<<kind(exit.kind)<<' '<<hex(exit.address)<<'\t'<<routine.rendered[i]<<'\n';
        }
        if(calls) out<<"origin\t"<<name(entry)<<'\t'<<(routine.origin.empty() ? std::string_view("-") : routine.origin)<<'\t'<<calls<<'\n';
        for(std::size_t i=0;i<c.reasons.size();++i) if(c.reasons[i])
            out<<"fallback\t"<<name(entry)<<'\t'<<routine_fallback_names[i]<<'\t'<<c.reasons[i]<<'\n';
        if(c.irq_deliveries) out<<"delivery\t"<<name(entry)<<"\tirq\t"<<c.irq_deliveries<<'\n';
        if(c.nmi_deliveries) out<<"delivery\t"<<name(entry)<<"\tnmi\t"<<c.nmi_deliveries<<'\n';
        if(c.abandoned) out<<"abandoned\t"<<name(entry)<<"\t-\t"<<c.abandoned<<'\n';
        if(c.unused_sync) out<<"unused_sync\t"<<name(entry)<<"\t-\t"<<c.unused_sync<<'\n';
    }
    const auto& census=routines.census();
    auto summary=[&](const char* key,std::uint64_t value,const std::string& detail) {
        out<<"summary\t"<<key<<'\t'<<detail<<'\t'<<value<<'\n';
    };
    summary("frames",census.frames,"-"); summary("scaffold_steps",census.scaffold_steps,"-");
    summary("top_irq",census.top_irq,"-"); summary("top_nmi",census.top_nmi,"-");
    summary("nested_irq",census.nested_irq,"-"); summary("nested_nmi",census.nested_nmi,"-");
    summary("body_irq",census.body_irq,"-"); summary("body_nmi",census.body_nmi,"-");
    summary("abandoned",census.abandoned,"-");
    summary("max_step",census.max_step,name(census.max_step_entry));
    summary("max_overshoot",census.max_overshoot,name(census.max_overshoot_entry));
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
    }
    if(routines.census().scaffold_steps && !routines.empty())
        problems.push_back(std::to_string(routines.census().scaffold_steps)+" steps of test scaffolding ran");
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
