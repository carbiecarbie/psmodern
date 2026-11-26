# PSModern - Project Status

**Milestone 19 was completed and accepted on 2026-11-25.** It changed
nothing the player sees: the tests now play each cartridge from a new game to
the title after the credits, through the rescue of the petrified companion,
and no stop was found; the [plan](milestone-19-plan.md) records the results
and the route's limits. The latest published release, **1.3.1** (2026-11-23),
ships the Windows x64 zip and the signed Android APK
(`PSModern-1.3.1-android-arm.apk`, ARM64 and ARMv7, `versionCode 10301`),
covering through milestone 17 plus the fix of the stop at the petrified
companion; milestone 19 changed no game code and needs no release. See the
[project history](project-history.md).

Phantasy Star runs from the player's US Rev-A or Brazilian cartridge image.
The whole game runs in C++ (milestone 14, see its [plan](milestone-14-plan.md)).
The program has no Z80: interrupts are
accepted in C++ at the exact instruction, and cartridge code that has no C++
version stops the game with a message. The Z80 survives only in the tests, as
the original that the C++ is compared with. Optional features include saved encounter and experience
rates, a dungeon automap, session-only 2x/4x fast-forward, changeable
controls, language selection, a widescreen view and the Japanese cartridge's
FM sound. Without saved settings, rates are original, the automap and
widescreen are off, the sound is PSG and controls follow 0.2.0; acceleration
starts at 1x.

Replacements are checked against the original, call by call and frame by
frame, with exact state and timing. A private study base explains how the
original code works. The [roadmap](roadmap.md) orders the next milestones; the
maintainer starts each explicitly.

## What can be played

The whole game, as on the cartridge, plus saved rate settings, a dungeon
automap, fast-forward, changeable controls, live language selection and an
optional widescreen view. Milestone 12 changed how PSModern's own screens
look (Options, Controls, start) and no feature of the game: with every option
off the game plays and looks as it did. Milestone 19 played the whole story
on both cartridges in the tests, one route each, without a stop.

