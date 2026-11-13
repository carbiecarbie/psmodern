# Development

Use the pinned UCRT64 toolchain in [dependencies](dependencies.md). In a
UCRT64 shell, from the repository root:

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPSMODERN_ROM_US="/path/to/us-rom.sms" \
  -DPSMODERN_ROM_BR="/path/to/br-rom.sms"
cmake --build build-rel
ctest --test-dir build-rel -j 16 --output-on-failure
./build-rel/psmodern.exe "/path/to/game.sms"
```

`build-rel` uses C++20 and `-O2 -g` with assertions enabled. For other
locations set `PSMODERN_ROM_US` and `PSMODERN_ROM_BR` to the player's own
uncompressed files. `PSMODERN_ROM_TESTS` defaults to `ON`: a missing or empty
ROM path makes its test report CTest's skip code 77; an existing wrong file
fails, including the other version's ROM. Set the option to `OFF` to omit ROM
tests, as CI does. The cache path
`PSMODERN_REFERENCE_DIR` locates external reference manifests. Use
`ctest --test-dir build-rel -L fast --output-on-failure` for fast tests.
Every test that needs a ROM also carries the label `rom`, besides `fast` or
`checkpoint`.
The suite runs in parallel: set `-j` to the machine's logical processor count.
Tests that wait on wall-clock time (`reference_driver`, `rom_unit_cleanup`,
`rom_us_save_process`) carry `RUN_SERIAL`, so CTest runs each alone, and each
recording's `_options` test, which runs five replays at once, counts as five
processors (`PROCESSORS 5`). Serial and parallel runs give the same results
and the same censuses.
See [the complete-suite protocol](../tools/AGENTS.md) before a complete run.

Set the `PSMODERN_Z80_TEST_DIR` cache path to an external directory containing
`zexdoc.com`, `zexall.com`, `z80flags.tap`, `z80memptr.tap`, `z80ccf.tap`,
`woodmass.tap` and `spectrum48.rom` to run the Z80 conformance tests. The
Woodmass tests need both their TAP and the Spectrum 48K firmware. An empty
path or a missing file skips the affected tests; CI supplies the three Rak
TAPs and skips the other external suites. See
[dependencies](dependencies.md#z80-conformance-programs) for provenance and
licenses. These tests use the integrated CPU wrapper with a test bus, not
the game ROM or the upstream standalone harness.

Keep ROMs, emulator captures, traces, saves and other working material outside
the repository. Never commit ROM-derived
data.

For a plain-words introduction to the console parts described below, see
[how the Master System works](how-it-works/master-system.md).
The game-system chapters explain [controller reading](how-it-works/controls.md),
[random numbers and encounters](how-it-works/random-numbers.md),
[party records and experience](how-it-works/party.md), and
[the sound driver](how-it-works/sound.md).

## Program

Run `psmodern.exe <rom-path>` with the player's uncompressed `.sms` file, or
`psmodern.exe` alone for the [start screen](#start-screen).
The program identifies US Rev-A and Brazilian cartridges by SHA-1; other
files are refused. Errors appear on stderr and in an SDL message box.
The window title names the identified version. The 256x192 picture uses
nearest-neighbour integer scaling, centred with black bars in a resizable
window initially 768x576. Alt+Enter toggles borderless desktop fullscreen.

`psmodern.exe --version` prints `PSModern 0.3.0` and exits with 0 before any
window or SDL start-up; combined with any other argument it is refused. The
version comes from `project(PSModern VERSION ...)` in `CMakeLists.txt`,
through the generated header `PSModernVersion.h`.

**Terminal behaviour.** `psmodern.exe` is a Windows GUI-subsystem program, so
a double-click opens no console window. Standard streams that are already
redirected (pipes, files, CTest, Python tests) are kept as they are;
otherwise the program attaches to the parent console. `cmd` and PowerShell do
not wait for a GUI program, so its output appears after the prompt returns
and the exit code is not available at once. To wait and get the exit code use
`start /wait psmodern.exe ...` in `cmd`, or `Start-Process -Wait -PassThru`
(or piping the output, `| Out-Host`) in PowerShell. After attaching to the
terminal's console the program ignores Ctrl+C, so Ctrl+C typed in that
terminal does not stop the game or cut a `--record` short; close its window.
Command-line arguments are read as UTF-16 and converted to UTF-8, so ROM
paths with non-ASCII characters work.

### Start screen

With no arguments the program reads the remembered ROMs and shows the start
screen in the game window, in the original black and white window style with
the ROM's font, so labels are uppercase. Entries: `PLAY PHANTASY STAR (US)`
and `(BR)` (each only if remembered), `CHOOSE ANOTHER ROM`, `CONTROLS`,
`OPEN SAVES FOLDER` and `QUIT`, with the footer `PSMODERN 0.3.0`. The window
title is `PSModern 0.3.0`.

Arrows, the d-pad or the left stick move the selection (it wraps, with the
same key repeat as Options). Z, Enter (without Alt), controller A or Start
accept; Escape or closing the window quits. The start screen never follows
the player's bindings (`start_input` is a fixed table), so `CONTROLS` is always
reachable; it opens the [Controls screen](#controls-screen), and closing that
returns here with the cursor on `CONTROLS`. Alt+Enter, and a controller input
bound to Fullscreen, toggle fullscreen,
and the game that follows uses the same window and keeps the choice. A
button still held from the start screen does not reach the game. Leaving the
game ends the program. A second instance choosing Play is refused by the
save lock.

**Remembered ROMs.** `%APPDATA%\PSModern\roms.txt` holds a header line
`psmodern-roms 1`, then `us <path>` and/or `br <path>`. Delete the file to
reset. On start, each remembered ROM is identified again; missing or changed
files are dropped with one message and the list is rewritten. With nothing
remembered, a Windows Open dialog (`*.sms`) asks for a ROM: cancel exits with
0 and writes nothing; a wrong file shows a message naming the accepted
dumps, with Choose again or Quit (Back from the start screen). Choosing a
ROM of the other version adds it; of the same version replaces it. The file
is written atomically, like the settings companion.

**Hardware documentation.** The [SMS Power palette documentation](https://www.smspower.org/Development/Palette)
describes CRAM as `00BBGGRR`: two bits each for red, green and blue. The
program expands each component to eight bits by multiplying by 85.

## Language

The global Language screen is available from the start screen and Options.
English and Portuguese require their respective identified ROMs. The start
screen has one PLAY entry and ADD ROM. AUTO prefers English when both ROMs
are present, or the language of the sole available ROM. An explicit CLI ROM
sets the session origin for AUTO and restricted modes, independently of saves.

`language.txt`, beside `roms.txt`, contains `psmodern-language 1` followed by
`native`, `en` or `pt-br` on a second line. The bounded parser accepts LF/CRLF;
explicit Save writes canonical LF using atomic replacement. An absent file
means AUTO. An unusable file warns and defaults without being overwritten.
If the preferred ROM is unavailable, the session uses the available ROM
language and retains the preference. Both images are loaded and identified
before play; later language changes perform no file reads of the ROMs.

Language has its own SAVE/CANCEL, like Controls. Its Save does not apply or
discard the parent's encounter/experience/automap draft. A saved language
updates host labels immediately and changes game text after native Options
cleanup. Parent Cancel does not undo that independent Save. A failed language
write leaves the screen open and the previous choice intact. Quitting before
cleanup keeps the saved preference for the next launch. Power cycle remains
unavailable while a host menu is open.

Cartridge RAM, save lock, companion files and automap use one shared store,
regardless of language or available ROMs. Native save and companion formats
are unchanged. Recording and `--no-routines` use the startup ROM and never
write the global language preference; they use the same shared store.

Host labels, window titles and player-facing dialog summaries are available
in both languages. Cartridge-font Portuguese is uppercase without accents;
original game text comes directly from the selected ROM. Configuration tokens,
command-line flags and detailed logs stay English.

## Saves

The machine has no file access. The app loads and stores one shared 8 KiB
image at `%APPDATA%/PSModern/shared/phantasy-star.sav`, with its options and
automap alongside it. A missing file supplies zeroes;
a malformed or unreadable file is refused. A changed image is flushed at the
end of the frame, on host power cycle and on exit. A clean image is not
written. On Windows the app writes an exclusive temporary file in the same
folder, flushes and closes it, then replaces the destination with
`MoveFileExW` and write-through. A failure is reported and retried next frame.

**Legacy import (maintainer decision, 2026-11-13).** Before opening the shared
store, the app holds `shared-import.lock` in the root save folder. Existing
`shared/` storage is authoritative, even when empty; an invalid directory or
raw save is refused, never replaced from legacy files. Otherwise the app
attempts the old US and BR save locks, snapshots each complete six-file set
(raw save, options, automap and their backups), and imports one set. A lone
valid set or two identical sets import automatically. Different sets, or an
unavailable other set, require an explicit one-time choice; Cancel writes no
shared storage. Neither legacy save needs its matching ROM present.

Files are copied byte-for-byte to a unique sibling stage, individually
flushed, then the whole directory is published by a no-clobber atomic rename.
The importer never edits original data or timestamps. Nonregular/unreadable
files, current SRAM other than 8192 bytes, or artifacts over 16 MiB make a
candidate unavailable; readable malformed companions/backups are preserved
for the existing loaders to diagnose. Incomplete stages are ignored. Failed
imports attempt cleanup only of their own stage, with bounded retries for
transient host locks and a warning if it remains; startup never sweeps leftovers.
Migration can create lock files but never copies them. Runtime writes target
only shared storage, including after a ROM is added, removed or switched.
`SaveStore(folder, version)` remains an explicit legacy/fixture API;
production uses `open_shared_save`, returning an already locked shared store.

**Maintainer decision.** Before the first save write of a session, an existing
image is copied atomically to `<save name>.bak`, replacing an older backup.
A failed backup prevents the save write and is reported and retried. A session
holds an operating-system lock for its save; another instance using that save
is refused. The lock is released when the process exits, including after a crash.

The unchanged raw image has a versioned `<save name>.options` companion under
the same lock. It stores committed, optional pending and backup SHA-256/options
records. Before raw publication the app atomically stages the old durable
image association and the candidate association, then publishes SRAM. Startup
matches actual SRAM, preferring pending, committed, then backup. Equal digests
prefer pending: a settings-only Apply commits preferences without writing raw
SRAM. No two-file atomicity or arbitrary power-loss durability is assumed.

Before the first mutation of a session, the companion snapshot is published
to `.options.bak` and any existing raw image to `.sav.bak`. Their operations
are independent. The live companion retains the session-first raw backup's
association through later saves. Restoring raw backup alone recovers its
matching preferences when identifiable; identical SRAM bytes cannot reveal an
older choice, so restoring the matching companion backup restores that choice.
Publication failures keep dirty/retry state; failed companion staging prevents
raw replacement. The absent-file zero image can have saved settings without
creating a raw file.

Missing metadata defaults to original. Malformed, unsupported or unmatched
companions produce a startup diagnostic and original preferences. Automatic
SRAM writes keep that companion byte-for-byte unchanged; explicit Apply may
replace it. Raw saves still work while the unrecognized companion is preserved.
Copying raw SRAM to a cartridge/emulator retains earned totals but carries no
options. Importing a raw image without matching metadata defaults original.

## Automap

The automap (milestone 7) records which dungeon cells the party has seen and
shows a map of the current floor on request. Tracking runs for an identified
ROM with routines on and outside recording mode, even while the Automap option
is off; the option only gates the screen. Recording mode and `--no-routines`
neither track, show a map nor read or write its file.

**Tracking points.** `FrameDriver` owns the dungeon tables (read from the
player's ROM at start, `src/game/dungeon_tables.cpp`) and an
`ExplorationTracker` (`src/game/automap.cpp`). Its existing native-instruction
hook watches four points in bank 0 and reads the machine only with `peek`:

| Point | When | What the tracker does |
| --- | --- | --- |
| `00:0F48` in game mode `$0B` | the dungeon walking wait | samples floor, position, facing and the floor layout at `$CB00` |
| `00:1D2D` | a save has completed | stages the exploration under the SHA-256 of the game block `$C300-$C6FF` |
| `00:06B4` | Continue has copied a slot | restores the record of that block's digest, or an empty map |
| `00:05F4` | a new game starts | clears the map (also at power-on and F5) |

A sample marks the cell stood on and the cells the 3D view drew (cut at the
first wall ahead), a pitfall once stood on and a fake wall once walked
through. The view shows each seen cell's true kind, an unopened chest as soon
as its cell is seen (Maintainer decision, 2026-11-11), an opened chest from
the game's flag byte, and the exit marker only on exit cells that lead to the
world map. The milestone 7 plan, decisions 5 and 6, gives the rules. A
watched instruction must stay native: activating the routines throws
`std::logic_error` when any of the 13 host-observed instructions
(`host_observed_instructions` in `src/app/frame_driver.h`: the Options
boundary, the frame-wait watch, the field waits, the menu cancel, the
tempo-audio points and these four) is a registered entry or a declared exit
address of any segment. A watched address rendered inside a segment body is
caught instead by the passivity tests below, which compare the map's events
with routines on and off.

**The `.automap` file.** `<save name>.automap` beside the `.sav`, under the
same lock, is text: the header `psmodern-automap 1`, then for each saved game
a `record <digest> <n>` line followed by `n` lines
`floor <id> <seen> <found>` (two hex digits, then 32 bytes of each cell set
as hex; byte k holds cells 8k to 8k+7, cell 8k in bit 0). Records are kept
only for the used slots of the cartridge image on disk and of the image being
written, so the file holds at most 10 records and 85,559 bytes; the reader
accepts at most 10 records and 98,304 bytes and refuses anything malformed
whole (`src/app/automap_store.h`). A staged record is written at the next
flush, before `.options` and the `.sav`, so the first `.sav` write after
`00:1D2D` follows it; `.automap.bak` receives the previous file once per
session. A failed write is reported once and never prevents the `.sav`
write; it is retried with a backoff, even with a clean cartridge image: after
60 frames, then twice as long after each further failure, up to a minute, at
once when the next save stages a new record, and at once at exit or power
cycle. A missing file is an empty map. An unusable file (unknown header or
version, corrupt, too large, not a regular file) gives a warning at start and
an empty map at Continue; the next save keeps it as `.automap.bak` and
replaces it, and a file that cannot be kept that way is never replaced
(Maintainer decision, 2026-11-11). Because `.automap.bak` is written once per
session from whatever file is there, a preserved unusable file lasts only
until the first save of the next session, which replaces that backup with the
file it finds then. Two games with byte-identical game blocks share one record.

**Settings format 2.** The `.options` companion is written as
`psmodern-options 2`, each record `<label> <sha256> <encounter> <experience>
<automap>` with automap `0` or `1`. Format 1 files (no automap field) are
read with the map off. Release 0.1.0 refuses a format 2 file with a warning
and plays with the original settings until new ones are applied. An options
file saved with encounters at 25 is likewise refused by release 0.2.0 or older,
which warns, plays with the original settings and keeps the file untouched
until the player applies new options there. The automap
switch sits beside the routine options, never inside them: it never reaches
`register_routines` and never makes a change pending.

**Keys.** The Map binding (M on the keyboard, X on a controller by default) opens the map at the dungeon
walking wait with the option on (`request_menu(HostMenu::map)`); elsewhere,
with the option off, while recording or with `--no-routines`, the press is
ignored and never reaches the game. The game is held exactly as for Options:
the host runs and cancels the game's own field menu, which clears the two
bytes the field menu always clears, as that original action does, so opening
the map is not invisible to the game. The `MapView` is built once when the hold begins and
drawn over the captured picture (`src/app/automap_screen.cpp`), with the floor
label (`FLOOR 2`, `FLOOR B1`) in the title row beside `MAP`, from
`DungeonTables::floor_level`, which counts inside each stack of floors joined
by stairs ([dungeons](how-it-works/dungeons.md#floors-in-stacks-and-the-maps-floor-label)); the Map, Options or Button 2
binding closes it. The Options window has a third row, AUTOMAP OFF (DEFAULT)
or ON, and the window title adds the Map binding (`Map: M / X` by default)
while the option is on.

**Tests.** `automap_unit` and `automap_store_unit` (ROM-free) cover the model
and the file; `rom_<version>_automap_view` the real tables, and
`rom_<version>_automap_mutation_0` and `_1` run the original view drawing
(`01:6AED`) in 400 states with every unseen cell changed, which must leave the
machine identical (states 0-199 and 200-399 of one random stream). The
`psmodern_automap_session` program has five parts:
`rom_<version>_automap_session` (the screen pixels, admission, the hold
against the Options hold, power cycle, Continue, new game and an unusable
file on `first-dungeon`); `rom_us_automap_wrong_map` (one driver, the
console's reset button between phases: a new game after exploring gives an
empty map, a Continue of another recorded game gives exactly its record, a
Continue of a game without a record after exploring gives an empty map, a
dungeon departure forgets the fake-wall sample, and the mode test at
`00:0F48` refuses mode `$2B`); `rom_<version>_automap_passivity_<recording>`
(`first-dungeon` and `deep-dungeon` of each version, US `boss` and the three
US dungeon recordings), in which four drivers play the recording on four
threads: recording mode, the option off, the option on but never opened, and
routines off with a tracker; their machines must be equal after every frame
and at the end, and the three trackers must log the same events (samples with
their values and cycles, stage, Continue, reset, leave) and hold the same
exploration after every frame; and `rom_us_automap_chain_1` to `_3`, which
play the three US dungeon recordings in one save folder in order, each
Continue restoring the record the previous walk saved, and check that an
unopened chest shows on the map before the party reaches it.
`psmodern_automap_session ... frames <folder>` writes inspection frames (PPM)
from the dungeon recordings instead.

## Input

**Bindings.** `src/app/bindings.h` holds the whole model, without SDL: thirteen
actions (the eight console inputs in the order of `Keys`, then Speed, Options,
Map, Power cycle and Fullscreen), each with at most one key (an SDL scancode
from a fixed table of bindable keys) and at most one controller input (an SDL
controller button or one of the two triggers). `Bindings::defaults()` is the
0.2.0 mapping; Alt+Enter is not a binding, so the Fullscreen key slot stays
empty. The assignment rules are pure functions: a key or button already held
by another action swaps with the entry being set, except that an action
without a key cannot take a key of one of the six required ones (up, down,
left, right, button 1, button 2), which also cannot be cleared; Alt, the
Windows keys and keys outside the table are never bindable.
The player's guide is [player-controls](player-controls.md).

**The file.** `controls.txt` sits beside `roms.txt` in the save folder: the
header `psmodern-controls 1`, then `<action> key|pad <name>` lines (names are
SDL's, uppercased, plus `NONE`; the Fullscreen action has no key line), with
`#` comments and blank lines ignored and case not mattering. `parse_bindings`
never throws on content: a line that cannot be applied counts as ignored and
the rest still applies, and an unknown header or a file over 4096 bytes gives
the defaults. `load_bindings` also turns an unreadable or non-regular file into
the defaults, with a warning that goes to stderr only. Lines are applied
through the same assignment rules as the screen, so a duplicate key in a
hand-edited file swaps. `save_bindings` writes atomically, and only the
Controls screen saves.

