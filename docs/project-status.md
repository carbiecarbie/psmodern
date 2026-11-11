# PSModern - Project Status

**Milestone 6 (fast-forward) is complete,** accepted on 2026-11-11.
**Release 0.1.0**, the first public Windows release, adds a start screen
that remembers the player's ROMs ([plan](release-0.1.0-plan.md)).
Phantasy Star runs from the player's own ROM on a virtual Master System, in
the US and the Brazilian version. Controller polling, the random-number
routine, the random-encounter checks, experience, battle setup and money award now run in C++;
the rest of the original program runs on the virtual Z80. Optional features
include saved encounter and experience/battle-meseta rates, plus session-only
2x/4x fast-forward. New profiles use original rates; acceleration starts at 1x.
Milestones 1, 2 and 3 were accepted on
2026-11-08, 2026-11-09 and 2026-11-09.

Replacements are checked against the original, call by call and frame by
frame, with exact state and timing. A private study base explains how the
encounter checks, experience and the sound driver work. The
[roadmap](roadmap.md) orders the next milestones. The maintainer starts each
milestone explicitly.

## What can be played

The whole game, as on the cartridge, plus saved rate settings and fast-forward.

- **Starting:** double-click `psmodern.exe` (no arguments) to reach the start
  screen: the first time, pick an uncompressed `.sms` file of the US Rev-A or
  the Brazilian Tec Toy cartridge; it is remembered, and any other file is
  refused with a message. Or run `psmodern.exe <rom-path>` from a terminal.
  The window title names the version, active rates and any pending change.
  See [development](development.md#start-screen) for both paths.
- **Picture:** the original 256x192 picture, scaled by whole steps in a
  resizable window, with black bars. Alt+Enter toggles fullscreen.
- **Sound:** the original sound chip, resampled for the computer's audio
  device. Music and effects keep normal tempo and pitch while running at 2x/4x.
- **Controls:**

  | Console | Keyboard | Game controller |
  | --- | --- | --- |
  | Directional pad | Arrows | D-pad or left stick |
  | Button 1 / 2 | Z / X | A / B (bottom / right) |
  | Pause | Enter | Start |
  | Reset | Backspace | Back |
  | Power off and on | F5 | - |
  | Speed: 1x / 2x / 4x | Tab | Right shoulder (R/RB) |

  Keyboard and controller work together. The first controller found is used,
  also when it is plugged in during play.
- **Fast-forward:** each Tab/RB press cycles 1x, 2x, 4x, then 1x; holding
  does not repeat. An indicator appears while accelerated. Music and effects
  keep normal tempo at 2x/4x and continue smoothly when returning to 1x,
  without an audio backlog.
  Options and loss of focus temporarily use 1x and restore the chosen speed
  afterward. New sessions and F5 start at 1x; recording remains at 1x.
- **Saves:** the game saves through its own menu, as on the cartridge. Each
  version keeps one file in `%APPDATA%\PSModern\`. Before the first write of
  a session, the previous file is copied to a `.bak` file. Only one running
  copy of PSModern may use a save file; a second one is refused.
- **Options (off by default):** two flags after the ROM path, each a
  percentage of the original, where 100 is the original game.
  - `--encounter-rate 0`, `50`, `100` or `200`: how often random fights
    start, on the map and in dungeons. 0 turns random fights off, 50 halves
    them, 200 doubles them. Story and fixed fights do not change.
  - `--experience-rate 100`, `200` or `400`: how much experience each fight
    gives. The number on screen is what every character gains. A character
    still gains at most one level per fight.

  Any other value is refused. New profiles use 100/100. Saved choices load on
  startup; explicit flags override only their corresponding field.
- **In-game options:** Escape or controller Y opens the two-row window during
  free exploration in towns, on the world map and in dungeons, with released
  controls. The scene stays behind it and music continues. Combat, title,
  dialogue and other menus ignore the shortcut. Left/Right changes a rate;
  Z/A saves and closes, X/B or Escape/Y discards the draft. Each 100% is
  DEFAULT; encounters 50% and experience/meseta 400% are RECOMMENDED.
- **Persistence:** a companion file retains preferences without changing the
  cartridge save format. Battle meseta follows the experience rate; other
  money sources remain unchanged. Recording ignores the shortcut and uses
  original rules; raw-Z80 mode exposes read-only options. Details are in
  [development](development.md#options).

## Architecture overview

The design is in [design](design.md) section 5. Milestone 1 built its lowest
layer.

| Part | Folder | What it does |
| --- | --- | --- |
| Console | `src/hw/`, `src/machine/` | Z80, memory and mapper, cartridge RAM, ports, VDP, PSG, pads, Pause and Reset, driven by one cycle counter |
| ROM | `src/rom/` | Identifies the two supported cartridges by SHA-1 |
| Game | `src/game/` | Registered C++ routines (controller, random numbers, encounters, experience, battle setup and money) and the options |
| Program | `src/app/` | SDL window, sound, input, save files, frame pacing, command-line options |
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
  or show text can be rewritten the same way.

How the console works, in plain words, is in
[how the Master System works](how-it-works/master-system.md). Building,
testing, recordings and the reference are in [development](development.md).
The game-system chapters cover [controller reading](how-it-works/controls.md),
[random numbers and encounters](how-it-works/random-numbers.md),
[party records and experience](how-it-works/party.md), and
[the sound driver](how-it-works/sound.md).

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
| Menus, text, sound driver, native saves and the rest | Original Z80 code; host options use the original field-menu wait |

On the twelve checkpoint recordings, controller polling ran in C++ on all
93,555 calls. The other routines ran in C++ on almost every call; the few
fallbacks are calls where a frame boundary could fall inside the routine.
The generated [coverage census](coverage.md) gives the counts per piece and
recording. It describes these recordings, not every possible route through
the game.

## How it is verified

- **Frame-by-frame comparison** with a patched MesenCE 2.2.1, on twelve
  recordings, six per version: the opening, the first battle, the first
  dungeon, a later town, a boss fight and a deep dungeon. All 124,920 frames
  match in the picture, RAM, video memory, palette, cartridge RAM, CPU state
  and sound writes, with the rewritten routines on and off.
- **Each C++ call** is checked against a copy running the original, including
  hidden machine state, cycles, R and opcode fetches. All previous/current
  button combinations are checked for both ROMs.
- **State probes** run each piece of the new routines against the original
  from many machine states, in both versions: the random-number routine's states,
  all 192 map region records, every threshold comparison, every reward value
  and the level-up cases. Because the recordings reach few encounter and
  experience paths (two of 192 map regions, one level gain), these probes
  carry the exactness there.
- **The options** are checked against their formulas over every input, with
  interrupt and frame boundaries forced at each cycle, and by running the
  twelve recordings with the options on. At 100 the game is byte-identical to
  the original.
- **Conformance programs:** the Cringle, Rak and Woodmass Z80 suites, and
  the VDPTest results for the SMS1 video chip.
- **CI** on a clean Windows machine runs every test that needs no ROM. Of
  the external conformance suites it runs only the three Rak Z80 tests. The
  ROM, probe and reference tests run locally, as
  [development](development.md) describes.

## Known gaps

- **Options remain deliberately small.** Encounter and experience/meseta rates
  are available in-game and saved. Walking speed, remapping, automap and other
  features remain later work on the [roadmap](roadmap.md).
- **Recording coverage is thin where the new routines act.** The recordings
  reach few encounter paths and one level gain. State probes carry the
  exactness; new recordings come when a milestone needs a route the existing
  ones miss.
- **The experience ceiling differs only with the option on.** The original
  can in principle stop a character's levelling at a total ceiling, but no
  sequence of battles reaches it ([original quirks](original-quirks.md)). With
  experience at 200 or 400 it is reachable, and PSModern then keeps one level
  per fight, also for a character at the ceiling (Maintainer decision,
  2026-11-11).
- **The registered-routine mechanism has limits.** It still refuses routines
  that change interrupt sources, adjust the stack or write mapping registers
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
- **Controls are fixed.** Remapping and the second pad are later work, as
  is aspect correction of the picture.
- **Rare sound stalls are not ruled out.** One of three measured sessions
  had stalls of about 90 ms; their cause is not known.
