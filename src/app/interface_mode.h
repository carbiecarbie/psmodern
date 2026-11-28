// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace psmodern::app {
enum class InterfaceMode { original, enhanced };
enum class ScriptVariant { cartridge, sms_english, sms_literal };

// Only the player's interface preference belongs in the save companion.
constexpr InterfaceMode resolve_interface(InterfaceMode preference,ScriptVariant script,bool recording) {
    if(recording)return InterfaceMode::original;
    return script==ScriptVariant::cartridge ? preference : InterfaceMode::enhanced;
}
}