**Samplers.** `Controller` carries the active bindings and builds the
controller half of an `InputSnapshot` (held inputs, the left stick, whether a
controller is open); the keyboard half is read only while the window has
focus, with Return cleared while an Alt key is held or Alt+Enter consumes it.
`frame_input` turns a snapshot into the console input as before: the bound
keys give the keyboard pad, the bound controller inputs plus the left stick
(dead zone -8000 through +8000 on each signed 16-bit axis) give the controller
pad, and the two are ORed for pad 1; opposite directions are passed through
and pad 2 stays released. `menu_input` serves the Options window and the map
from the direction, Button 1, Button 2, Options and Map bindings. The start
screen and the Controls screen read fixed inputs, so neither can be locked out
by the player's choices. Input is sampled once per emulated frame. The first
available controller is selected, including after hot-plug. Alt+Enter does not
also press Pause, and the Enter press used for fullscreen stays consumed until
Enter is released, even if Alt is released or pressed again first.

**Triggers.** A trigger counts as pressed past half travel
(`trigger_threshold`, 16384 of 0 to 32767). `Controller::translate` latches
each trigger, so a held trigger reports one press; it is released only when
let go below about a quarter of the travel, and a trigger already held when a
controller is connected or the game starts does not count as a press.

**Shortcuts.** Speed, Options, Map, Power cycle and a controller-bound
Fullscreen are read from events in `main.cpp`, one press per event (key
repeats ignored), so the arming rules are unchanged: a release must be observed
before a shortcut arms after focus loss or controller replacement, and Return,
if bound, never presses with Alt or while Alt+Enter consumes it. Power cycle
and Fullscreen do nothing while the Controls screen is open; Power cycle also
does nothing during an Options hold or a recording. The window title is built
from the bindings (`Speed 1X (TAB / RB)`, `Options: ESCAPE / Y`, `Map: M / X`
with the option on, and `(F5 disabled)` while recording), and a hint is left
out for an unbound action.