- **On Android**, open PSModern and select a supported cartridge in the system
  file picker. The app remembers an imported copy. Play in portrait or
  landscape using the cartridge-style touch controls or a controller.
  [Android controls](player-controls.md#android-touch-controls) explains the layout.
- **On Windows**, double-click `psmodern.exe` to reach the start screen: the
  first time, pick an uncompressed `.sms` file of the US Rev-A or the
  Brazilian Tec Toy cartridge; it is remembered, and any other file is refused
  with a message. One PLAY entry starts the game; ADD ROM makes the other
  language available; the start screen opens Controls and Language. Or run
  `psmodern.exe <rom-path>` from a terminal. See
  [development](development.md#start-screen).
- **Picture and sound:** the original 256x192 picture, scaled by whole steps
  in a resizable window with black bars (Alt+Enter toggles fullscreen), and the
  original sound chip, resampled for the audio device, at normal tempo and
  pitch also at 2x/4x.
- **Japanese FM sound (PSG by default):** with the player's Japanese
  cartridge added through ADD ROM (identified by its digest, a sound source
  and never a game), the SOUND row of Options switches between PSG, the US
  and Brazilian sound, and FM, the Japanese release's music and effects on
  its FM chip. The switch is heard and saved at once, with the music at the
  same point. A private Japanese sound driver in C++ runs beside the game
  from power on and hears every request; the game, its timing and its saves
  do not change. If that engine fails, the sound returns to PSG until power
  cycle. Recording ignores the choice.
- **Widescreen (off by default):** the WIDESCREEN row of Options shows the
  world map, the towns and the spaceship flight at 344x192 (16:9): the original
  256 pixels in the centre and 44 more on each side, drawn with the
  cartridge's own tiles and colours (in space, from the starfield). Broken ice
  stays broken in the sides after a screen change (fixed in milestone 11).
  Battles, dialogue, shops, houses, dungeons, scenes and the Options,
  Controls, Language and automap screens stay at 256 pixels, centred between
  black bars; the start screen stays 256. While the option is on, the window
  keeps the wide size on every screen. The choice is saved with the shared
  options; recording mode ignores the Options shortcut and keeps widescreen off.
  The machine is the
  same either way: the option only changes what is drawn.
- **Controls:** [controls](player-controls.md) lists every key and button.
  Keyboard and controller work together; the first controller found is used,
  also when plugged in during play. The defaults are those of release 0.2.0
  (arrows, Z/X, Enter, Backspace, F5, Tab, Escape, M and Alt+Enter on the
  keyboard; D-pad, A/B, Start, Back, RB, Y and X on a controller).
  The Controls screen (from the start screen or Options) sets one key and one
  controller input for each of 13 actions; a key in use swaps, the triggers
  can be set like buttons, and fixed keys keep the player from being locked
  out. SAVE applies the choice at once. The screen uses the same windows as
  Options, with a small window for the controller state.
- **Fast-forward:** each Speed press cycles 1x, 2x, 4x, then 1x, with an
  indicator while accelerated and music at normal tempo. Options and loss of
  focus temporarily use 1x; new sessions, F5 and recording start at 1x.
- **Language:** on the start screen Language has its own SAVE/CANCEL. In
  Options it is the sixth row: Left and Right step through AUTO, ENGLISH and
  PORTUGUES (each in its own words; a language without its ROM is skipped), and
  each step is saved and switches PSModern's screens at once; the game's own
  text changes when Options closes, and a failed save restores the previous
  choice. AUTO follows the ROM played. A saved choice is global, the scene and
  progress are kept across a switch, and if the preferred ROM is absent the
  available language is used with a warning.
- **Saves:** the game's own save menu writes one shared cartridge file,
  `phantasy-star.sav`, under `%APPDATA%\PSModern\shared` on Windows or
  `Android/data/org.psmodern.game/files/shared/` on Android, with options and automap
  companions beside it, a `.bak` of the previous file before the first write
  of a session, and a lock against concurrent use. First use imports old US/BR
  files without modifying them. The parent folder holds `roms.txt`,
  `controls.txt`, `language.txt` and `sound.txt` (and on Windows
  `sound-rom.txt`), each written only when its own choice is saved.
- **Automap:** with the option on, M or controller X while walking in a
  dungeon opens a map of the current floor (the game waits, the music goes
  on). It shows only what the party has seen; a pitfall appears once stood on,
  a fake wall becomes floor once walked through, and bosses, guardians and
  scripted events are never marked. Exploration is recorded even with the
  option off and kept with each saved game. FLOOR 1 is the lowest floor with an
  exit to the outside, FLOOR 2 and up above it, FLOOR B1 and down below it; other
  floors cannot be viewed.
- **Options (off by default):** two flags after the ROM path, each a percentage
  of the original (100 is the original game): `--encounter-rate` 0, 25, 50, 100
  or 200 (how often random fights start, on the map and in dungeons; story and
  fixed fights do not change) and `--experience-rate` 100, 200 or 400 (how much
  experience each fight gives; a character still gains at most one level per
  fight). Other values are refused; missing saved options use 100/100; explicit
  flags override only their field.
- **In-game options:** the Options shortcut (Escape or Y) opens one page of
  three windows in the game's own style (cartridge font and borders) during
  free exploration in towns, on the world map and in dungeons; the scene stays
  behind it and music continues. At the left are the seven rows (ENCOUNTERS,
  EXP / MESETA, AUTOMAP, WIDESCREEN, CONTROLS, LANGUAGE, SOUND) with their
  values in grey; beside the selected row a window lists every choice, the
  current one in yellow; at the bottom a window explains the row, its last
  line showing the state (SAVES ON EXIT, SAVES WHEN CHANGED for Language and
  Sound, saved and waiting, or save failed). Left/Right changes a value directly;
  button 1 on CONTROLS opens its own screen, which saves independently;
  leaving with button 1, button 2 or the shortcut saves the displayed values.
  Combat, title, dialogue and other menus ignore the shortcut. The
  explanations name 100% as the original and recommend encounters 25% and
  experience/meseta 400%. In Portuguese ON/OFF read SIM/NAO and the automap
  is AUTOMAPA (Maintainer decision, 2026-11-15).
- **Persistence:** a companion file retains preferences without changing the
  cartridge save format. Format 2 (the automap) is refused by release 0.1.0,
  and release 0.2.0 refuses a file saved with encounters 25; format 3
  (widescreen) is written only while a record has widescreen on, and formats 1
  and 2 read as widescreen off. The reader also imports play-test format 4,
  keeping all current preferences and dropping the retired switch. Battle meseta follows the experience rate;
  other money is unchanged. Recording ignores the shortcuts and keeps no map
  ([development](development.md#options), [automap](development.md#automap)).

## Architecture overview

The design is in [design](design.md) section 5. Milestone 1 built its lowest
layer.

| Part | Folder | What it does |
| --- | --- | --- |
| Console | `src/hw/`, `src/machine/` | The register file, memory and mapper, cartridge RAM, ports, VDP, PSG, the FM chip (Nuked-OPLL in `third_party/`), pads, Pause and Reset, driven by one cycle counter |
| ROM | `src/rom/` | Identifies the two supported cartridges, and the Japanese one as a sound source, by SHA-1 |
| Game | `src/game/` | Cartridge-driven C++ game routines, the Japanese sound driver, optional game rules, dungeon exploration and map-screen readers |
| Program | `src/app/` | SDL window, sound (with the private FM engine), input and its bindings, save files, the start, Options, Controls, Language and map screens (the first three in the game's window style), `WideView` (the widescreen renderer), frame pacing, command-line options |
| Android host | `android/`, `src/app/` | Android activity, document picker, app storage, touch controls and SDL lifecycle |
| Recordings | `src/replay/` | Input recordings, the headless replay runner, lockstep and frame verification, routine censuses |
| Oracle | `src/oracle/`, `third_party/` | Test-only: the pinned Z80 core and a machine running the original, for lockstep. Not linked into `psmodern.exe` |
| Reference | `tools/` | The driver that replays recordings in a patched MesenCE, the comparison tool and the coverage census |
| Tests | `tests/` | Unit, conformance, ROM, probe, checkpoint, stop-sweep and playthrough tests |

Some properties matter for the next milestones:

- **Exact and deterministic recordings.** The same recording always gives the
  same frames, byte for byte; Options shortcuts are ignored while recording.
- **Copyable.** The whole machine copies as one value, so a run can be
  forked and compared. Immutable ROM images are shared.
- **Verified replacements.** Every instruction the program runs is rendered
  by a registered C++ segment, each checked against the original running
  beside it. An address with no segment, or a state a segment does not
  handle, stops the game with a message.
- **Timing.** Each instruction accounts for the original CPU cycles, and
  interrupts are accepted at their original instruction boundaries, also
  inside a segment. Long copies resume from the remaining byte count and the
  two pointers.
- **Host observations.** The frame driver sees the game at the points used
  for Options, language switching, saves, sound and the automap. When a
  replacement begins at one of these points, the callback runs before the
  machine decides whether the replacement can execute.
- **Widescreen is a read-only presentation layer.** After each frame, while
  the option is on, the driver copies video memory, palette, registers and a
  few work-RAM bytes; `WideView` and `MapScreens` read the maps from the
  player's ROM and draw the bands. A frame that is not consistent is shown as
  the 256 centre between black bars. The virtual Master System
  never sees the option; tests prove the machine equal with it on or off every
  frame, and they passed unchanged over the rewritten map.
- **The automap only reads.** It samples floor, position, facing and layout
  at the dungeon's walking call (`00:0F48`) and stores, restores or clears
  the exploration after a save (`00:1D2D`), a Continue (`00:06B4`) and a new
  game (`00:05F4`), in `<save>.automap`, keyed by a digest of the saved game
  block; an unknown game or unusable file gives an empty map, never a wrong one.

How the console works, in plain words, is in
[how the Master System works](how-it-works/master-system.md). Building,
testing, recordings and the reference are in [development](development.md).
The game-system chapters cover [controller reading](how-it-works/controls.md),
[random numbers and encounters](how-it-works/random-numbers.md),
[party records and experience](how-it-works/party.md),
[the sound driver](how-it-works/sound.md),
[walking in the dungeons](how-it-works/dungeons.md) (with how the automap reads
the game), [text](how-it-works/text.md) and
[the world map and the towns](how-it-works/world-map.md) (with how widescreen
reads the map) and [menus and windows](how-it-works/menus.md).

The annotated disassembly is private (design section 6): it rebuilds the
player's ROM exactly without holding any ROM data, and its catalog names
the routines and RAM fields studied so far. This repository cites original
code only as `bank:address`, and each citation is checked against that
catalog.

## Rewritten and original systems

| Game system | Implementation |
| --- | --- |
| Walking, vehicles and dungeon exploration | C++ handles movement, map transfers, dungeon loading, the 3D view and animations. |
| Battles, items, spells and party | C++ chooses actions and targets, resolves effects and awards experience and meseta. |
| Menus, text, saves and scenes | C++ handles selection, dialogue, name entry, saving and Continue, story events and the ending. Text and graphics come from the player's ROM. |
| Music, sound effects and display updates | C++ interprets the cartridge's sound commands and schedules chip writes at the original cycles. The Japanese cartridge's FM driver is also C++, run privately for the listener. |
| Instruction execution and interrupts | C++ renders every instruction and accepts every interrupt. The original Z80 runs only in the tests, beside the C++, for comparison. |

See [battles](how-it-works/battle.md), [interactions](how-it-works/interactions.md),
[scenes](how-it-works/scenes.md), [interrupts](how-it-works/interrupts.md) and
the [per-recording census](coverage.md).

## How it is verified

- **Recordings.** All seven reference components matched on 222,246 frames
  from 20 US and seven Brazilian checkpoint recordings, including the ending,
  defeat, pause, the flute and Brazilian name entry. They run in the complete
  suite. See the [census](coverage.md) and the
  [recording and save provenance](development.md).
- **The playthrough (milestone 19).** One chain of recordings per cartridge
  goes from power-on with an empty save to the title after the credits: 25 US
  links (673,967 frames) and 27 Brazilian (685,128), each continued from the
  previous link's save with only strength and money edited, and a chain test
  over every step, the rescue, the bosses, the credits and the title. 89
  shorter recordings (44 US, 45 Brazilian) add shops, healing places and rare
  branches; 6 start from an unchanged chain save, 79 from an edited save not
  verified as reachable by play, 4 from known artificial states. Each matches
  the reference at every frame, under per-instruction lockstep, and runs once
  as the plain game. These 378 tests, the `playthrough` group, are off by
  default and run only at a milestone closure: about 32 minutes at `-j 16`
  ([development](development.md#playthrough-chain)).
- **Four kinds of evidence** are told apart in the [census](coverage.md):
  checkpoints, the playthrough group, prepared-state routes and state probes.
  Recordings reach 2,794 of 3,156 segments; 362 are reached by none (4 of
  them by a route) and 64 by US recordings only, each with a reason in
  `tools/coverage-reasons.tsv`. The reference is MesenCE 2.2.1 with four
  patches; the fourth (milestone 19) sets bit 3 of F after `LD A,I` and
  `LD A,R` as the Z80 does.
- **Routine checks.** Lockstep comparisons at every instruction boundary
  include CPU state, cycles and ordered port accesses. The 27 recordings and 96 cartridge tests account
  for the 14,195 listed instruction addresses through measured execution or
  an individually documented role. State probes cover branches the recorded
  journeys missed.
- **The options** are checked against their formulas over every input, with
  interrupt and frame boundaries forced at each cycle, and by running the
  recordings with the options on. At 100 the game is byte-identical to the
  original. The default controls are release 0.2.0's, checked over every
  combination of keys and buttons.
- **The automap and widescreen change nothing in the machine:** on nine
  recordings (7 US, 2 Brazilian; automap) and on 23 recordings (15 US, 8
  Brazilian; widescreen) the
  machine is equal after every frame with the option off and on, and the
  widescreen option-off picture equals release 0.4.0's. The widescreen
  renderer's centre equals the virtual video chip's picture, its bands equal a
  later frame showing the same world position, and state probes cover both
  wraps, a vehicle, the road, the spaceship's eight routes, power cycle, ROM
  swap and the option turned on mid-stay or during a transfer.
- **Text and languages:** both-ROM segment probes and a Brazilian text
  recording check the distinct text paths; switching and legacy-import tests
  cover preserved state, cancellation, missing or locked files, concurrency
  and crashes.
- **FM sound:** each of the Japanese driver's 191 C++ pieces matched the
  original Japanese driver on the Z80 over every request, which in turn
  matched the reference emulator's FM writes on the opening. With FM on and
  off the game is identical at every frame (state, cycles, picture, PSG and
  saves), and a forced engine failure falls back to PSG.
- **Review and acceptance.** Independent reviews covered the timing mechanism,
  game routines, the Android host, the FM sound and the playthrough harness.
  The maintainer passed the FM play-test on Windows and the S24. At milestone
  19 the Windows suite passed 438/438 with zero skips in 1,735 seconds, and
  the playthrough group passed 378 of 378 with no skip in 2,409.73 s. On the S24, the Android build
  sustained 1x, 2x and 4x, also with FM ([milestone 16](milestone-16-plan.md#verification),
  [milestone 17](milestone-17-plan.md#verification)).

## Known gaps

- **Options remain deliberately small.** Other features are later work on the
  [roadmap](roadmap.md).
- **Controls have one key and one controller input per action,** only the
  first controller is used, and there is no second pad or shortcut back to the
  start screen during play. A damaged `controls.txt` is reported only on the
  console; a trigger held when a controller connects is ignored best-effort.
- **Recording coverage.** 362 segments are reached by no recording and 64 by
  US recordings only; state probes exercise them. Of their 426 reasons, 161
  rest on a run (the segment runs inside another's body, which the census
  does not count), 10 on a reading of the code and 153 on inference; 102 say
  the trigger is unknown ([coverage](coverage.md)).
- **One route per cartridge.** Unsaved work RAM restarts at every link;
  other routes and optional places are visited only by the shorter
  recordings, mostly from edited saves. Edits are checked field by field, not
  as whole saves (a raised level keeps its earlier spell counts until the
  game refreshes them). A boss link shows one winning sequence, not a win
  rate. Between closures a maintenance fix runs only the complete suite.
- **Playthrough tooling.** The group takes about 32 minutes against the plan's
  mark of 45. The reference capture failed twice inside `tools/reference.lua`
  and passed when repeated (heavy load suspected, not established); it fails
  loudly and writes no manifest. MesenCE also lacks the rule that an interrupt
  right after `LD A,I` or `LD A,R` resets P/V; no recording has shown it, and
  it stays unpatched (Maintainer decision, 2026-11-24).
- **An unusable map file is kept only briefly.** `.automap` that cannot be
  read becomes `.automap.bak` at the next save, and the first save of the next
  session replaces that backup ([development](development.md#automap)).
- **Some automap checks cover the US only.** Map passivity does not run on the
  Brazilian boss recording or on `places` (a town recording, where the
  automap does not apply); the map's and the menus' own recordings and
  play-tests are US.
- **Some tests are slow.** The two first dungeon-walk checkpoints take
  slightly over a minute, and the map's terrain and mode probes about 90 and
  250 seconds each, above the guideline of about 60 seconds per test; they
  carry the `rom` label without `fast`.
- **The experience ceiling differs only with the option on.** The original can
  in principle stop a character's levelling at a total ceiling, but no
  sequence of battles reaches it ([original quirks](original-quirks.md)). With
  experience at 200 or 400 PSModern keeps one level per fight, also for a
  character at the ceiling (Maintainer decision, 2026-11-11).
- **Unreached code stops the game.** Cartridge code that no recording, probe
  or static walk reaches has no C++ version; if it ever runs, the game stops
  with a message and keeps the save of the last frame end
  ([milestone 14 plan](milestone-14-plan.md)). The static walk is a test
  since 1.3.1, after a player met such a stop at two code-table targets, and
  stricter since milestone 19.
- **The program is slower and larger than before milestone 14:** about
  1,720 microseconds per emulated frame against 1,495, and a 9.2 MB
  executable against 5.5 MB, a cost of rendering every instruction. It
  still uses about a tenth of each frame's time; the maintainer accepted it
  (Maintainer decision, 2026-11-19).
- **Some details of the first Master System's video chip are unmeasured.**
  PSModern follows the reference and [development](development.md) labels
  them Unknown; the US recordings reach two (video memory accesses at two
  moments), where the real console's behavior is not known.
- **Widescreen draws a frame behind in rare cases.** In about 0.55% of map
  frames the cartridge rewrites tile patterns in the top lines after the
  copy, so the bands there can lag one frame behind a tile animation; after a
  screen change the sides keep a broken-ice trail until the cartridge shows it
  whole.
- **Repeated stars in the spaceship flight.** In space the bands are the
  starfield wrapped as the video chip would draw it, so stars at the right
  repeat those at the left; on some routes the last take-off frames show map
  bands beside a partial top row of stars (at most 7 lines, 3 frames).
- **The menus keep the cartridge's look** (a new look is optional and
  undecided, see the [roadmap](roadmap.md)), and the redone Options page covers
  more of the scene than the old window did.
- **No aspect correction of the picture** yet.
- **FM sound limits.** The FM engine runs whenever the Japanese image is
  registered, also while PSG is heard; 4x with FM is measured only on a
  64-bit phone. Switching can sound an already-held note again, and effect
  `AF` ends about half a second later in FM than in PSG, as on the Japanese
  cartridge. FM loudness is a fixed gain, confirmed by ear in the play-test.
- **Audio under heavy host load.** Windows can delay the game beyond its
  150 ms audio reserve. After an underrun the next samples resume immediately.
  A 30-minute WASAPI session with the revised audio queue recorded zero
  underruns and zero queue resets. [Audio measurements](development.md#audio-and-pacing).
