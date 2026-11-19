// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_replies(RoutineRegistry& registry);
struct ReplySegment { RoutineEntry entry,unit; };
// The last sixteen rows are joining stretches of milestone 14 (what runs
// between a dialogue or inventory call returning and the next entry); they
// are registered by interaction_handoffs.cpp and interaction_letter.cpp.
inline constexpr std::array<ReplySegment,32> interaction_reply_segments{{
    {{1,0x4B28},{1,0x4AD3}},
    {{1,0x4B06},{1,0x4AD3}},{{1,0x4B1D},{1,0x4AD3}},{{1,0x4B2E},{1,0x4AD3}},{{1,0x4B37},{1,0x4AD3}},
    {{1,0x49D5},{1,0x49B5}},{{1,0x49C6},{1,0x49B5}},{{1,0x49D0},{1,0x49B5}},{{1,0x4A7B},{1,0x4A50}},
    {{1,0x4AAE},{1,0x4AA9}},{{1,0x4AC3},{1,0x4ABE}},
    {{1,0x4949},{1,0x4940}},{{1,0x4F61},{1,0x4F58}},
    {{1,0x4BA6},{1,0x4B9D}},{{1,0x4BAB},{1,0x4B9D}},{{1,0x4BB2},{1,0x4B9D}},
    // interaction_handoffs.cpp
    {{1,0x49C3},{1,0x49B5}},{{1,0x4A5D},{1,0x4A50}},{{1,0x4A60},{1,0x4A50}},{{1,0x4A6D},{1,0x4A50}},
    {{1,0x4CD5},{1,0x4C70}},{{1,0x4CD8},{1,0x4C70}},{{1,0x4D01},{1,0x4C70}},{{1,0x4D48},{1,0x4C70}},
    // interaction_letter.cpp
    {{1,0x4C9C},{1,0x4C70}},{{1,0x4CAB},{1,0x4C70}},{{1,0x4CCE},{1,0x4C70}},{{1,0x4D32},{1,0x4C70}},
    {{1,0x4D3F},{1,0x4C70}},{{1,0x4D53},{1,0x4C70}},{{1,0x4D5B},{1,0x4C70}},{{1,0x4D63},{1,0x4C70}},
}};
}
