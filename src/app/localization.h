// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/language.h"
#include <cstdint>
#include <string_view>

namespace psmodern::app {
enum class Action : std::uint8_t;
enum class PadInput : std::uint8_t;
enum class UiText {
    none, options, encounters, experience_meseta, automap, controls, language,
    on, off_default, default_suffix, recommended_suffix, save_failed, recording_read_only,
    z80_read_only, saved_waiting, saves_on_exit, more_above, more_below, play,
    choose_rom, open_saves, quit, keyboard, gamepad, restore_defaults, save, cancel,
    no_binding, controller_connected, no_controller, press_key_for, press_button_for,
    key_unavailable, swapped_with, key_used_by, button_used_by, needs_key,
    fullscreen_fixed, defaults_unsaved, cleared, map, floor, you, chest, empty,
    up, down, exit, door, locked, magic, pit, automatic, english, portuguese,
    needs_us_rom, needs_br_rom, applies_to_app, selected, choose_language,
    choose_again, back, rom_dialog_title, rom_dialog_filter, warning_title,
    language_fallback, language_file_warning, language_write_failed, save_write_failed,
    cannot_open_saves, cannot_remember_roms, rom_missing_or_changed, rom_unreadable,
    rom_choose_help, rom_refused, power_cycle_disabled, recording, speed, changes_waiting,
    presentation_language, startup_failed, preferences_warning, automap_warning, audio_unavailable,
    legacy_title, legacy_choice, legacy_unavailable, legacy_us, legacy_br, legacy_failed, count
};
// Host-authored labels only. ROM text remains in the player's ROM.
// ROM-rendered labels use uppercase unaccented ASCII supported by RomFont.
std::string_view ui_text(UiText id,Language language=Language::english);
std::string_view language_label(LanguagePreference preference,Language language=Language::english);
std::string_view language_label(Language value,Language language=Language::english);
std::string_view ui_action_label(Action action,Language language=Language::english);
std::string_view ui_key_label(std::uint16_t key,Language language=Language::english);
std::string_view ui_pad_label(PadInput input,Language language=Language::english);
}
