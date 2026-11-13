# PSModern - Project Status

**Milestone 8 (full controller support) is complete,** accepted on
2026-11-13 ([plan](milestone-8-plan.md)): every key and controller button
can be changed on an in-game Controls screen. **Release 0.1.0**, the first
public Windows release, added a start screen that remembers the player's
ROMs ([plan](release-0.1.0-plan.md)); **release 0.2.0** (2026-11-12) added
the automap.
Phantasy Star runs from the player's own ROM on a virtual Master System, in
the US and the Brazilian version. Controller polling, the random-number
routine, the random-encounter checks, experience, battle setup, money award
and walking in the dungeons now run in C++; the rest of the original program
runs on the virtual Z80. Optional features include saved encounter and
experience/battle-meseta rates, a dungeon automap, session-only 2x/4x
fast-forward and changeable controls. New profiles use original rates with
the automap off and the 0.2.0 controls; acceleration starts at 1x.
Milestones 1 to 7 were accepted between 2026-11-08 and 2026-11-12.

Replacements are checked against the original, call by call and frame by
frame, with exact state and timing. A private study base explains how the
encounter checks, experience, the sound driver and dungeon walking work. The
[roadmap](roadmap.md) orders the next milestones. The maintainer starts each
milestone explicitly.

## What can be played

The whole game, as on the cartridge, plus saved rate settings, a dungeon
automap, fast-forward and changeable controls.

