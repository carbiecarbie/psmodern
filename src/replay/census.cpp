// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/census.h"
#include <iomanip>
#include <ostream>
namespace psmodern::replay {
void write_census(std::ostream& out,const RoutineRegistry& routines) {
    out<<"entry\tcpp\tfallback";
    for(auto name:routine_fallback_names) out<<'\t'<<name;
    out<<'\n';
    for(const auto& [entry,routine]:routines.entries()) {
        out<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(2)<<entry.bank
           <<':'<<std::setw(4)<<entry.address<<std::dec<<'\t'<<routine.counters.cpp_calls
           <<'\t'<<routine.counters.fallback_calls;
        for(auto count:routine.counters.reasons) out<<'\t'<<count;
        out<<'\n';
    }
}
}
