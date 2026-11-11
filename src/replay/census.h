// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <iosfwd>
#include <span>
#include <string>
#include <vector>
namespace psmodern::replay {
// One line per registered entry (segment): entry, unit, C++ and fallback calls,
// forced calls and forced failures, the fallback reasons, then the forced reasons.
void write_census(std::ostream& out,const RoutineRegistry& routines);
// The checkpoint rule, for a run in original mode. Returns one text per violation.
// - An entry with at least min_gated_calls calls needs at least 90% C++ calls;
//   entries with fewer calls are not gated.
// - forced and forced_failed must be 0.
// - Each required entry needs at least one call and at least 90% C++ calls.
inline constexpr std::uint64_t min_gated_calls=20;
std::vector<std::string> checkpoint_problems(const RoutineRegistry& routines,std::span<const RoutineEntry> required);
}