- **Starting:** double-click `psmodern.exe` (no arguments) to reach the start
  screen: the first time, pick an uncompressed `.sms` file of the US Rev-A or
  the Brazilian Tec Toy cartridge; it is remembered, and any other file is
  refused with a message. The start screen also opens the Controls screen.
  Or run `psmodern.exe <rom-path>` from a terminal.
  The window title names the version, active rates and any pending change.
  See [development](development.md#start-screen) for both paths.
- **Picture:** the original 256x192 picture, scaled by whole steps in a
  resizable window, with black bars. Alt+Enter toggles fullscreen.
- **Sound:** the original sound chip, resampled for the computer's audio
  device. Music and effects keep normal tempo and pitch while running at 2x/4x.
- **Controls:** [controls](player-controls.md) lists every key and button
  for players. The defaults are those of release 0.2.0:

  | Console | Keyboard | Game controller |
  | --- | --- | --- |
  | Directional pad | Arrows | D-pad or left stick |
  | Button 1 / 2 | Z / X | A / B (bottom / right) |
  | Pause | Enter | Start |
  | Reset | Backspace | Back |
  | Power off and on | F5 | - |
  | Speed: 1x / 2x / 4x | Tab | Right shoulder (RB) |
  | Options window | Escape | Y |
  | Dungeon map (Automap option on) | M | X |
  | Window or fullscreen | Alt+Enter | - |

  Keyboard and controller work together. The first controller found is used,
  also when it is plugged in during play. Button names are positional (A is
  the bottom button, whatever is printed on the pad).
- **Changing the controls:** the Controls screen (from the start screen or
  the Options window) sets one key and one controller input for each of 13
  actions: the eight console inputs, Speed, Options, Map, Power cycle and
  Fullscreen. A key already in use swaps; the triggers can be set like
  buttons. Alt+Enter and the left stick are fixed, the keys of the
  directions and buttons 1 and 2 cannot be emptied, and the start and
  Controls screens answer fixed keys, so no choice locks the player out.
  SAVE applies the controls at once, menus and the map included.
- **Fast-forward:** each Speed press (Tab or RB by default) cycles 1x, 2x,
  4x, then 1x; holding
  does not repeat. An indicator appears while accelerated. Music and effects
  keep normal tempo at 2x/4x and continue smoothly when returning to 1x,
  without an audio backlog.
  Options and loss of focus temporarily use 1x and restore the chosen speed
  afterward. New sessions and F5 start at 1x; recording remains at 1x.
- **Saves:** the game saves through its own menu, as on the cartridge. Each
  version keeps one file in `%APPDATA%\PSModern\`. Before the first write of
  a session, the previous file is copied to a `.bak` file. Only one running
  copy of PSModern may use a save file; a second one is refused. Two
  companion files sit beside it without changing the cartridge format: the
  options, and the dungeon maps of the saved games (`.automap`). The same
  folder holds `roms.txt`, the start screen's remembered ROMs, and
  `controls.txt`, written only when the player saves on the Controls screen;
  without it the defaults apply, and a damaged file never stops the game.
- **Automap:** with the option on, M or controller X while walking in a
  dungeon opens a map of the current floor; M, X, Escape or B close it. The
  game waits and the music continues. The map shows only what the party has
  seen, each seen cell as what it really is (doors plain, locked or magic,
  stairs, exits that lead out, chests, opened or not). A pitfall appears once
  the party has stood on it, a fake wall becomes floor once walked through,
  and bosses, guardians and scripted events are never marked. Exploration is
  recorded even with the option off and kept with each saved game: Continue
  brings back that game's map, a new game starts empty. A floor label names
  the floor: FLOOR 1 is the lowest floor with an exit to the outside, FLOOR
  2 and up above it, FLOOR B1 and down below it (a PSModern convention, not
  something the original shows). There is no view of other floors.
- **Options (off by default):** two flags after the ROM path, each a
  percentage of the original, where 100 is the original game.
  - `--encounter-rate 0`, `25`, `50`, `100` or `200`: how often random
    fights start, on the map and in dungeons. 0 turns random fights off, 25
    keeps a quarter, 50 halves them, 200 doubles them. Story and fixed
    fights do not change.
  - `--experience-rate 100`, `200` or `400`: how much experience each fight
    gives. The number on screen is what every character gains. A character
    still gains at most one level per fight.

  Any other value is refused. New profiles use 100/100. Saved choices load on
  startup; explicit flags override only their corresponding field.
- **In-game options:** the Options shortcut (Escape or controller Y by
  default) opens the four-row window during free exploration in towns, on
  the world map and in dungeons, with released controls. The scene stays
  behind it and music continues. Combat, title, dialogue and other menus
  ignore the shortcut. Left/Right changes a row; button 1 saves and closes,
  button 2 or the Options shortcut discards the draft. Each 100% is DEFAULT;
  encounters 25% and experience/meseta 400% are RECOMMENDED, the pace of the
  Switch release's easy mode; the third row is AUTOMAP OFF (DEFAULT) or ON;
  the fourth, CONTROLS, opens the Controls screen and comes back.
- **Persistence:** a companion file retains preferences without changing the
  cartridge save format; since milestone 7 it is written in format 2, with
  the automap choice, which release 0.1.0 refuses (it then plays with the
  original settings); release 0.2.0 likewise refuses a file saved with
  encounters 25. Battle meseta follows the experience rate; other money
  sources remain unchanged. Recording ignores the shortcuts, uses original
  rules and keeps no map; raw-Z80 mode exposes read-only options and keeps
  no map. Details are in [development](development.md#options) and
  [development](development.md#automap).

## Architecture overview

The design is in [design](design.md) section 5. Milestone 1 built its lowest
layer.

| Part | Folder | What it does |
| --- | --- | --- |
| Console | `src/hw/`, `src/machine/` | Z80, memory and mapper, cartridge RAM, ports, VDP, PSG, pads, Pause and Reset, driven by one cycle counter |
| ROM | `src/rom/` | Identifies the two supported cartridges by SHA-1 |
| Game | `src/game/` | Registered C++ routines (controller, random numbers, encounters, experience, battle setup and money, dungeon walking), the options, the dungeon tables and the automap's exploration model |
| Program | `src/app/` | SDL window, sound, input and its bindings, save files and their companions, the start, Options, Controls and map screens, frame pacing, command-line options |
| Recordings | `src/replay/` | Input recordings, the headless replay runner, per-call and frame verification, routine censuses |
| Reference | `tools/` | The driver that replays recordings in a patched MesenCE, and the comparison tool |
| Tests | `tests/` | Unit, conformance, ROM, probe and checkpoint tests |

Some properties matter for the next milestones:

- **Exact and deterministic recordings.** Options shortcuts are ignored while recording.
  The same recording always gives the same frames, byte for byte.
- **Copyable.** The whole machine copies as one value, so a run can be
  forked and compared.
- **Verified replacements.** Registered C++ routines replace the original
  only where interrupts and frame boundaries permit exact timing. Other
  calls stay on the Z80. A developer switch disables replacements.
- **Segments.** A routine that calls original code, for instance to wait for
  a frame or show text, is cut into pieces. Each piece runs in C++ and ends
  by handing one original instruction to the Z80 core, so that cycles and
  interrupts come out as on the console. Later routines that wait for frames
  or show text can be rewritten the same way. Since milestone 7 a piece may
  also end with words held on the stack across an original call, with a
  conditional jump, or partway through a long loop, which splits the
  dungeon's table scans so they fit between frames. Pieces registered
  earlier use none of these and run as before. Shared helpers for the
  dungeon pieces form a small kit (`src/game/dungeon_kit.h`).
- **Host-observed instructions.** The program watches 13 original
  instructions (the Options and map hold, frame waits, the normal-tempo
  audio points and the automap's points) without changing anything.
  Activating the routines fails at once if any of them became the entry or
  exit of a C++ piece, and the automap's tests compare its events with
  routines on and off to catch one hidden inside a piece.
- **The automap only reads.** It samples floor, position, facing and layout
  at the dungeon's walking call (`00:0F48`), and stores, restores or clears
  the exploration after a save (`00:1D2D`), a Continue (`00:06B4`) and a new
  game (`00:05F4`). Each saved game's exploration goes into
  `<save>.automap`, keyed by a digest of the saved game block; an unknown
  game or an unusable file gives an empty map, never a wrong one.

How the console works, in plain words, is in
[how the Master System works](how-it-works/master-system.md). Building,
testing, recordings and the reference are in [development](development.md).
The game-system chapters cover [controller reading](how-it-works/controls.md),
[random numbers and encounters](how-it-works/random-numbers.md),
[party records and experience](how-it-works/party.md),
[the sound driver](how-it-works/sound.md), and
[walking in the dungeons](how-it-works/dungeons.md), which also explains
how the automap reads the game.

The annotated disassembly is private (design section 6): it rebuilds the
player's ROM exactly without holding any ROM data, and its catalog names
the routines and RAM fields studied so far. This repository cites original
code only as `bank:address`, and each citation is checked against that
catalog.

## Rewritten and original systems

| Game system | State |
| --- | --- |
| Controller polling (`00:0339`) | C++ by default; original Z80 fallback when needed |
| Random numbers (`00:05B1`) | C++ by default; original Z80 fallback when needed |
| Map encounter check (`01:5F63`, with the group choice `01:5FD8`) | C++ by default, in pieces; calls into original code run on the Z80 |
| Dungeon update with its encounter check (`00:0F3C`) | C++ by default, in pieces |
| Experience award (`00:170D`, with the per-character helper `00:1754`) | C++ by default, in pieces |
| Battle setup (`01:5FFE`) and money addition (`00:297A`) | C++ by default; battle meseta scales with the experience option |
| Dungeon walking: the input handler (`01:65EE`), steps, turns, the pitfall, doors, the exit lookup, the check for chests, fights and events, and the cell readers | C++ by default, eleven routines in 53 pieces; three short pitfall sequences that switch interrupts off stay on the Z80 |
| The 3D dungeon view and its animations, the dungeon load, the world-map transfer | Original Z80 code |
| Menus, text, sound driver, native saves and the rest | Original Z80 code; host options and the map use the original field-menu wait |

On the fifteen checkpoint recordings, controller polling ran in C++ on all
101,335 calls. The other routines ran in C++ on almost every call; the few
fallbacks are calls where a frame boundary could fall inside the routine.
The longest dungeon scan, split into chunks, runs in C++ on about 97% of
its calls in the boss recordings.
The generated [coverage census](coverage.md) gives the counts per piece and
recording. It describes these recordings, not every possible route through
the game.

## How it is verified

- **Frame-by-frame comparison** with a patched MesenCE 2.2.1, on fifteen
  recordings: six per version (the opening, the first battle, the first
  dungeon, a later town, a boss fight and a deep dungeon) and three US
  dungeon walks (stairs, back steps, a fake wall, a pitfall, chests, a door,
  a fixed fight, Continue and an exit to the world map). The twelve first
  recordings' 124,920 frames and every frame of the three walks match in the
  picture, RAM, video memory, palette, cartridge RAM, CPU state
  and sound writes, with the rewritten routines on and off.
- **The automap changes nothing:** on eight recordings the machine is equal
  after every frame in recording mode, with the option off and with it on
  but never opened, and the map's events are the same with routines on and
  off. Running the original 3D drawing with every unseen cell changed never
  changes the picture, so the map never shows more than the view could.
- **Each C++ call** is checked against a copy running the original, including
  hidden machine state, cycles, R and opcode fetches. All previous/current
  button combinations are checked for both ROMs.
- **State probes** run each piece of the new routines against the original
  from many machine states, in both versions: the random-number routine's states,
  all 192 map region records, every threshold comparison, every reward value,
  the level-up cases, and every dungeon cell value in every facing with all
  object and exit records. Because the recordings reach few encounter and
  experience paths (two of 192 map regions, one level gain) and some dungeon
  paths not at all, these probes carry the exactness there.
- **The options** are checked against their formulas over every input, with
  interrupt and frame boundaries forced at each cycle, and by running the
  twelve recordings with the options on. At 100 the game is byte-identical to
  the original.
- **The default controls are release 0.2.0's:** a test checks every
  combination of the default keys and controller buttons; recordings and
  replay never see the bindings.
- **The complete suite** has 159 tests; at the closure of milestone 8 all
  passed with none skipped.
- **Conformance programs:** the Cringle, Rak and Woodmass Z80 suites, and
  the VDPTest results for the SMS1 video chip.
- **CI** on a clean Windows machine runs every test that needs no ROM. Of
  the external conformance suites it runs only the three Rak Z80 tests. The
  ROM, probe and reference tests run locally, as
  [development](development.md) describes.

## Known gaps

- **Options remain deliberately small.** Encounter and experience/meseta
  rates, the automap and the controls are available in-game and saved.
  Other features remain later work on the [roadmap](roadmap.md).
- **Controls have one key and one controller input per action,** and only
  the first controller found is used; there is no second pad and no
  shortcut back to the start screen during play. A damaged `controls.txt`
  is reported only on the console, not in the window. A trigger held when a
  controller connects is ignored only on a best-effort basis: some drivers
  may still deliver it as a press.
- **Recording coverage is thin where the new routines act.** The recordings
  reach few encounter paths and one level gain. Thirteen dungeon pieces are
  reached by no recording and are covered by state probes only: locked and
  magic doors, exits that start a scene or an interaction, cascading pits
  and most chest, fight and event records ([coverage](coverage.md)). State
  probes carry the exactness; new recordings come when a milestone needs a
  route the existing ones miss.
- **An unusable map file is kept only briefly.** When `.automap` cannot be
  read, the next save keeps it as `.automap.bak` and writes a new one, but
  the first save of the next session replaces that backup in turn
  ([development](development.md#automap)).
- **Some automap checks cover the US only.** Map passivity runs on the first
  and deep dungeons of both versions, but not on the Brazilian boss
  recording; the map's own recordings and play-test are US.
- **Two checkpoint tests take slightly over a minute** (the first two new
  dungeon walks), above the suite's guideline of about 60 seconds per test.
- **The experience ceiling differs only with the option on.** The original
  can in principle stop a character's levelling at a total ceiling, but no
  sequence of battles reaches it ([original quirks](original-quirks.md)). With
  experience at 200 or 400 it is reachable, and PSModern then keeps one level
  per fight, also for a character at the ceiling (Maintainer decision,
  2026-11-11).
- **The registered-routine mechanism has limits.** It still refuses routines
  that change interrupt sources, adjust the stack beyond declared held words
  (so the pitfall's three interrupt-off sequences stay native) or write
  mapping registers
  other than the declared upper-bank one ([development](development.md#segments)).
  The [coverage census](coverage.md) tracks progress.
- **Study coverage is route-bound.** The study observed the six US
  recordings; routes they never reach, such as the ending, rare battle and
  sound paths, and most work-RAM fields are still Unknown.
- **Windows only** for the save location.
- **The Brazilian version is checked by its recordings and probes, not by
  play.** Development and play-tests use the US version (design 5.5); the
  Brazilian version's six checkpoints and its probes run in the tests.
- **Some details of the first Master System's video chip are unmeasured.**
  PSModern follows the reference there, and [development](development.md)
  labels them Unknown. The game reaches two of them, video memory accesses
  at two moments of the US recordings, where PSModern matches the reference
  but the real console's behavior is not known.
- **No aspect correction of the picture** yet.
- **Rare sound stalls are not ruled out.** One of three measured sessions
  had stalls of about 90 ms; their cause is not known.
