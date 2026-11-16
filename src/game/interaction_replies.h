// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>
namespace psmodern::game {
void register_interaction_replies(RoutineRegistry& registry);
struct ReplySegment { RoutineEntry entry,unit; };
inline constexpr std::array<ReplySegment,16> interaction_reply_segments{{
    {{1,0x4B28},{1,0x4AD3}},
    {{1,0x4B06},{1,0x4AD3}},{{1,0x4B1D},{1,0x4AD3}},{{1,0x4B2E},{1,0x4AD3}},{{1,0x4B37},{1,0x4AD3}},
    {{1,0x49D5},{1,0x49B5}},{{1,0x49C6},{1,0x49B5}},{{1,0x49D0},{1,0x49B5}},{{1,0x4A7B},{1,0x4A50}},
    {{1,0x4AAE},{1,0x4AA9}},{{1,0x4AC3},{1,0x4ABE}},
    {{1,0x4949},{1,0x4940}},{{1,0x4F61},{1,0x4F58}},
    {{1,0x4BA6},{1,0x4B9D}},{{1,0x4BAB},{1,0x4B9D}},{{1,0x4BB2},{1,0x4B9D}},
}};
}
