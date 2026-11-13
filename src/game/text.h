// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>

namespace psmodern::game {
inline constexpr RoutineEntry text_indexed_entry{1,0x575a};
inline constexpr RoutineEntry text_output_unit{0,0x33d6};
inline constexpr std::array text_output_entries{RoutineEntry{0,0x33d7},RoutineEntry{0,0x33da},
    RoutineEntry{0,0x33f6},RoutineEntry{0,0x3416},RoutineEntry{0,0x3419},RoutineEntry{0,0x3425}};
inline constexpr RoutineEntry text_stream_unit{0,0x31d4};
inline constexpr std::array text_stream_entries{RoutineEntry{0,0x31fa},RoutineEntry{0,0x3204},RoutineEntry{0,0x320c},RoutineEntry{0,0x3211}};
inline constexpr RoutineEntry text_dictionary_unit{0,0x333e};
inline constexpr RoutineEntry text_name_unit{0,0x336c};
inline constexpr std::array text_dictionary_entries{RoutineEntry{0,0x333e},RoutineEntry{0,0x335a},RoutineEntry{0,0x335f},
    RoutineEntry{0,0x336c},RoutineEntry{0,0x3370},RoutineEntry{0,0x337b}};
inline constexpr std::array text_continuation_entries{RoutineEntry{0,0x31cb},RoutineEntry{0,0x31cf},RoutineEntry{0,0x31d4},RoutineEntry{0,0x31db},
    RoutineEntry{0,0x31ed},RoutineEntry{0,0x31f3},RoutineEntry{0,0x31f4},RoutineEntry{0,0x322c},
    RoutineEntry{0,0x3235},RoutineEntry{0,0x323a},RoutineEntry{0,0x325d},RoutineEntry{0,0x326f},RoutineEntry{0,0x328d}};
inline constexpr RoutineEntry text_number_unit{0,0x3296};
inline constexpr std::array text_number_entries{RoutineEntry{0,0x3296},RoutineEntry{0,0x32ac},RoutineEntry{0,0x32bc},
    RoutineEntry{0,0x32cc},RoutineEntry{0,0x32dd},RoutineEntry{0,0x32ed},RoutineEntry{0,0x32fb},
    RoutineEntry{0,0x3305},RoutineEntry{0,0x3308},RoutineEntry{0,0x331c},RoutineEntry{0,0x3330}};
inline constexpr RoutineEntry text_alternate_unit{0,0x337d};
inline constexpr std::array text_alternate_entries{RoutineEntry{0,0x337d},RoutineEntry{0,0x3388},RoutineEntry{0,0x3389},
    RoutineEntry{0,0x338e},RoutineEntry{0,0x3392},RoutineEntry{0,0x3396},RoutineEntry{0,0x339a},RoutineEntry{0,0x339e},
    RoutineEntry{0,0x33a2},RoutineEntry{0,0x33b2},RoutineEntry{0,0x33b9},RoutineEntry{0,0x33c8},
    RoutineEntry{0,0x33cf},RoutineEntry{0,0x33d4}};
void register_text(RoutineRegistry& registry);
}
