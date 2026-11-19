// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <iosfwd>
#include <span>
#include <string>
#include <vector>
namespace psmodern::replay {
// One line per registered entry (segment): entry, unit, C++ calls, refused
// calls (each stopped the run), the forced calls and forced failures (always 0
// since milestone 14; kept for the format), the refusals by reason, then the
// forced reasons (0).
void write_census(std::ostream& out,const RoutineRegistry& routines);
// The checkpoint rule, for a run in original mode. Returns one text per violation.
// - An entry with at least min_gated_calls calls needs at least 90% C++ calls;
//   entries with fewer calls are not gated.
// - Each required entry needs at least one call and at least 90% C++ calls.
// - No step of test scaffolding (Machine::set_scaffold_step) ran with routines
//   registered (an authored fixture with none runs entirely on it).
inline constexpr std::uint64_t min_gated_calls=20;
std::vector<std::string> checkpoint_problems(const RoutineRegistry& routines,std::span<const RoutineEntry> required);
// The engine census of milestone 14 as one TSV (section, key, detail, count):
// `rendered` the calls that ended at each declared exit (entry, exit kind and
// address), `origin` the registering source file of each called segment,
// `fallback` refused calls per segment and reason (each stopped the run),
// `delivery` interrupts taken inside each segment's body,
// `exact` the segments whose body takes interrupts itself (1) or not (0) with
// their calls, `atomic` every segment that is not exact with its calls, and
// `summary` the machine-wide figures (acceptances, maximum
// step and overshoot with where they ended).
void write_transition_census(std::ostream& out,const RoutineRegistry& routines);
}
