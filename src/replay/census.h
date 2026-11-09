// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <iosfwd>
namespace psmodern::replay {
void write_census(std::ostream& out,const RoutineRegistry& routines);
}