The Speed binding (Tab or the right shoulder by default) cycles 1x, 2x and 4x
for the current session. Power cycle (F5 by default) returns the choice to 1x; console Reset does not. Recording ignores
the shortcut. The effective speed becomes 1x while unfocused or inside Options,
then restores the chosen speed. A released key or shoulder must be observed
before a new activation after focus loss or controller replacement. The host
runs every emulated frame and drains its sound samples. First acceleration
starts a separate execution of the ROM's sound driver at its next update
boundary. Music and effects keep normal tempo and pitch, including after
returning to 1x; power cycle discards that separate timeline. The audio queue
and resampler stay continuous across speed changes. Sessions that never
accelerate, and recordings, use the original audio path. See the
[sound chapter](how-it-works/sound.md#sound-during-fast-forward) for request
scheduling and guarded fallback. The indicator affects only the
presented picture. The host clock rebases on speed changes; no choice is saved.

The Options binding (Escape or controller Y by default) opens Options during free exploration in towns, on the
world map and in dungeons, when the original field menu is available. It is
unavailable during battles, dialogue, other menus and the title screen; those
presses are discarded. Pause remains the original Pause. Up/Down
selects one of the five rows (the two rates, Automap, CONTROLS and LANGUAGE);
four rows fit, so the list scrolls. Left/Right changes a value.
Button 1 persists the three values and closes;
Button 2 or the Options binding discards the draft. On the CONTROLS row Button
1 opens the [Controls screen](#controls-screen) instead, also in the read-only
modes, because controls never touch the game. LANGUAGE opens the
[Language screen](#language), with its own Save/Cancel; recording and raw-Z80
sessions cannot change language. The screen marks each
100% value as DEFAULT, encounters 25% as RECOMMENDED and experience/meseta
400% as RECOMMENDED; other values have no marker. Only restrictions, pending
activation or a failed save add a status message inside the window. A failed
save keeps the draft open so confirmation can retry.

The screen consumes opening and closing inputs until all gameplay buttons
are released. Focus loss does not count as a physical release: the menu
rearms only after a neutral focused snapshot, and closing-button suppression
survives focus loss. Its black and white window overlays the current scene
and the music continues. The window title shows active rates and saved
changes waiting for a safe game boundary.

The host reads font patterns from the player's immutable ROM using the plane
format loaded by `00:03CD` and the character mapping used at `00:33D6`.
It also reads the original field-menu border and cursor block, using the
original tiles and their flipped corners. Both versions match original-loader
probes. The cartridge's ASCII slots for `%`, `/`, `(` and `)` are blank.
To implement the approved labels, the host composes those symbols at runtime
from the ROM's Z diagonal, C curves and reduced zero rings. These
are composed UI punctuation, not literal glyphs present in the cartridge;
no font pixels or new image assets are stored in the repository. Host drawing
preserves every background pixel outside the window and does not modify
console VRAM or the cartridge save.

The host captures the exploration picture, then runs the original field menu
on the same live machine while drawing Options over that picture. Admission
requires the original exploration frame wait, its map or dungeon caller,
released controls and no movement or pending action. A rejected request does
not survive into a later scene. The original menu keeps gameplay waiting
while interrupts and the sound driver continue normally.

Closing requests the original cancel action and waits for its cleanup before
returning control. The bridge preserves the caller's registers and the input,
sprite and stack fields affected by this additional call; it does not rewind
the CPU, console clock or sound state. Gameplay controls, Pause, Reset and Power cycle
are consumed while the bridge is active, including cleanup after the visible
options window closes. Quit remains available.

### Controls screen

`ControlsScreen` (`src/app/controls_screen.cpp`) edits a draft copy of the
bindings and draws the 13 actions with a keyboard and a controller column,
then RESTORE DEFAULTS, SAVE and CANCEL, in the Options window style. Seven
rows are visible and the list scrolls, with the title, header and status line
fixed. It reads the raw snapshot, never the bindings, and answers fixed
navigation: arrows, D-pad and left stick move; Enter, Z and A choose;
Backspace, Delete and X clear; Escape, X and B leave and discard.

Choosing a cell starts a capture: the first new key (the lowest scancode if
several arrive together) or controller button on the cell's own device is the
answer; only fresh presses count, and the capture waits only for the choosing
accept to be released. A press on the other device, five
seconds without an answer or losing focus ends it with no change. Alt and the
Windows keys are never an answer (silently ignored), nor is Return while Alt
is held; a key outside the table shows `KEY NOT AVAILABLE`. The status
line shows the controller state, the capture prompt or, for two seconds, the
last result (`SWAPPED WITH`, `KEY USED BY`, `NEEDS A KEY`, `ALT ENTER IS
FIXED`, `KEY NOT AVAILABLE`, `CLEARED`); `DEFAULTS - NOT SAVED YET` and `SAVE
FAILED - RETRY` stay until the next press. Unlike the Options window the screen
does not wait for a fully neutral snapshot: each fixed navigation source held
when it opens, when focus returns or when a capture ends is ignored until that
source alone is released, and a blocked source never hides another source of
the same meaning. Every other held input has no effect on it.

`main.cpp` and `start_loop.cpp` own the saving: on SAVE they write the file and
pass the draft to `Controller::set_bindings`, so the change applies at once,
and a failed write keeps the screen open. Opened from Options it returns
there, the game held by the Options bridge throughout; opened from the start
screen it returns to the start screen.

## Audio and pacing

**Inference.** The host uses a high-resolution wall clock at the machine rate,
3,579,545 / 59,736 frames per second, independent of display refresh. It runs
whole frames, at most three per iteration at 1x, six at 2x and twelve at 4x.
When further behind it rebases the
wall deadline; no machine frame is skipped or duplicated. The SDL renderer
requests accelerated presentation with vsync and falls back to the previous
accelerated renderer, then software. Vsync governs presentation only: a
picture can be dropped when multiple machine frames are due, while the host
still executes each of those frames. The loop yields with a 1 ms SDL delay
when waiting for the deadline. Minimising suppresses presentation but not
emulation or audio.

**Inference.** The resampler averages each output period of the internal
223,721.5625 Hz mono stream, including fractional endpoints, retaining its
fractional state across frames. This box filter is the minimal averaging
filter for downsampling; it attenuates high-frequency aliases but is not an
ideal brick-wall filter. The preferred device rate is 48 kHz. Queue feedback
adjusts the resampling ratio by at most ±0.5%, targeting 75 ms. The queue is
primed with silence; an empty queue triggers a refill and a queue above 150 ms
is cleared and refilled. A power cycle also resets it. Device removal closes
the device and retries the default device at most once per second; emulation
continues without sound while it is unavailable. Queue diagnostics are sampled
before adding a frame and their range is cumulative for the run.

**Unknown.** The cause of rare sound stalls of about 90 ms, seen in one of
three measured host runs.

## Input recordings

Recordings are ASCII with LF line endings, including the last line. Version 1
is canonical: exactly one space between fields, lowercase 40-digit SHA-1s,
unsigned decimal frame numbers without leading zeroes, no comments or blank
lines. The first three lines are the format/version, ROM identification digest
and SHA-1 of the initial 8192-byte cartridge RAM image. For example:

```text
psmodern-recording 1
rom 07fcf297be4f4c9d92cd3f119a7ac48467e06838
cart-ram 0631457264ff7f8d5fb1edc2c0211992a67c73e6
input 1 p1=1 p2=- pause=0 reset=0
input 2 p1=- p2=- pause=0 reset=0
end 12
```

The implicit initial input is released. Each `input` line replaces both pads,
Pause and Reset. Pads use `UDLR12` in that order, omitting released buttons;
`-` means all released. Opposite directions are permitted. Pause and Reset
are `0` or `1`. Only changes appear, in strictly increasing call order.
`end` gives the total number of calls; changes must precede that count.
Zero calls are valid for a session closed immediately. Frame counts fit uint64.
Unknown keys/buttons, wrong versions, redundant changes, missing headers/end,
CR, noncanonical spacing/numbers and trailing content fail with a line number.
Parsing and writing a valid recording preserves every byte.

**Frame mapping.** `input N` is the exact argument supplied to the zero-based
Nth call of `Machine::run_frame(next_input)`, not an SDL presentation number.
The argument prepares the latch at the first cycle of line 0 where that call
ends. On power-on, argument zero also supplies the initial cycle-zero latch.
Consequently argument zero governs video intervals zero and one; argument N
for N > 0 governs interval N+1. A change at input 600 is first observable by
software during interval 601, in manifest frame 602. The final argument is
still recorded even when no later interval runs. This contract preserves the
existing app frame driver and is shared by the headless and reference drivers.
**Maintainer decision.** Keep this one-frame mapping: the reference uses the
same mapping, existing recordings depend on it, and one frame is not noticeable
when playing.

## Record mode

Run `psmodern.exe <rom-path> --record <new-file>`. The plain command is unchanged.
Recording starts at power-on with the app's normal save, whose digest enters
the header. Preserve a copy of that save before starting if it is nonzero:
the app can update the normal save during the session, and a hash cannot
reconstruct its initial contents. Power cycle (F5 by default) is disabled while recording and the title
says so; console Reset remains available. Every completed emulated call is
counted, including catch-up calls and calls while minimized.

The destination must not exist and its parent must already exist. On normal
exit, the complete recording is written to an exclusive temporary file in the
same folder, flushed, closed and atomically published without replacing an
existing destination. A crash before publication leaves no destination. A
failed publication reports an error; a partial destination is never published.

## Replay runner

```text
psmodern_replay <rom> <recording> --dump <selection> --out <new-folder> [--cart-ram <file>] [--no-routines] [--lockstep-calls] [--compare-recording]
                      [--encounter-rate <0|25|50|100|200>] [--experience-rate <100|200|400>]
```

The runner links no SDL and reads no app save. It checks the ROM and initial
cartridge RAM SHA-1s before creating output. Missing `--cart-ram` supplies an
8192-byte zero image; a supplied image must have exactly that size. The output
folder must not exist. Selection is `all` or comma-separated one-based output
frame numbers/ranges, such as `1,60-65,1200`, all within the recording.
The two rate flags (see [Options](#options)) run the recording with an optional
feature on: the game then differs from the recording, so the runner writes
manifests and the census without comparing to anything.

Manifest frame K is the boundary after call K-1, at the first complete CPU
instruction boundary at or after K*59736 cycles. The picture is the completed
256x192 image from the preceding video interval. `manifest.txt` starts with
`frame picture ram vram cram cart-ram cpu psg`, followed by a decimal frame
and seven SHA-1s on every line. Selected dumps are `frame-000001.picture.bin`,
`.ram.bin`, `.vram.bin`, `.cram.bin`, `.cart-ram.bin`, `.cpu.bin` and
`.psg.bin`. The first five contain 49152 color bytes, 8192 work RAM bytes
(C000-DFFF), 16384 VRAM bytes, 32 CRAM bytes and 8192 cartridge RAM bytes,
respectively. Colors retain six-bit SMS CRAM values, not host RGB.
**Maintainer decision.** CPU captures contain AF, BC, DE, HL and their alternate
pairs, IX, IY, SP and PC as big-endian pairs, then I, R (including bit 7),
IFF1, IFF2, interrupt mode and HALT as bytes. PSG captures contain ordered
three-byte events: a big-endian cycle offset within the video interval and the
written byte. Writes belong to the interval containing their absolute cycle,
even when an instruction crosses a frame boundary.
`psg-writes.txt` holds the entire run as decimal absolute cycle and two-digit
hex byte. Both PSG write and sample queues are drained after every call.

## Registered routines

Routines are enabled by default. `psmodern.exe <rom-path> --no-routines`
and `psmodern_replay ... --no-routines` disable every replacement. The app
option can be combined with `--record`; the runner option can be combined
with `--cart-ram`. The routines are shared by the US Rev-A and Brazilian
images: controller polling (`00:0339`, a fixed 70-cycle bound; see the
[controller chapter](how-it-works/controls.md)), the random-number routine
(`00:05B1`), the map encounter check (`01:5F63`), the monster-group choice
(`01:5FD8`), the dungeon game-mode handler that holds its own check
(`00:0F3C`) and the experience award (`00:170D`) with its per-character helper
(`00:1754`). The last five are registered as several segments each (see
Segments below); the
[random numbers and encounters](how-it-works/random-numbers.md) and
[party](how-it-works/party.md) chapters explain them. Milestone 7 adds dungeon
walking as 53 segments in eleven units: the dungeon input handler
(`01:65EE`), the pitfall (`01:661C`), the forward step (`01:6758`), the step
back (`01:6792`), the turns (`01:67B7`, `01:67E2`), opening a door
(`01:681B`), the exit lookup (`01:68BC`), the object check (`01:6963`) and the
two cell readers (`01:6BCA`, `01:6BE9`), in `src/game/dungeon_cells.cpp`,
`dungeon_movement.cpp` and `dungeon_objects.cpp` with their shared helpers in
`dungeon_kit.h`; they use the held-word, conditional and loop exits below, and
the [dungeons chapter](how-it-works/dungeons.md) explains them. Their state
probes are `rom_<version>_dungeon_cells`, `_dungeon_tables`,
`_dungeon_movement` and `_dungeon_objects`. Registration is gated by
the verified ROM SHA-1: authored runner fixtures keep an empty registry. The
game layer lives in `src/game/`.

Every replay writes `routine-census.tsv` beside its manifest, one line per
registered entry. The columns are `entry`, `unit` (the routine the entry
belongs to), `cpp`, `fallback`, `forced`, `forced_failed`, then the eight
fallback reasons, then the same eight reasons prefixed with `forced_`. An empty
registry writes just the header. Counts contain no ROM bytes. A dungeon segment
refused because its stack does not give back a pushed return address (stack
bytes in ROM, reached only by the state probes) is filed under the `frame`
reason, since its bound is set above one frame; such refusals never occur in a
recording.

The fifteen checkpoint tests (six per version and the three US dungeon
recordings) check every admitted C++ call, compare
routines on/off frame by frame, and compare the resulting frames with the
pinned milestone 1 reference. In each checkpoint an entry with at least 20
calls needs at least 90% C++ calls; entries with fewer calls are reported, not
gated. `forced` and `forced_failed` must be 0 there, since the checkpoints run
in original mode. The controller must be called in every checkpoint and keeps
the 90% share whatever its call count. The tests write counts to
`build-rel/routine-census/{us,br}/`. Generate [the coverage chapter](coverage.md)
after all fifteen pass:

```sh
python tools/generate_coverage.py build-rel/routine-census --out docs/coverage.md
```

The generator groups segments under their unit and applies the same 90% rule
to every entry with at least 20 calls, over the fifteen censuses together and in
each census. An entry with at least one call needs at least one C++ call,
unless it is on the explicit allow-list in the generator, which carries a
written reason per entry and is empty now. An allow-listed entry that has C++
calls, or that no recording calls, is stale and an error. The generator also
refuses forced calls: the censuses run in original mode. Entries no
recording reaches are listed as covered by state probes only.

**Controls tests.** `bindings_unit` (ROM- and SDL-free) covers the names,
the defaults, the swap and refusal rules, the `controls.txt` round trip with
damaged and partial files, and remapped input. Its equivalence proof compares
the default bindings with the 0.2.0 tables, kept in the test, over every state
of the default keys and controller inputs at a range of stick values, with and
without focus and a controller, and shows that inputs outside the default
tables never leak. `sdl_input` repeats the proof through the real samplers
(every state of the default keys, Alt, focus and a consumed Return) and, with
SDL's virtual controller, covers remapped buttons and the triggers.
`controls_unit` covers the Controls screen without SDL (navigation, scrolling,
capture, messages, commands, arming); `rom_<region>_controls_screen` also draws
the screen with the cartridge font, checks the pixels and writes frames for
inspection.

The ROM-only controller tests compare all 65,536 pairs of previous/new
controller bytes in each version, including the whole machine, read timing,
R and fetch count; the random, encounter and experience tests probe their
segments against the original from many machine states. The ROM-free catalog
and coverage-generator tests run in CI.

Besides the fifteen checkpoints, every checkpoint recording has an `_options` test
(`rom_us_<recording>_options`, `rom_br_<recording>_options`, label
`checkpoint`; the script is `tests/replay_rom_tests.py <runner> <rom> <recording>
options <reference-folder> [full|identity-only] [marker-file]`). Each test runs
the replay runner without flags and with `--encounter-rate 100
--experience-rate 100` and requires identical manifests, sound log, census and
frame dump, and no forced call in the original-mode census. It then runs three
enhanced passes (`--encounter-rate 50 --experience-rate 400`,
`--encounter-rate 0 --experience-rate 200` and `--experience-rate 400`). The
first two play differently from the recording by design, so only the census is
read: the run completes without a runtime guard, every option segment whose
option is on that the run reaches has no fallback, no failed forced call and
`forced` equal to its call count, and the segment of an option left at 100 is
never forced. The third leaves encounters alone, so the play stays the
original's until the first award, and the experience segments must be reached
wherever the original reached them. The recording is not compared in these
passes. In the rate-0 pass the random-fight marker segments (`01:5FD8`,
`01:5FEC`, `00:0F62`, `00:0F6D`: the group choice, and what follows the
dungeon cell test) must have no calls, and each compare segment (`00:0F5B`,
`01:5FB0`) that the original-mode run reached must be reached at least once
(not as often: the play diverges, so the census cannot say how many compares
the original needed up to its first passing one). To show that the zero-marker
check can fail, each test records the marker calls of its original-mode run,
and `rom_us_options_fights` and `rom_br_options_fights` require at least one
recording of the version with a nonzero count (the `first-battle` and
`deep-dungeon` recordings have one). A random fight needs a draw below the
threshold at one of the two compares, then the group choice on the map or
after the cell test in a dungeon; a fixed fight does not pass through them
(Inference). At recording level the dungeon side proves only that the first
compares run and no group choice follows with encounters off, because the
play diverges soon after; the state probes cover the compare and the
dungeon segments exhaustively. All fifteen recordings take about 7 minutes
of suite time.

`RoutineRegistry` is owned by the frame driver or runner and passed to
`Machine::run_frame`. Tests can pass it to `Machine::step` directly. It keys
entries by mapped ROM bank and CPU address; cartridge RAM, work RAM and
unmapped entry accesses never dispatch. The registry, enable flag and counters are
outside the machine snapshot and `Machine::equivalent`.

Each entry supplies a bound function of the const entry machine and a body
taking `RoutineContext`. The bound includes the original returning
instruction. Dispatch skips a registered entry only when the core actually
accepts an interrupt at that boundary; this starts no call and changes no
routine counter. With INT asserted and IFF1 clear, C++ can run. During the
one-instruction EI delay, the entry reaches the window check and falls back
with the `ei_delay` reason. A later return from an interrupt handler can
then dispatch the call normally.

The acceptance predicate follows the pinned
`third_party/redcode-z80/sources/Z80.c`: lines 2380-2383 gate creation of the
INT request on IFF1 and clear it when the line is deasserted; lines 2443-2492
handle resumed work; lines 2505-2507 enter request processing; lines
2560-2582 give the NMI-rejection marker precedence over NMI acceptance;
lines 2605-2618 apply the prior-instruction delay conditions for INT.
The request used here is the preceding instruction's E-1 sample, including
the existing handling of an NMI first visible at entry.

Otherwise dispatch checks the half-open interval `[entry, entry+bound)`.
An enabled maskable interrupt source, any NMI request (including one first
visible at entry), or a frame boundary in that interval forces the original
path. The maskable check requires IFF1; the window is deliberately checked
even beyond the body's actual end. The VDP query uses arithmetic on the
counter and source registers, with an invalidated cache, instead of copying
and simulating the VDP. It is conservative relative to `Vdp::next_irq`.

The context advances devices through `Machine::advance`. Port offsets are
relative to the call's entry, and must be the original instruction's T3
offset. Advancing to an offset spends those cycles; advance the remaining
cycles afterward. Bus reads/writes must follow the original order relative
to ports: `read`/`write` update the CPU open bus, and `fetch_opcode` also
accounts for one fetch and R. `account_fetches` can account for a known
fetch count separately from bus accesses; it preserves R bit 7. Authors
must reproduce all architectural register and flag effects.

End with `finish_return(address)` at the path's own returning instruction.
The core executes that instruction, including any ignored index prefixes,
and must restore the caller's stack and PC. Conditional returns must take
their returning path. The step ends there. Missing returns, cost overruns
(including the core return), writes to mapper addresses or memory-control
ports, and VDP register 0, 1 or 10 commands are runtime errors in normal
play too. VDP guards decode the actual latch, including a half-command at
entry. Other VDP commands and I/O control remain available. A segment
may end with another kind of final instruction; see Segments below.

Per-entry counters record C++ calls, fallback calls and reasons: disabled,
IRQ, NMI, frame, `ei_delay`, `interrupt_delay` (the core's NMI-rejection
marker), `halted` and `unmapped_stack`. An unmapped stack byte
has no stable entry return address, so that call stays native. A halted core
at a complete boundary also stays native, with its reason counted. Resumed
prefix/HALT work is inside an already-started instruction and cannot start
a new registered call, even when PC still points at its prefix. It bypasses
dispatch without counting the same call again. Several window reasons may
apply to one fallback;
interrupt acceptance itself does not start a routine call. Existing hooks
retain their native-path redirection behavior and are not called for a
successful replacement.

The runner has two independent verification options, both off by default:

- `--lockstep-calls` copies the machine at each admitted C++ call. A const
  `Machine::peek` reads the stack return address without bus callbacks or
  open-bus updates. The copy runs with routines off until the first complete
  instruction boundary with entry SP plus two and that PC. No further
  instruction starts at or beyond the declared bound; a native instruction
  that ends past it, or a call still unfinished at it, reports
  `understated bound`. An accepted interrupt or a frame boundary inside the
  window reports `window-check failure`. A boundary exactly at the final
  return cycle is outside the window. The real C++ call then runs once and
  its complete machine state is compared. The first failure exits nonzero.
- `--compare-recording` runs a second machine from the same recording and
  initial cartridge RAM with routines off. It compares the seven manifest
  components (`picture`, `ram`, `vram`, `cram`, `cart-ram`, `cpu`, `psg`) at
  every frame, stopping at the first differing frame and component. It
  requires routines enabled and can be combined with `--lockstep-calls`.

Both options preserve the real machine, timing, manifest and output files.
With call verification off, there are no verification copies or extra
per-instruction checks; the verifier pointer is checked only at C++ calls.
For example:

```sh
psmodern_replay game.sms input.psrec --dump 1 --out replay-output --lockstep-calls --compare-recording
```

`Machine::differences(other)` returns the field name, original/replacement
values and ROM-sensitivity flag for every difference; `equivalent` is
exactly an empty difference list. Array and queue elements have indexed
field names. CPU equivalence uses the same field comparisons. The derived
VDP IRQ cache, host bindings/instrumentation and diagnostic `last_access`
probe retain their existing exclusions.

A call report begins with `Divergence: recording <name>, frame <number>,
cycle <entry-cycle>, routine <bank:address>, call <number>`. Frames are
one-based; cycles are absolute. The call number is the per-entry sum of
C++ and fallback calls, including the failing call. Each following line
names a differing field and gives `original=<value>, replacement=<value>`.
Fields that can carry ROM bytes instead say `different (values redacted)`;
their values are not serialized even into the difference list. These
include open bus, core opcode/data bytes, registers holding read results
(including I and R/R7), byte control/scroll latches, memory/picture/pipeline
bytes and sound data. Flags are reported
separately from the accumulator. Reports contain no ROM data or decoded
instructions. A recording report is `Recording divergence: recording
<name>, frame <number>, component <name>` and prints no component contents.

Two narrowly defined scratch exclusions apply to CPU equivalence. After
an unprefixed instruction with no suspended operation, `data[1..3]` are
stale: the pinned `third_party/redcode-z80/sources/Z80.c:2962` replaces the
first byte before dispatch, lines 2073, 2080 and 2489 replace the second
before prefix dispatch, line 2113 replaces the fourth before using it, and
line 1142 replaces the third before its interrupt-delay use at lines 2614–2616.
All four bytes remain compared at prefixes and resumptions; the first is
always compared. `xy` is a temporary loaded before use at lines 2094, 2488
and 2834; lines 2144–2148 merely save and restore an otherwise unused old
value around another index load. It is excluded. `scratch_overwrite` in
`tests/routine_tests.cpp` perturbs these fields, executes each prefix
family, checks overwrites and verifies subsequent equivalence. MEMPTR, Q,
R/R7, bookkeeping and every other previously compared field remain checked.

`lockstep_*` tests cover clean calls and recordings, dispatch, window failures,
field comparison and deliberate faults in cycles, R, fetch count, flags,
MEMPTR, Q, RAM, VDP/PSG timing, the control latch, open bus and the bound.
These tests use authored programs and need no game ROM.

`lockstep_segments` also verifies held-word chains, conditional final jumps
(every condition, taken and untaken, also ending at a frame boundary), split
table scans (clean, one record late, one early, and crossing a frame boundary
against a run without routines) and the return rules for inherited words.

`routine_unit` covers synthetic replacements, E-1 window edges, guards,
mapping, counters, scratch overwrites and the held-word, conditional and
loop exits with their refusals; `routine_irq_query` compares the
cheap query against the exact query across counter, register, pending-flag
and scanline states. Neither test needs a ROM.

### Segments

A routine can be registered as several segments instead of one. Each segment
has its own entry (a mapped bank and address, as above) and its own bound and
body, and belongs to a unit: the routine it is part of. A routine registered
the old way, as one body ending in a return, is a segment whose unit is its own
entry.

A segment ends with its last instruction executed by the core, through
`finish_return` or `finish_core`. The declared exits say which instruction that
may be:

- a return, executed by the core as before;
- a call, a conditional call or a jump into original code, or the text
  renderer's unprefixed `RST 08`. The restart requires ordinary admission
  and a declared scratch stack covering its two pushed bytes in addition to
  any held words; both addresses must be mapped and off the mapper registers.
  Other restart vectors remain refused. The
  core takes or skips it, and the callee then runs wherever it lives, in the
  original or in another registered segment;
- a plain instruction with no port access, no memory write and no control
  transfer, which is not a halt, an interrupt enable or an interrupt disable. The segment ends there and
  the next instruction starts a new dispatch;
- the original `LD (nn),HL`, only in a single-instruction segment with
  `absolute_wram_store` declaring that exact destination. Both bytes must be
  canonical work RAM and currently mapped as RAM. No other final store is
  permitted.

The stack and the program counter are checked after that step against the
declared exit, and the cost bound includes the step. C++ never fakes a step of
the core, so cycles, the refresh register, interrupt sampling and the frame
boundary come out exactly as on the Z80.

Bare calls and returns between segments stay on the Z80: they need no
rewriting, they are not registered, and the unit still counts as rewritten.

Three permissions exist, declared per segment and off by default.
`slot2_mapper` allows a write to `$FFFF` (the slot-2 bank register). Writes to
the other mapper addresses stay runtime errors. `scratch_stack` names a number
of bytes below the stack pointer that the body may use for its own pushes. They
go through the bus like any write, so a stack in ROM or cartridge RAM behaves as
on the original; dispatch falls back when those bytes are unmapped or fall on
the mapper registers. `absolute_wram_store` permits only the final store above,
to a declared pair wholly within `$C000-$DFFF`; mirrors, mapper registers,
cartridge RAM and other destinations are refused. The core performs the actual
write. If RAM is disabled, the enhanced forced body is rejected before its
step, its register adjustments are undone, and the unchanged original
instruction runs natively.

A single-instruction segment is exactly one core step with register adjustments
before and after it. It may be `force_admit`: its window reasons (interrupt,
NMI, frame boundary, `ei_delay`, unmapped stack) never cause a fallback, because
the core runs the instruction itself and the segment adds no delay. Such calls
are counted as `forced` (and also as C++ calls), with the reasons they ran
through in the `forced_` columns. A halted core still causes forced fallback.
The NMI-rejection marker does too, except for the declared absolute work-RAM
store: the core clears that marker and executes the validated store itself.
These hard fallbacks count `forced_failed`, and so does a
forced body the context rejects before its step: bus, port or time use, an
undeclared exit, a missing finish, a changed entry stack or an interrupt that
would be accepted. Such a call falls back to the original with its register
changes undone and counts no window reason, so a fallback with no reason is
valid when `forced_failed` accounts for it. Every single-instruction segment,
forced or not, refuses bus, port and time operations; only register adjustments
and the final step are allowed. Forced admission exists for options that adjust a register around one
instruction; original mode never uses it, so the checkpoint census requires
both counts to be 0.

The verifier recognises a `returned` exit by the return address it read from the
stack at entry. A routine that overwrites its own return address is therefore
reported as an understated bound, because the original never reaches that
address.

A segment that stores through an index register (the experience segments at
`00:1774` and `00:179D`) raises a run-time error instead of falling back if the
address is one of the mapping registers `$FFFC-$FFFF`. The game always points
that register at a character record, so this cannot happen in play.

A scratch stack is at most 16 bytes, and a core-final exit at the entry address
is accepted only for a single-instruction segment. Plain instructions that
change the stack pointer (pops, loads of SP, increments and decrements of SP)
are refused as final instructions.

#### Held stack words, conditional exits and loop exits

Three exit properties let a segment end inside a routine that keeps words on
the stack, branches, or loops. Each defaults to the value that selects the
behaviour above, and no segment registered before milestone 7 uses one. An
exit is identified by its kind and address; `stack`, `conditional` and
`after` are properties of that one declaration (`SegmentExit::operator==`
compares kind and address, `same_declaration` every field). `add` refuses two
exits with the same kind and address.

- **Held words** (`SegmentExit::stack`, `SegmentSpec::inherited_stack`). An
  exit's `stack` is SP before the final instruction minus the entry SP, in
  even bytes. Negative: words the segment pushed and holds across the core's
  final step (typically a CALL into original code); it may not go below the
  declared `scratch_stack`. Positive: words the segment popped that an earlier
  segment of the unit pushed; it may not exceed `inherited_stack`, the bytes
  above the entry SP that belong to the unit (even, at most 8). A returned exit
  cannot hold a pushed word, and a single-instruction or forced segment can
  declare neither. `finish_core` and `finish_return` require SP equal to the
  entry SP plus the exit's offset before the step, and check the step's
  result relative to that SP (a return raises it by 2, a taken call lowers it
  by 2, anything else leaves it). The body reads and writes these words
  through the context's bus, so a stack in ROM or cartridge RAM behaves as in
  the original. Dispatch falls back with `unmapped_stack` when any byte from
  SP to SP + `inherited_stack` + 1 is unmapped or in `$FFFC-$FFFF`.
- **Conditional exits** (`SegmentExit::conditional`, core-final only). At such
  an exit the final instruction must be `JP cc,nn`, `JR cc,e` or `DJNZ`, and
  nothing else; without the flag those opcodes stay refused, as do `JR e`,
  `JP (HL)`, `JP (IX)` and `JP (IY)` everywhere. The core decides the outcome
  from the flags and B that the body left; the step must end at the target or
  at the next instruction with SP unchanged.
- **Loop exits** (`SegmentExit::after`, core-final only, at most 16,384). The
  exit is taken at the first arrival at its address with at least `after`
  cycles elapsed since the entry. `finish_core` refuses an earlier arrival.
  With 0 it is the first arrival, as before.

What the per-call verifier compares, and why a divergence cannot hide:

- The native copy stops at a boundary at a declared core-final address only
  when SP is the entry SP plus that exit's offset and, for a loop exit, at
  least `after` cycles have passed. The context refuses an earlier arrival
  in C++; a body that stops a record late reaches the same address with other
  cycles and registers, and the whole-machine comparison names them. Held words stay in memory, so the
  words pushed by one segment are compared at every later exit of the chain.
- The stop at a return to the word read at the entry SP (SP = entry + 2)
  applies as before, except in a segment with `inherited_stack` above 0 that
  declares no returned exit at offset 0: there that word is held data, and a
  pop leaving PC equal to it must not end the copy. For each returned exit at
  offset s above 0, the copy also stops when the instruction at that address,
  started at a boundary with SP = entry + s, leaves SP = entry + s + 2 and PC
  equal to the word that was at that SP. An untaken conditional return there
  continues.
- The exit the copy stopped at is the declared exit, offset included, and it
  must equal the body's exit in every field; a different address, kind or
  offset is reported as `exit mismatch` (offsets are printed as `stack -2`).
  A native path that returns at an undeclared offset never stops and fails as
  `understated bound` or at the frame boundary. A conditional exit needs no
  new rule: the copy stops at the declared address and steps once, and a body
  that left the wrong flags or B ends at another PC, which the comparison
  names together with the flags or B.

### Options

Two optional flags change the game. The app loads saved preferences for
omitted fields; an explicit flag overrides just that field, even at 100.
The headless replay runner always defaults to original and reads no app
settings. The flags exist in `psmodern.exe` and `psmodern_replay`, share
one parser (`src/game/option_flags.h`) and take their allowed values from
`src/game/options.h`:

| Flag | Allowed values | Effect |
| --- | --- | --- |
| `--encounter-rate <percent>` | 0, 25, 50, 100 or 200; default 100 | Scales the random-encounter threshold on the map and in dungeons: threshold times the rate divided by 100, rounded down, at most 255. 0 turns random encounters off but still consumes the draw. Fixed and story fights are untouched |
| `--experience-rate <percent>` | 100, 200 or 400; default 100 | Scales the experience reward once, rounded down, at most 65,535 and never zero when the original reward is not; the message and every character's gain use that value |

A value must be a whole number of digits with no sign or space; a value
that is not on the list, a missing value or a flag given twice is an error with
a message listing the allowed values. A flag at any value other than 100 is refused with
`--no-routines` (the options are carried out by the rewritten routines) and,
in the app, with `--record` (recordings always play the original); in the
runner it is also refused with `--lockstep-calls` and `--compare-recording`,
because the game differs from the original by design and cannot be compared.
An explicit 100 is accepted in restricted modes too. App recording and
`--no-routines` force original execution, retaining configured preferences
even when SRAM changes. Recording ignores the Options binding; raw-Z80
sessions can open Options read-only during exploration. CLI overrides remain
session-only until an explicit in-game Apply, which supersedes launch flags.

**Maintainer decision (2026-11-11).** The options take fixed values, not
ranges: encounters 0, 50, 100 or 200 percent and experience 100, 200 or 400
percent, with 100 (the original game) as the default for both. **Maintainer
decision (2026-11-13).** Encounters also accept 25, and the recommended pace,
the Switch release's easy mode, is encounters 25 and experience 400; the
constants for them are in `src/game/options.h` and the selection screen. A
saved 50 stays valid.
Reduced or off encounters still consume the random draw, a character at the
experience ceiling keeps gaining one level per fight. The formulas are explained in the
[random numbers](how-it-works/random-numbers.md#the-encounter-rate-option) and
[party](how-it-works/party.md#the-experience-rate-option) chapters.

Apply first persists configured preferences and queues their activation. A new
Apply supersedes an older pending choice. Effective rates and their captured
registry remain unchanged until the native main dispatcher `00:00AF`, after
the complete mode handler, so a battle's displays, character awards and money
share one rate. Activation uses the instruction hook forwarded through
`run_frame` without redirecting execution; no hook runs inside a registered
routine or accepted interrupt. Registry replacement preserves enabled/verifier policy and occurs
only at the unregistered dispatcher. Reset reaches that dispatcher; F5 creates
a new machine with the latest chosen values. Quit/restart loads the durable
choice. No runtime SRAM restore is added. Opening/cancelling changes nothing.

To run a recording with the options on and read what ran, use the runner; it
writes manifests and the census but compares nothing:

```sh
psmodern_replay <rom> <recording> --dump 1 --out <new-folder> [--cart-ram <file>]   --encounter-rate 25 --experience-rate 400
```

The census then has five option segments: `00:0F5B` and `01:5FB0` (the
two threshold comparisons) and `00:170D` and `00:1773` (the reward load and the
per-character addition), plus `01:60BC` (the battle-money production store).
The experience rate controls the last three. In normal gameplay a segment
whose option is on is admitted at every moment, since it is one original
instruction run by the core with a register adjusted around it and adds no
time. Those calls count in `forced` (a subset
of `cpp`) and in the `forced_` reason columns when an interrupt, a frame
boundary or another window reason fell inside. A healthy enhanced census has
`forced` equal to the call count and `fallback` and `forced_failed` 0 on these
five in normal mapped game states; the other segments behave as in original
mode. A forced call the mechanism cannot admit counts in `forced_failed`
and falls back to the
original instructions. In original mode the same five segments follow the
ordinary window rule and `forced` is 0, which the checkpoint tests and the
coverage generator require.

## Reference driver

```text
python tools/reference.py <Mesen.exe> <rom> <recording> --dump all --out <new-folder> [--cart-ram <file>] [--reference-label <label>]
```

Use the patched MesenCE 2.2.1 as the primary reference; stock CE remains usable.
Pass a build-specific `--reference-label` (the default says "unspecified build").
Each run writes `reference.json` with that label, the executable SHA-256 and
the SHA-256 of each adjacent DLL and of the recording. The reference manifest
also carries `reference mesence 1`, `build-sha256`, `build-label` (a JSON string)
and `recording-sha256` immediately after its column header. These identify the
producer, build and exact recording; the runner's plain manifest is not a
reference. `PSMODERN_REFERENCE_SHA256` pins the accepted executable build; its
default is `1ba3e21c1008ffd71fd94fd9c3ab9bd23bb65c750e5c7f3e9d0260c4d93984b5`.
If the reference is rebuilt, verify the new executable's SHA-256, configure
with `-DPSMODERN_REFERENCE_SHA256=<new-lowercase-sha256>`, rebuild the checkpoint
test and regenerate the reference manifests with that build.
Each run requires a new output folder and
creates a fresh portable data folder in a temporary sibling, copying the supplied executable
and adjacent DLLs, with an empty configuration and only the requested initial
battery save.
Lua uses relative paths from that portable home, including for non-ASCII output
paths. Only a completed capture and manifest are published as the requested
folder; failed preparation, process execution or hashing removes the staging folder.
Callback failures stop the reference immediately and are reported by the launcher;
CE otherwise logs Lua errors and can continue with incomplete frame files. The
Lua timeout is set in the fresh configuration, because CE's command-line numeric
setting parser ignores `ScriptTimeout` without reporting an error.
No existing MesenCE data folder is reused. SMS1, export NTSC, zero-filled RAM,
FM off and both SMS controllers are specified explicitly. No firmware is copied,
so no BIOS is loaded. Lua sets scanline and V counter to zero before the first
instruction and verifies the initial 8 KiB cartridge RAM byte for byte.

The script applies input before running, at startFrame and at inputPolled.
**Reference API limitation.** CE 2.2.1's `setInput` reads port/subport from Lua
stack positions three/four. The driver uses `setInput(table,0,port,0)` and checks
both pads with `getInput(port,0)` after every application. Two-argument calls
would let the released second pad overwrite the first pad.

At startFrame K, Lua saves the previous picture before buffer swap, cropping
unfiltered RGB rows 24-215 and recovering SMS channel values. It arms a pending
capture. The next exec callback saves RAM, VRAM, CRAM and cartridge RAM if no
interrupt entry intervened. CE dispatches its IRQ/NMI event after entry, before
that exec callback. In that case the driver subtracts the entry duration and
exports the instruction-end memory, undoing entry-time writes in the exported
copy only. The exported CPU state similarly reverses entry changes: the pushed
PC and prior SP and R are recovered, and the frame-start state supplies the
interrupt flip-flops and HALT state where entry cleared them. When the CPU is
already halted at the frame event, CE has no fetch callback, so the driver
captures its unchanged halted state at that event and uses the cycle count of
the halted step. If the frame began during the entry itself, the next exec
remains the stopping point. CPU and palette write notifications supply the journal;
CE does not deliver VRAM stores to Lua under their VRAM domain, so the driver
journals pending VRAM transfers with CE's SMS1 slot schedule and live-address latch. The authored
regressions include a NOP ending at the frame boundary and a queued VRAM store
completed during the following IRQ entry. Both match the runner before PC is
pushed. `boundaries.txt` records the captured instruction-end cycles.
**Maintainer decision.** The checkpoint compares the CPU and the ordered PSG
writes as well as the picture and memories. CE exposes the CPU registers through
its Lua state API. Its PSG port-write notifications include the exact CPU cycle;
the callback-domain workaround described below identifies those notifications.
The launcher hashes all frames and retains raw dumps only for the selection;
the manifest and dump names/sizes are shared with the runner.

**Reference limitation.** CE has no SMS console Reset pin reachable from Lua. Recordings
pressing Reset are refused before output creation. Machine unit tests and the
play-test cover Reset. Its `SmsConsole::Reset()` reloads the ROM as a power
cycle; routing Lua input to a hardware Reset pin would require changes across
the input and console layers, so the duration patch leaves it out. Stock CE
has known CPU timing faults, so a difference against it is evidence to
investigate. **Inference (from authored probes).** CE's R count while halted differs from
PSModern's, as an authored HALT comparison demonstrates; the full register
comparison reports that difference rather than suppressing it.

## Comparison

```text
python tools/compare_frames.py <left-manifest> <right-manifest> [--stack-range CAF8:CAFB]
```

Exit status is zero for equality, one for differences and two for invalid input.
The report includes the first overall difference and the first differing frame
of every component, so an early RAM mismatch cannot hide a later picture mismatch.
When both raw dumps exist, it reports the first differing offset; RAM also shows
the C000-based address and both values. It never reads or prints ROM bytes.
The additional RAM summary scans all available dumped frames for the first
mismatch outside explicitly supplied inclusive hexadecimal stack ranges. These
ranges affect only that extra summary: stack mismatches remain in the main
report. Without raw RAM dumps it cannot classify individual addresses. Supply
ranges justified by the run rather than assuming every high RAM address is stack.

Each version has its own inputs under `recordings/us/` or `recordings/br/`.
The first three checkpoints start at power-on with zero cartridge RAM. The
other three, and the three US-only dungeon recordings, load slot 1 from a
cartridge RAM image after power-on.

| Recording | Play covered |
| --- | --- |
| `opening` | Start a new game, name save slot 1, visit Alis's home, talk to Suelo and open the town menu. |
| `first-battle` | Leave Camineet, defeat the first random encounter on Palma and walk on after the reward. |
| `first-dungeon` | Enter Camineet's warehouse and follow the 3D corridors around three turns to a door. |
| `later-town` | Load Paseo, walk to the hospital, talk to its attendant and open the town menu. |
| `boss` | Load the path to the governor, hand over the Shortcake, receive the letter, sleep and finish the Saccubus battle. |
| `deep-dungeon` | Load near Maharu Cave, enter, use a Flash and follow corridors around several turns, fighting any encounters. |
| `dungeon-walk-1` (US) | Continue in Maharu Cave: steps, a step back, the item and money chests, the pitfall to the floor below, the fake wall both ways, stairs down and up, a plain door opened and passed both ways, then save in the dungeon. |
| `dungeon-walk-2` (US) | Continue from that save: win the fixed fight ahead and open its chest, five steps back, stairs up, then save in the dungeon. |
| `dungeon-continue` (US) | Continue from that save: steps, a step back, and out through the stairs-up exit to the world map. |

The images of the six shared checkpoints were made by playing in the
reference with SMS1, export NTSC, zero-filled power-on RAM, FM off and two SMS
controllers, then saving through the game's menu. The three dungeon
recordings form a chain of game-made saves, none edited: the first image is
the deep-dungeon image after a preparatory run of its own inputs that saves
in Maharu Cave, and each later image is the previous recording's final
cartridge RAM, identical in PSModern and in the reference. Each Brazilian image was loaded and saved again by the
Brazilian game in the reference. Keep each initial image at
`<PSMODERN_REFERENCE_DIR>/<us-or-br>/<recording-stem>/cart-ram.bin`.
Supply that image with `--cart-ram` when regenerating its manifest.

To regenerate a checkpoint, use the reference driver above with that region's
ROM and recording, the patched executable, the label
`MesenCE 2.2.1 durations+int-boundary+block-io`, `--dump 1` and a fresh output folder.
Copy `manifest.txt` and `reference.json` into
`<PSMODERN_REFERENCE_DIR>/<us-or-br>/<recording-stem>/`. The
`checkpoint` CTest label runs each recording against its own manifest and
skips a checkpoint when its ROM, required image or manifest is missing. With
the ROM present, a missing recording file fails: the recordings are part of
the repository.
Before skipping it validates every available input: recording format and nonzero
frame count, ROM digest, image size/digest, manifest header and provenance.
A present incorrect file fails. Missing provenance, a build digest different
from `PSMODERN_REFERENCE_SHA256`, an unlabelled build, a
different recording digest or a runner-produced manifest fails. It
checks the initial image against the recording header's SHA-1, then checks all
seven hashes at every frame, with no exclusions or transient-byte allowance.
Wait for the launcher to exit successfully and verify that the manifest's last
frame equals the recording's `end` frame before copying it. `complete.txt`
marks emulator capture completion; hashing and manifest publication follow it.

The short recordings exercise two-run determinism and scripted SDL recording.
The data-free tests cover the format, publication, runner arguments and
synthetic manifests. Keep drafts, saves, reference binaries, manifests and
captures outside the repository.
`PSMODERN_REFERENCE_EXECUTABLE` enables the authored reference-driver CTest
regressions against a local MesenCE build; an absent executable skips that test.
Python tests use explicit checks and nonzero exits, including with `-O` or
`PYTHONOPTIMIZE` set.

## Fixed machine start and frame input

**Inference (from community source).** The fixed state follows the MesenCE 2.2.1
initializers with no BIOS,
SMS1 export/NTSC, FM off and zero-filled RAM, except for its initial IRQ line
as described below. At CPU cycle 0 the VDP starts at scanline 0, VDP cycle 1.
The CPU starts with PC=`0000`, AF=`AB42`, BC=`0002`, DE=`A51B`, HL=`0000`,
IX=IY=`0000`, SP=`DFF0`, alternate AF/BC/DE/HL=`0000`/`0200`/`C700`/`C75F`,
I=`00`, R=`1D`, IFF1=IFF2=0 and IM=1. WZ and other core internal fields start
at zero. The mapper control is zero, its banks are 0, 1 and 2, memory control
port `$3E` is `AB`, and I/O control is `0F`. Work RAM is zero except `$C000`
=`AB`; an absent save supplies 8 KiB of zero cartridge RAM. VRAM is zero.

The representative VDP register bytes R0–R10 are `36 60 0F 00 00 7E 03 00
00 00 FF`; the reference initializes the underlying fields directly, so these
bytes reconstruct their effective values rather than read back write-only
registers. CRAM starts as two 16-byte rows:

```text
00 3F 3E 3F 30 30 38 3F 37 3F 00 00 00 00 00 00
00 03 30 0F 07 16 3F 02 00 0F 00 00 00 00 00 00
```

The VDP address is `3D6A`, code 1, read buffer 0, command latch clear,
line-counter reload `FF` and current counter `FC`; VBlank and sprite overflow
are pending, and line IRQ and collision are clear. The H-counter latch is 0.
The source is the [MesenCE VDP initializer](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsVdp.cpp)
with a startup readback described in the [plan](milestone-1-plan.md#reference-emulator).
Only values actually read back while running the identified ROM have the label
**Reference emulator**; initializer-only values remain an inference from community
source. These are selected starting values, not measured chip power-on values.

**Hardware documentation; maintainer decision.** INT is derived from the frame
and line pending flags and their R1 bit 5 and R0 bit 4 enables. The
[VDP notes, section 12](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
describe the enabled pending flag as asserting the line. Consequently INT is
active in the fixed initial state. MesenCE instead initializes the pending
flag and enable without recalculating IRQ, then initializes the CPU with no
active IRQ source. Its next R0/R1 write, status read or relevant event
recalculates the line; see `InitSmsPostBiosState` and `UpdateIrqState` in the
linked VDP source.

**ROM static.** In both ROMs IFF1 and IFF2 are clear at startup. The path
from `00:0000` disables interrupts, then `00:0084` disables them again, sets
up the mapper and clears RAM. It calls `00:02ED`, which initializes sound in
bank 0C with RAM and PSG writes, no interrupt enable or VDP access. The block
output at `00:03AA` in the routine at `00:03A4` writes R0 first, causing
MesenCE to recompute its IRQ before the first interrupt enable at `00:00AE`.
An early Pause NMI returns while IFF2 remains clear. Thus the different initial
IRQ level cannot be accepted by the CPU before the models agree.

Frame N starts on the first CPU cycle of line 0. The host supplies pad, Reset
and Pause state for that frame before the boundary; the machine latches it at
the boundary. An input change during a frame takes effect at the following
boundary. **Hardware documentation:** the [SMS display measurements](https://www.smspower.org/forums/8161-SMSDisplayTiming)
place Pause NMI at the transition into line 261; a held button produces only
one NMI edge. The frame runner stops at the first complete instruction boundary
at or after the next frame start and reports the cycle overshoot.

`Machine::run_frame(next_input)` supplies the pad, Reset and Pause state for
the boundary at which that call ends. This makes frame N's input available
while frame N-1 is running. A Pause edge reached after an instruction's
`E-1` sample point is deferred to the next instruction, including a host
scheduled edge. NMI and INT use the same instruction sample point.

## VDP and interrupt timing

The scheduler uses dots, 342 per line. CPU cycle 0 is VDP dot 1 and each CPU
T-state advances three half-dots, so every 228-cycle line has the same phase:
at line cycle `c` the VDP is at dot `floor(1 + 1.5c)`, and an event at dot `d`
becomes visible at line cycle `ceil((2*d - 2)/3)`. For an instruction
occupying CPU cycles `[S,E)`, the machine supplies the core with the INT
level sampled at `E-1`, after any port access in that instruction. An event
first visible at `E` is sampled by the following instruction. The core's
prefix, EI, RETI/RETN and NMI acceptance rules also determine whether an
interrupt is accepted and the boundary hook is called.
`Machine::advance(n)` uses the same `E-1` point for INT and NMI when `n>0`;
a replacement accounts for its own PC/R effects separately. `advance(0)` is a
no-op: it neither advances devices nor resamples INT or NMI.

**Hardware documentation.** The [Zilog Z80 CPU User Manual, UM008011-0816,
figure 7](https://www.zilog.com/docs/z80/um0080.pdf), shows the I/O machine
cycle as T1, T2, an automatic wait state and T3; read data is taken in T3,
before /RD goes inactive. Port reads and writes take effect at the start of
T3, three T-states after the machine cycle starts. For `IN A,(n)` and
`OUT (n),A` this is also the INT sample point `E-1`. Charles MacDonald's
logic-analyser measurements on an NTSC SMS, in the
[SMS display timing thread](https://www.smspower.org/forums/8161-SMSDisplayTiming)
(February 2006), put the frame /INT 607 master clocks after pixel 0 of line
192 and the line /INT at 608, 4 and 3 clocks before the /HSYNC rise. An `IN`
whose I/O cycle ends at that /CSYNC rise reads the new V counter; one ending
four CPU cycles earlier reads the old value. With INT sampled in the last
T-state, the first instruction that takes the frame IRQ and the first `IN`
that sees the new V counter end at the same cycle, and the line IRQ is taken
one cycle later. In the same thread (March 2008), FluBBa's IRQ and NMI
handlers latch H with their first instruction after a HALT. He reports lowest
values `06` for the frame IRQ, `07` for the line IRQ and `06` for the Pause
NMI, among four values with `08` repeated.

**Hardware test.** The [SMS VDPTest](https://github.com/FluBBaOfWard/SmsVdpTest),
checked by its author on an SMS1 VDP, first synchronizes the CPU to the TH
latch. It then times the V-counter change, the readable frame bit, sprite
overflow and collision, and, after HALT, the frame and line IRQs (`82`/`84`
and `88`/`8A`) against that latch. The model passes all 55 automated SMS1
results. Each event is fixed by its own test to one CPU cycle.

The model therefore has one VDP event at dot 315, line cycle 210. On physical
line 192 it sets the frame flag, which is pending and readable at once. On
every line it advances the V counter. **Inference (from community source).**
Following CE, sprite overflow is recorded on physical lines 0–191 and 261,
including line 191's evaluation for target line 192. **Unknown:** whether an
SMS1 raises overflow for that non-visible target line. The frame IRQ asserts at CPU cycle
43986 of a frame, taken by an instruction ending at line cycle 211 or
later. The line counter is clocked at dot 317, line cycle 211, one CPU cycle
after the frame event. It decrements on physical lines 261 and 0–191 and
reloads R10 on 192–260. This is the 0–192 decrement window of the
[VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
when lines are named by the V-counter value after its transition. Underflow
sets the line pending flag and reloads R10; a write to R10 takes effect at the
next reload, not immediately. The Pause NMI sample at line 260, cycle 212,
two cycles after the frame event, reproduces FluBBa's NMI value.

A status read clears both pending interrupt flags and the readable sprite and
frame flags. INT is the OR of the two pending flags gated by their enables,
including when an enable is set while a flag is already pending. The status
byte has no readable line-IRQ flag. The NTSC 192-line V-counter sequence is
`00–DA, D5–FF`. H-counter reads return the held latch. On an export SMS1
without a light gun, a rising TH edge via port `$3F` updates the latch, and
ordinary reads leave it unchanged.
**Inference (from community source).** The five low status bits return the
current CPU open bus, following MesenCE's `ReadPort` implementation. This value
is not established here by a hardware measurement or an identified-ROM run.

**Inference (from community source and authored probes).** The H latch takes the dot three dots after
the T3 port write that raises TH. The latched value is `p/2` for `p<296`
and otherwise `(p-343)/2` with signed division toward zero, where `p` is
that dot. Latches at line cycles 209, 210 and 211 read `F3`, `F4` and `F5`.
[MesenCE](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsVdp.cpp)
adds the same three internal dots in `InternalLatchHorizontalCounter`. Its V
counter, frame flag and sprite overflow share its cycle 315, and its line
event follows one CPU cycle later. Our own HALT and NOP-loop test program
gives the same H values in this model, MesenCE 2.2.1 and Emulicious, with
HALT and NOPs alike. VDPTest, MacDonald and FluBBa fix only the total offset
between the T3 access and the latched dot. How the three dots divide between
the I/O chip's write edge and the VDP's TH input is not established.

**Inference.** A model that keeps port accesses at T2 is equivalent. It must
add six dots to the latch, move the V counter and overflow to dot 312, and
make the frame bit readable at dot 313, before its pending point at 315. The
observations above cannot separate the two. T3 follows the Zilog figure and
keeps the frame flag a single event. These sources also fix the VDP events
only relative to each other and to the CPU sample points, not to power-on.
Moving every interrupt four cycles later and keeping the other events would
satisfy them equally. The power-on phase, VDP dot 1 at CPU cycle 0, follows
MesenCE as described under the fixed machine start. That selected phase places
the frame IRQ at absolute CPU cycle 43986.

## Mode 4 picture

**Hardware documentation.** The [VDP register map](https://www.smspower.org/Development/VDPRegisters)
and [MacDonald's VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
describe the 315-5124 mode 4 background: 32 by 28 name entries, 4-bit planar
patterns, horizontal and vertical tile flip, palette select and background
priority. Pattern color zero draws CRAM entry 0 or 16 from the selected tile
palette, but is transparent to sprite priority. R2 bits 1-3 select
the name table with bit 0 controlling the row mask. R7 selects the
sprite-palette backdrop entry, R1 bit 6
enables display, and R0 bit 5 blanks the left eight pixels.
Vertical name-table rows wrap at 224 for every R9 value in this 192-line mode:
`E0`-`FF` select the same initial rows as `00`-`1F`, as described in the
[vertical-scroll experiment](https://www.smspower.org/forums/16020-VerticalScrollingQuestion).

**Inference.** The VDP notes say R3 and R4 should be set for mode 4, but do
not specify the behavior of unset bits. The renderer treats them as address
masks on the low and high pattern planes, respectively; the byte-array tests
cover this model, which remains to be confirmed on hardware.

**Hardware documentation.** The same sources specify sprite Y as one less
than the first covered line, Y=`D0` as the list terminator, up to eight
sprites on a line, 8 by 8 or 8 by 16 patterns, vertical zoom for all eight
and horizontal zoom for only the first `max(0,N-4)` of the N selected sprites,
R0 bit 3's eight-pixel
left shift, R5's attribute table address bits and R6's pattern address bits.
The [SMS1 zoom measurements](https://www.smspower.org/forums/19069-ZoomedSpritesOnSMS1)
and [sprite documentation](https://www.smspower.org/Development/Sprites) clarify
that four or fewer selected sprites get no horizontal zoom.
**Inference.** The R6 mask descriptions conflict: MacDonald's notes give
tile-index bits 8 and 6, while the SMS Power register map gives bits 7 and 6.
PSModern follows SMS Power: R6 bit 2 supplies address bit 13 independently,
and bits 1 and 0 AND-mask tile-index bits 7 and 6 (address bits 12 and 11).
CE does not implement these two SMS1 masks.
**Inference (from community source).** As in CE, sprite bank, tile and latched
row address fields combine with OR. A height change between Y evaluation and
index fetch can overlap tile/row bits, but must not carry into the bank bit.
Sprites use the second CRAM row; an opaque priority background pixel covers
a sprite without suppressing sprite collision. The first sprite at an
overlapping opaque pixel owns its color. The [VDPTest source](https://github.com/FluBBaOfWard/SmsVdpTest)
tests overflow, collision, offscreen-Y and terminator behavior on SMS1.
The renderer records overflow for the next line at the dot-315 event
described under VDP and interrupt timing, including when display is
disabled. It records a collision when the second opaque sprite pixel reaches
dot `9+x`; every later overlapping pixel can set the collision flag again
after a status read. Transparent pixels never collide. A status read clears both flags.
**Hardware test.** VDPTest's overflow and collision tests fix both points to
one CPU cycle relative to the H latch.

**Hardware documentation.** R0 bit 6 holds horizontal scroll at zero for
the top two tile rows, and bit 7 holds vertical scroll at zero in the right
eight tile columns. **Inference.** R8 keeps the model's dot-313 latch for
the following line.
**Hardware documentation; inference (from community source).** The [VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
say an active-display R9 write is stored for the next active-display period;
[MesenCE](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsVdp.cpp)
copies R9 into its vertical-scroll latch when the scanline wraps to zero.
The machine latches R9 at the start of line 0, before that line is drawn, so
writes during the preceding vertical blank apply to that frame.

**Hardware documentation.** Sega's [official manual](https://www.smspower.org/Development/SMSOfficialDocs)
specifies access spacing (29 T during active video, 16 T during vertical blank).
The [VRAM timing discussion](https://www.smspower.org/forums/18134-TimingOfVRAMAccessSlotsDuringActivePassiveDisplay)
describes name fetches followed by two pattern-word fetches and interleaved
CPU slots; its detailed bus trace is from a Mega Drive in mode 4.
**Inference (from community source).** The SMS1 schedule below follows the
CE 2.2.1 source; it is not a measurement on hardware. Dots are
completion coordinates in this machine, one dot after CE's bus-operation index.

| Display state | CPU access completion dots |
| --- | --- |
| Active lines 0-191 | `3+32*n` for n=0..7; 257,259,261,263; 289,291,293,295,297; 323,325 |
| Blank lines 192-259 | `3+4*n` below 256; every odd dot 257-341 |
| Line 260 | Same as blank, with no slots from 327 onward (sprite preparation) |
| Line 261 | Every odd dot below 256 except 11,19,27 in each 32-dot block; active-line slots above 256 |
| Display off | Blank-line schedule on every line |

VRAM and SMS CRAM writes share one pending data latch. A second write replaces
its value; there is no FIFO or CPU stall. The live address increments only when
a slot completes, wrapping after `3FFF`; a control command can retarget an
unserved request. A read command requests a prefetch. Writes have priority,
then a pending read fills the buffer at the following slot.
**Hardware documentation; inference.** A data-port read returns the old buffer
and requests the same slot-served read-ahead. Repeated early reads coalesce;
a read while a write is pending returns the shared write latch and leaves a
read request behind it. CE's command prefetch and shared-latch arbitration
support this model, but CE refills on data reads immediately. **Unknown:**
hardware behavior of mixed/overrun accesses outside the documented spacing.

**Inference (from community source).** Each column fetches its name at dot `1+8*c`, low
pattern planes at `5+8*c`, and high planes at `7+8*c`. The pipeline retains
those bytes; later writes affect only later fetches. R2 takes effect on the
next name fetch, R3/R4 on the corresponding pattern fetch. R8 keeps the
prior-line dot-313 latch; R9 keeps the frame-start latch. Palette, backdrop
and display enable are sampled as each pixel leaves at dot `9+x`.
**Hardware documentation.** The [sprite description](https://www.smspower.org/Development/Sprites)
places sprite preparation before the line that displays it.
**Inference (from community source).** Exact fetch dots follow CE's
`ProcessSpriteEvaluation` and `LoadSpriteTilesSms`, not physical measurements.
Y scanning starts at completion dots 327,329,...,341 in the earlier hblank
(two entries per event), continuing at 11,19,27 in each 32-dot group of the
line before display. During that line, X/index fetches for entries 0-7 complete
at 265,267,277,279,299,301,311,313. Their low plane words complete at
269,273,281,285,303,307,315,319, and high words two dots later. The next line
uses those retained bytes; later VRAM writes cannot change them. Blank/display-off
lines evaluate two Y entries every four dots below 256. Overflow is exposed at
dot 315 on physical lines 0–191 and 261, including target line 192 from line 191
(**Inference (from community source)**; SMS1 hardware behavior for that target
is **Unknown**). Collision remains at `9+x`, including
the hardware-tested vertical-border behavior that CE omits. VDPTest passes its
automated cases; its offscreen-Y case does not cover target line 192.

**Maintainer decision.** PSModern does not draw an extra dot for an active-display
CRAM write, matching the reference build; the palette write still completes at
its CPU access slot. **Community source.** [Forum observations](https://www.smspower.org/forums/14997-HorizontalScrollPaletteChangeMidFrame)
report dots on some consoles, including an SMS II. **Unknown:** their color and
the behavior of SMS1 hardware.
**Reference emulator.** Emulicious independently confirms the mid-line name
changes at BR frame 4477 and US frame 10816. **Unknown:** the precise hardware
slots at US frames 11031 and 11623; Emulicious does not confirm CE there.
Matching those frames establishes reference equality, not hardware timing.
Each completed output is 256 by 192 bytes of 6-bit CRAM colors. At frame N's
start, `Machine::picture()` exposes frame N-1. The machine library does no
rendering I/O.

## PSG

**Hardware documentation.** The PSG follows [Maxim's SN76489 notes](https://www.smspower.org/Development/SN76489),
sections "SN76489 register writes", "Tone channels", "Noise channel" and
"Volume/attenuation", for Sega's integrated 16-bit variant. All writes to
`$40`–`$7F` reach it; reads still return V/H counters. There is no Game Gear
stereo register or FM unit.

- A latch byte (bit 7 set) selects one of eight registers and replaces its
  low bits. Registers 0/2/4 are 10-bit tone periods, 1/3/5/7 are 4-bit
  attenuations, and 6 is 3-bit noise control. A data byte retains the selection:
  it replaces tone bits 4–9, or the low four/three volume/noise bits. Ignored
  high bits never spill into another register. Each byte takes effect at once.
- Each CPU/16 tick decrements a nonzero tone counter. At zero it reloads the
  current period and flips its output bit. A period write leaves the counter
  running. Periods 0 and 1 contribute constant +1 to the mixer, while the
  internal divider continues to run, including the noise divider using tone 2.
- Noise has its own counter and toggled clock bit; rising edges shift the
  LFSR right. White noise inserts bit 0 XOR bit 3 into bit 15; periodic noise
  recirculates bit 0. Output is bit 0 after the shift, following the notes'
  implementation. Any noise-control write, including a data byte or an
  unchanged value, resets the LFSR to `8000`. Counter reloads are 16, 32, 64
  or tone 2's current period: shifts repeat at CPU/512, /1024, /2048 or
  CPU/(32*Tone2), respectively. A zero period clocks like one here.
- Attenuations 0–14 step down by 2 dB; 15 disconnects the channel.

**Inference (from community source).** The fixed state in `initial_state.h` follows the
[MesenCE 2.2.1 PSG constructor](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsPsg.cpp)
and its zero-initialized [state structures](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsTypes.h):
tone periods/counters/output bits zero, attenuations all 15, selected register
zero, noise control/counter/clock/output zero and LFSR `8000`. Output queues
start empty. MesenCE's Game Gear panning state has no SMS counterpart here.

**Inference.** Noise-control writes reset only the LFSR, retaining the counter,
clock bit and output latch until the next rising edge. Maxim specifies the
LFSR reset without specifying resets for those other fields; MesenCE retains
them too. The noise divider reads tone 2's register at its own reload, rather
than borrowing tone 2's output edge, following Maxim's counter model.

**Inference.** CPU cycle 0 precedes the first PSG tick at 16. Subsequent ticks
and samples are at absolute multiples of 16, independently of frame or
instruction boundaries. Maxim fixes the divider ratio, not its power-on
phase. `Machine::advance` and CPU steps call the same PSG scheduler. A tick
and its sample precede a write stamped at the same cycle; that write first
affects the next sample. This total ordering is a deterministic model choice,
not a measured power-on or bus-edge phase. Actual port writes retain the
T3 timestamp described under VDP and interrupt timing.

### Write log and samples

The machine owns the PSG, its divider phase and its pending outputs; copying
or assigning it copies all of them. Equality includes pending writes, samples
and write-budget accounting. `Psg::write(byte, cycle)` appends the `(cycle,
byte)` event and consumes it through the register decoder. Both live I/O and
log replay use this path; `sync(target)` makes the samples between events.
Replay requires the same starting PSG state, events and target cycle, with
the same draining needed to stay within the output bound. Repeated syncs or
different advance sizes do not change any sample.

The log permits **8,192 writes per absolute frame** (`cycle / 59736`), and
at most **8,192 unread writes** in total. The shortest CPU output instruction
costs 11 cycles, so even an unrealistically continuous stream cannot reach
the per-frame bound. It also covers direct test-side port writes. The host
drains using `Machine::take_psg_writes()` once per frame; draining
does not restore the current frame's budget. Nothing is discarded at a frame
boundary. A full log or exhausted budget throws `std::overflow_error` before
that access advances any device or changes the PSG. All accepted entries
remain available. If this exception escapes a CPU instruction callback, the
host must abort that run or restore a snapshot: instruction execution itself
is not transactional. Unit tests cover both bounds, retained data and drains.

**Inference (integer representation).** Samples are mono signed 16-bit values
at the exact rational rate **3,579,545/16 Hz** (223,721.5625 Hz), with no
resampling, filtering, host clock, audio device or file I/O. The integer table
is `round(8191 * 10^(-a/10))` for attenuation `a=0..14`, then zero:

```text
8191 6506 5168 4105 3261 2590 2057 1634
1298 1031  819  651  517  411  326    0
```

Mixing sums each channel's 0/1 output times its table entry, without division,
clipping or DC removal: range 0–32764. `Machine::take_samples()` returns the
first absolute sample cycle and contiguous values spaced 16 cycles apart,
then empties the pending sample vector. There is no cycle-0 sample. Hosts
drain samples per frame (3,733 or 3,734 samples at the exact boundary);
the vector retains all unread samples until drained. At most 16,384 unread
samples are allowed (about 4.39 frames); overflow throws before advancing
the other devices. The host drains both writes and samples once per frame.
This is an internal machine output; the app handles device-rate conversion.

### Reference comparison

**Inference (from community source).** MesenCE 2.2.1 flips the mixed tone output at periods
0/1 instead of holding +1, unlike Maxim's stated rule. Its integer volume
table also differs, including entry 7=1642, and its `Run` loop advances while
`masterClock + 16 < target`, rather than the inclusive grid above. These do
not override the hardware rules or this model's declared phase and mix.
Its resampled/filtered audio is for listening, not sample equality.

The 1,200-frame no-input comparison uses SMS1 NTSC, no BIOS, FM off, empty
save/firmware directories, zero-filled RAM and the same initial VDP phase.
Each capture must use a **new, previously nonexistent home directory** and
verify that cartridge RAM is all zero before the first instruction. MesenCE
writes battery RAM when a no-input run stops, so a directory created empty
for an earlier run is not an empty-save setup for a later run. Reusing it
changes the game's initialization path and its sound sequence. Capture helpers
reject an existing output/home directory rather than deleting saved data.
Compare byte values and order within `floor(cycle/59736)` for each frame;
keep cycle differences separate. **Hardware documentation; inference (from authored probes).**
Instruction measurements found six MesenCE duration differences against the
[Zilog manual's instruction tables](https://map.grauw.nl/resources/cpu/z80.pdf):
`INC (HL)` and `DEC (HL)` take 10 instead of 11 T; `LD (HL),n` takes 12
instead of 10; indexed `INC/DEC (IX+d)` take 22 instead of 23 and
`LD (IX+d),n` takes 24 instead of 19. The same indexed path serves IY.
The primary reference's first
[patch](../tools/mesence/mesence-2.2.1-durations.patch) changes only these
durations in `SmsCpu.cpp`:
one internal T before indirect INC/DEC writes, no two-T delay in indirect
immediate LD, and a two-T rather than five-T indexed-address delay for that
LD. A TH/H-latch probe gives the manual's values in patched CE, Emulicious
and PSModern, with neighbouring opcodes unchanged from stock CE.

The [Zilog manual's](https://map.grauw.nl/resources/cpu/z80.pdf) interrupt
timing diagram samples INT on the rising edge of the final T-state, and its
I/O cycle (T1, T2, an automatic wait state, T3) transfers the port data within
T3. Stock CE samples INT at instruction end and performs a port access after
all four I/O T-states. Moving only the INT sample one T earlier also moves
every port access after an interrupt one T later relative to it: the frame and
line TH/H interrupt probes then shifted one H step and disagreed with
Emulicious and hardware measurements. The primary reference's second
[patch](../tools/mesence/mesence-2.2.1-int-boundary.patch) makes both changes:
INT is sampled at the start of each instruction's final T-state, and port
reads and writes happen at the start of T3. Instruction durations are
unchanged. In [Charles MacDonald's logic-analyzer measurements](https://www.smspower.org/forums/8161-SMSDisplayTiming),
the first instruction that accepts the frame IRQ and the first `IN` that sees
the new V counter end on the same cycle; this relation holds with both
changes, as in stock CE, but not with the INT change alone. A synthetic probe
that raises the frame IRQ during an instruction's final T-state stores `$0118`
in patched CE, PSModern and Emulicious (stock CE: `$0115`). The TH/H interrupt
probes, the duration probe and the VDPTest results are unchanged by this
patch. The manual samples I/O data partway through T3; the start of T3 is the
whole-T quantization these results select, not a sub-T measurement. NMI
timing is unchanged.

The third [patch](../tools/mesence/mesence-2.2.1-block-io.patch) corrects
INI/IND/OUTI/OUTD and their repeating forms. [Sean Young, section 4.3](https://www.smspower.org/uploads/Development/z80-documented.pdf)
specifies N from the transferred byte, H/C from the I/O sum and P/V from its
low bits and B. [David Banks' measurements](https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags)
specify the additional H/P/V and PC-derived flag bits while repeating. The
basic probe gives F=`40/53/51` in patched CE, PSModern and Emulicious. In 16
repeated-form cases PSModern and patched CE agree with the formulas during
and after repetition; Emulicious agrees on completion but omits the extra
repeat-phase effects. No PSModern CPU change is needed.

**Known reference difference (vertical-border collision).** CE does not draw
sprite pixels outside the visible picture, so it misses SMS1 collisions in
the vertical border (VDPTest 46). The game trace shows collisions beginning
on blank line 225 and persisting to the next status read at line 193; only
bit 5 differs. PSModern preserves that hardware-tested collision behavior.
Checkpoint comparison remains strict for every RAM byte and picture pixel.

Stock CE invokes its port-write callback after four I/O cycles; our write,
like the patched reference's, is at the start of T3. This explains why equal
write streams from stock CE need not have equal timestamps. No CPU or PSG
timing is adjusted to match the reference.

**Inference (from community source; capture API limitation).** In CE 2.2.1,
[`ScriptingContext::InternalCallMemoryCallback`](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/Debugger/ScriptingContext.cpp)
converts non-relative callback domains through
[`SmsConsole::GetAbsoluteAddress`](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsConsole.cpp),
which discards the port domain. A direct `smsPort` write callback receives
nothing. External capture scripts observe the resulting bank-0 `smsPrgRom`
addresses `$40`–`$7F` instead, with an earlier relative `smsMemory` write
callback over `$0000`–`$FFFF` excluding actual memory writes at those cycles.
An external synthetic program validates this method against all 1,611 known
writes, their order and their cycles: immediate OUT with varying A, OUT (C),D
with varying B, OUTI/OUTD and OTIR/OTDR, including B=0's 256 repetitions,
on `$7F`, `$7E` and `$40`. Writes to memory at the alias addresses are excluded.
An independent instruction-boundary OUT-family trace gives the same events.
CE's SMS CPU discards the upper port byte before notifying its memory manager;
varying A/B therefore does not lose writes in this version. This routes the
original port-write notifications without decoding game instructions or
changing the emulator. The reference WAV is captured through CE's native
`WaveRecord` API, using its unmodified audio engine.

**ROM run; reference emulator.** With independently verified zero initial
cartridge RAM, both ROMs produce 5,873 writes over 1,200 no-input frames,
at most 36 in one frame. Every frame's bytes and order match MesenCE 2.2.1.
Matched write timestamps differ by -16 to +27 CPU cycles (PSModern minus CE).
The first IRQ entry is 462953 in PSModern and 462963 in CE. A reused,
initialized CE save instead gives its first IRQ at 342682 and 5,877 writes;
this is a different starting machine value, not a PSG timing discrepancy.

## Release packaging

`tools/package-release.ps1` builds the Windows release zip (`Get-Help` shows
its parameters). It reads the version from `CMakeLists.txt`, refuses a dirty
git tree, an output folder inside the repository and an existing zip,
`.partial`, staging or build folder, exports `HEAD` with `git archive` into a
temporary folder under the output folder, builds only the `psmodern` target
there (RelWithDebInfo, tests off, so it needs neither Python nor ROMs) and
stages that executable. It then requires exactly the DLLs `SDL2.dll`,
`libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll`, all from
the UCRT64 `bin` folder, runs the staged `psmodern.exe --version` and requires exactly
`PSModern <version>`, and writes the zip as `.partial` before renaming it.
`-KeepBuild` keeps the temporary build folder. `-BuildDir` packages an
existing build tree instead; it requires `-AllowDirty`, is only for testing
the script, and `-AllowDirty` adds `-dirty` to the name. The zip,
`PSModern-<version>-windows-x64.zip`, holds the stripped `psmodern.exe`, the
runtime DLLs, `LICENSE.txt`, the player's `README.txt`
(`tools/release/README.txt`), `THIRD-PARTY.txt` and `licenses\`; it never
holds ROMs, saves or debug files. See [dependencies](dependencies.md#release-package).
Stripping is not byte-deterministic, so record the zip's SHA-256 instead of
expecting a rebuild to match. `THIRD-PARTY.txt` names the full commit the
zip was built from; the script ends by printing it, and the tag `v<version>`
must point to that commit.

Steps:

1. Run the complete suite on a clean, committed tree.
2. Run `tools/package-release.ps1 -OutputDir <folder outside the repository>`
   (the tag does not have to exist yet; create `v<version>` on the printed
   commit before publishing).
3. Extract the zip into a clean folder and run `psmodern.exe --version` with
   `PATH` limited to System32, which proves the DLL set is complete.
4. Play-test the exact zip ([release play-test](playtests.md#release-play-test)).
5. Tag and publish the GitHub release only with the maintainer's
   authorization.
