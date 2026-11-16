# PSModern - Project Status

**Milestone 12 (menus and windows in C++, then the new interface) is
complete,** accepted on 2026-11-15 ([plan](milestone-12-plan.md)): the
game's windows, the cursor and every choice, the field menu, the hospital,
the church, the shops and the palette fades now run in C++, and the Options,
Controls and start screens were redone in the game's own window style.
Milestone 11 ([plan](milestone-11-plan.md)) did the same for walking on the
world map and in towns, milestone 10 ([plan](milestone-10-plan.md)) added the
16:9 view and milestone 9 ([plan](milestone-9-plan.md)) put text in C++. The
[project history](project-history.md) lists every milestone and release; 0.6.0
(2026-11-15, milestone 11) is the latest published version and does not yet
include milestone 12.

Phantasy Star runs from the player's own ROM on a virtual Master System, in
the US and the Brazilian version. Controller polling, the random-number
routine, the random-encounter checks, experience, battle setup, money award,
dungeon walking, text, walking on the map and in towns, and now menus,
windows and fades run in C++; the rest of the original program runs on the
virtual Z80. Optional features are saved encounter and experience/battle-meseta
rates, a dungeon automap, session-only 2x/4x fast-forward, changeable
controls, language selection and a widescreen view. Without saved settings,
rates are original, the automap and widescreen are off and controls follow
0.2.0; acceleration starts at 1x.

Replacements are checked against the original, call by call and frame by
frame, with exact state and timing. A private study base explains how the
original code works. The [roadmap](roadmap.md) orders the next milestones; the
maintainer starts each explicitly.

## What can be played

The whole game, as on the cartridge, plus saved rate settings, a dungeon
automap, fast-forward, changeable controls, live language selection and an
optional widescreen view. Milestone 12 changed how PSModern's own screens
look (Options, Controls, start) and no feature of the game: with every option
off the game plays and looks as it did.

- **Starting:** double-click `psmodern.exe` to reach the start screen: the
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
- **Widescreen (off by default):** the WIDESCREEN row of Options shows the
  world map, the towns and the spaceship flight at 344x192 (16:9): the original
  256 pixels in the centre and 44 more on each side, drawn with the
  cartridge's own tiles and colours (in space, from the starfield). Broken ice
  stays broken in the sides after a screen change (fixed in milestone 11).
  Battles, dialogue, shops, houses, dungeons, scenes and the Options,
  Controls, Language and automap screens stay at 256 pixels, centred between
  black bars; the start screen stays 256. While the option is on, the window
  keeps the wide size on every screen. The choice is saved per profile; it is
  read-only in recording mode and with `--no-routines`. The machine is the
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
  Options it is the last row: Left and Right step through AUTO, ENGLISH and
  PORTUGUES (each in its own words; a language without its ROM is skipped), and
  each step is saved and switches PSModern's screens at once; the game's own
  text changes when Options closes, and a failed save restores the previous
  choice. AUTO follows the ROM played. A saved choice is global, the scene and
  progress are kept across a switch, and if the preferred ROM is absent the
  available language is used with a warning.
- **Saves:** the game's own save menu writes one shared cartridge file,
  `%APPDATA%\PSModern\shared\phantasy-star.sav`, with options and automap
  companions beside it, a `.bak` of the previous file before the first write
  of a session, and a lock against concurrent use. First use imports old US/BR
  files without modifying them. The parent folder holds `roms.txt`,
  `controls.txt` and `language.txt`, written only on their own SAVE.
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
  behind it and music continues. At the left are the six rows (ENCOUNTERS, EXP
  / MESETA, AUTOMAP, WIDESCREEN, CONTROLS, LANGUAGE) with their values in grey;
  beside the selected row a window lists every choice, the current one in
  yellow; at the bottom a window explains the row, its last line showing the
  state (SAVES ON EXIT, read-only while recording or without the routines,
  saved and waiting, or save failed). Left/Right changes a value directly;
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
  and 2 read as widescreen off. Battle meseta follows the experience rate;
  other money is unchanged. Recording ignores the shortcuts and keeps no map;
  raw-Z80 mode exposes read-only options
  ([development](development.md#options), [automap](development.md#automap)).

## Architecture overview

The design is in [design](design.md) section 5. Milestone 1 built its lowest
layer.

| Part | Folder | What it does |
| --- | --- | --- |
| Console | `src/hw/`, `src/machine/` | Z80, memory and mapper, cartridge RAM, ports, VDP, PSG, pads, Pause and Reset, driven by one cycle counter |
| ROM | `src/rom/` | Identifies the two supported cartridges by SHA-1 |
| Game | `src/game/` | Registered C++ routines (controller, random numbers, encounters, experience, battle setup and money, dungeon walking, text, the six groups of the map and the six of the menus), the options, the dungeon tables, the automap's exploration model and `MapScreens`, the world-map and town reader |
| Program | `src/app/` | SDL window, sound, input and its bindings, save files, the start, Options, Controls, Language and map screens (the first three in the game's window style), `WideView` (the widescreen renderer), frame pacing, command-line options |
| Recordings | `src/replay/` | Input recordings, the headless replay runner, per-call and frame verification, routine censuses |
| Reference | `tools/` | The driver that replays recordings in a patched MesenCE, and the comparison tool |
| Tests | `tests/` | Unit, conformance, ROM, probe and checkpoint tests |

Some properties matter for the next milestones:

- **Exact and deterministic recordings.** The same recording always gives the
  same frames, byte for byte; Options shortcuts are ignored while recording.
- **Copyable.** The whole machine copies as one value, so a run can be
  forked and compared. Immutable ROM images are shared.
- **Verified replacements.** Registered C++ routines replace the original
  only where interrupts and frame boundaries permit exact timing. Other
  calls stay on the Z80. A developer switch disables replacements.
- **Segments.** A routine that calls original code, for instance to wait for
  a frame or show text, is cut into pieces. Each piece runs in C++ and ends
  by handing one original instruction to the Z80 core, so that cycles and
  interrupts come out as on the console. A piece may also end with words held
  on the stack across an original call, with a conditional jump, or partway
  through a long loop (milestone 7). Milestones 11 and 12 used the mechanism
  unchanged, with larger kits (`src/game/map_kit.h`: video-chip port output,
  the index registers and more instructions; `src/game/menu_kit.h`: video-memory
  reads and a few more) proved against the core, and one rule: a loop that
  crosses a frame boundary on most calls is cut into pieces (the menus cut
  window copies per row), and a copy too long to fit between frames is left to
  the Z80. The map's 153 entries form six groups, one file each in `src/game/`
  (`map_video`, `map_scroll`, `map_objects`, `map_terrain`, `map_entrances`,
  `map_modes`), and the menus' 323 six more (`menu_engine`, `menu_windows`,
  `menu_select`, `menu_field`, `menu_places`, `menu_fades`); `src/game/map.h`
  and `src/game/menu.h` list them and the code that stays native.
- **Host-observed instructions.** The program watches 13 original
  instructions (the Options and map hold, frame waits, the normal-tempo audio
  points and the automap's points) without changing anything, among them the
  map loop's frame wait at `00:0B14`, where Options and Language open, and the
  field menu's cancel point `00:2D93` and save point `00:1D2D`. No piece
  contains, starts or ends at one: activating the routines fails if one does,
  and tests compare the watched events and a hold at the field menu with
  routines on and off.
- **Widescreen is a read-only presentation layer.** After each frame, while
  the option is on, the driver copies video memory, palette, registers and a
  few work-RAM bytes; `WideView` and `MapScreens` read the maps from the
  player's ROM and draw the bands. A frame that is not consistent is shown as
  the 256 centre between black bars. The virtual Master System and the Z80
  never see the option; tests prove the machine equal with it on or off every
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

| Game system | State |
| --- | --- |
| Controller polling (`00:0339`), random numbers (`00:05B1`) | C++ by default; original Z80 fallback when needed |
| Map encounter check (`01:5F63`, with the group choice `01:5FD8`) | C++ by default, in pieces |
| Dungeon update with its encounter check (`00:0F3C`) | C++ by default, in pieces |
| Experience award (`00:170D`, helper `00:1754`) | C++ by default, in pieces |
| Battle setup (`01:5FFE`) and money addition (`00:297A`) | C++ by default; battle meseta scales with the experience option |
| Dungeon walking (input handler `01:65EE`, steps, turns, pitfall, doors, exit lookup, checks for chests, fights and events, cell readers) | C++ by default, eleven routines in 53 pieces; three short pitfall sequences that switch interrupts off stay on the Z80 |
| Map picture update in the frame interrupt (`00:0235`, `00:029F`, `01:588B`, `01:61F5`, `01:73D0`, `01:7C18`), steps, camera and screens in memory (`01:7143`, `01:723D`, `01:72A6`, `01:733A`, `01:744B`); sprites and object list (`01:576A`, `01:5818`, `01:5853`, object types from `01:599F`); ground, vehicles, ice and ground damage (`01:7764`, `01:74E4`, `01:756C`, `01:7602`) | C++ by default, in pieces, with the video-chip writes at their original cycles; long loops cut at their exits |
| Entrances, transfers, special ground (`01:77AC`, `01:7877`, `01:792A`); map modes, loading and the road (`00:0B08`, `00:0B6A`, `00:0CA6`, `00:0E8B`, `00:0ED7`) | C++ by default, in pieces |
| Text readers, dictionary, control codes, names/numbers, character output and waits | C++ by default in segments, with both-ROM timing verification |
| The 3D dungeon view and its animations, the dungeon load | Original Z80 code |
| Windows and printers (`00:3AA6`, `00:3A57`, `00:3A83`, `00:2FE1`, `00:356B`, `00:35C4`), field, item, status, shop, save and battle windows, choosing with the cursor and the waits (`00:2D51`, `00:2DA5`, `00:2E62`) | C++ by default, 179 pieces; long window copies cut per row |
| Field menu with status, magic, items, equip, drop, search, save and chests (`00:1BE1` and its commands); hospital, church and shops (`00:298D`, `00:2A85`, `00:2BC0`, `00:2C98`) | C++ by default, 121 pieces |
| Palette fades and palette effects (`01:7B05`, `01:7B20`, `01:7B40`, `01:7BEE`, `01:7C85`-`01:7D16`) | C++ by default, 23 pieces |
| What items and spells do, battle commands, name entry and title menus, the other tile animators, sound driver, native saves and the rest | Original Z80 code; host options and the map use the original field-menu wait |

**What stays on the Z80 in the map area** (the tile animators that are not the
map's are left for milestone 13, Maintainer decision, 2026-11-15; the palette
fades moved to C++ in milestone 12):
- the four short stretches that run with interrupts off: three in the map
  load (`00:0B6D`-`00:0B71`, `00:0BB4`-`00:0BB8` four times, `00:0C91`-`00:0C93`)
  and one in the ice breaking (`01:75CC`-`01:75E4`);
- the map loop's call at `00:0B14`, where Options and Language open;
- block copies too long to fit between frames: the 255-byte clearing of the
  object slots (`00:0CAC`-`00:0CB1`), the second clearing of a transfer
  (`01:7921`) and the sprite-table clearing's second fill;
- the 64-byte sprite copy `01:591E` when entered directly (it runs in C++
  inside the pieces that jump to it);
- the tile animators that are not the map's, the battle object types, the
  handler dispatcher and step controller, the jump-table dispatchers, the
  graphics decompressor, interrupt entry and exit, the sound driver and the
  spaceship scene (`00:0A5C`, `00:086F`).

**What stays on the Z80 in the menu area** (Maintainer decision, 2026-11-15;
all of it left for milestone 13):
- the instructions that switch interrupts off and on around a window's
  drawing, and the instruction next to each;
- the save: the cartridge-RAM switching and the 1,024-byte copy
  (`00:1D0E`-`00:1D32`), with the save point `00:1D2D` the host watches, and
  the field menu's cancel point `00:2D93`;
- the two loads `00:3451` and `00:3454` before the last window of a dialogue
  scroll and the end of each row of a window drawn at once
  (`00:3A97`-`00:3A9E`, six instructions, 52 cycles), because the frame
  boundary falls on them almost every time;
- the jump-table dispatchers and the frame waits;
- what items and spells do, the battle commands, the name entry and the title
  menus.

On the twenty checkpoint recordings, controller polling ran in C++ on all
of its calls and the other routines, the map's included, on about 99% of
theirs; the few fallbacks are calls where a frame boundary could fall inside
the piece. The menu pieces were called 694,553 times and 99.8% ran in C++. Every
entry reached 20 or more times runs in C++ on at least 90% of them (the
lowest in the menus, 93.9%). The generated [coverage census](coverage.md) gives the counts per
piece and recording. It describes these recordings, not every possible route
through the game.

## How it is verified

- **Frame-by-frame comparison** with a patched MesenCE 2.2.1, on twenty
  recordings: six per version (the opening, the first battle, the first
  dungeon, a later town, a boss fight and a deep dungeon), three US dungeon
  walks, three US map recordings made in milestone 11 (`road`, `vehicles`
  and `ice`: the road from a town to a spaceport and the ship to Palma, the
  Land Rover and the hovercraft, and the digging through an ice field on
  Dezoris) and two US menu recordings made in milestone 12: `field-menu` (the
  field menu with its commands, chests and saving) and `places` (the hospital,
  the church and the shops of Paseo). Every frame matches in the picture,
  RAM, video memory, palette, cartridge RAM, CPU state and sound writes, with
  the rewritten routines on and off. `vehicles`, `ice`, `field-menu` and
  `places` start from a game-made save whose fields were edited, since the
  game reaches them late; `road` starts from an unedited save. The edits are
  listed in [development](development.md) (Maintainer decision, 2026-11-15).
- **Each C++ call** is checked against a copy running the original,
  including hidden machine state, cycles, R and opcode fetches; the map's
  video-chip writes keep their original cycles. All previous/current button
  combinations are checked for both ROMs.
- **State probes** run each piece against the original from many machine
  states, in both versions: the random-number routine, all 192 map region
  records, every threshold, reward and level-up case, every dungeon cell value
  in every facing, for the map vehicles, ice, the road, special ground, both
  world wraps, entrances, transfers and loading, and for the menus all 323
  pieces (the window engine from many video and window states, every
  command of the field menu), from power-on machines and from real states.
  Where the recordings reach few paths, the probes carry the exactness.
- **The options** are checked against their formulas over every input, with
  interrupt and frame boundaries forced at each cycle, and by running the
  recordings with the options on. At 100 the game is byte-identical to the
  original. The default controls are release 0.2.0's, checked over every
  combination of keys and buttons.
- **The automap and widescreen change nothing in the machine:** on eight
  recordings (automap) and on all 21 (13 US, 8 Brazilian; widescreen) the
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
- **Independent review:** an implementation review of milestone 12, authorized
  by the maintainer, approved it with required changes and no behavioural
  defect: the field menu's code was rewritten from generated listing form into
  one function per command with a comment on each step, the documentation of
  what stays native was corrected, a comment was fixed, and five of its seven
  recommended items were applied (the original-quirks labels, the test
  assertions, the private annotation, the save-process test's scheduling, the
  line endings and wording). The sixth, a log of the order of video writes
  inside a piece, was not made and stays a documented limit, as in milestone
  11 (see Known gaps).
- **The complete suite** has 266 tests; at the closure of milestone
  12 all passed with none skipped, on both ROMs, in about 17
  minutes of real time at `-j 16`. It runs once per milestone
  ([tools/AGENTS.md](../tools/AGENTS.md)).
- **Conformance programs:** the Cringle, Rak and Woodmass Z80 suites, and
  the VDPTest results for the SMS1 video chip.
- **CI** on a clean Windows machine runs every test that needs no ROM, and of
  the external conformance suites only the three Rak Z80 tests. The ROM,
  probe and reference tests run locally ([development](development.md)).
- **Play-tests:** the maintainer played milestone 12 with the US cartridge, in
  one session with a kit of test saves (the redone screens, the field menu, a
  hospital, a church, the shops, chests, fades, saving and Continue, a battle),
  and found everything approved and no difference from the original. He played
  the build from before the review's code fixes and decided the play-test need
  not be repeated; the tests cover the difference (Maintainer decision,
  2026-11-15).

## Known gaps

- **Options remain deliberately small.** Other features are later work on the
  [roadmap](roadmap.md).
- **Controls have one key and one controller input per action,** only the
  first controller is used, and there is no second pad or shortcut back to the
  start screen during play. A damaged `controls.txt` is reported only on the
  console; a trigger held when a controller connects is ignored best-effort.
- **Recording coverage is thin where some routines act.** The recordings
  reach few encounter paths and one level gain. Thirteen dungeon pieces, some
  map pieces and 27 menu pieces (among them the second kind of shop and some
  story-bound palette effects) are reached by no recording and are covered by
  state probes only; the road, the vehicles and the ice have one US recording
  each ([coverage](coverage.md)). `field-menu` ends on the save-name screen,
  which is milestone 13 code.
- **There are no Brazilian map or menu recordings.** The map and menu code is
  the same in both versions (design 5.5) and the probes of every piece run on
  both ROMs, but only the six shared checkpoints and the text recording are
  Brazilian (Maintainer decision, 2026-11-15). This is not a full Brazilian
  playthrough; development uses the US cartridge first.
- **Port-write order inside a piece is not logged.** The per-call comparison
  checks the state afterwards and the total cycles, not each video-port
  access; the order and cycle of the writes inside a video segment rest on
  reading the code and on the kit's timing tests ([development](development.md)).
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
- **The registered-routine mechanism has limits.** It refuses routines that
  change interrupt sources, adjust the stack beyond declared held words (so
  the pitfall's three interrupt-off sequences stay native), write mapping
  registers other than the declared upper-bank one, or end a piece on a
  repeated port-write instruction (so `01:591E` stays native on direct entry)
  ([development](development.md#segments)).
- **Study coverage is route-bound.** Routes the recordings never reach, such
  as the ending, rare battle and sound paths, and many work-RAM fields are
  still Unknown.
- **Windows only** for the save location.
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
- **Rare sound stalls are not ruled out.** One of three measured sessions
  had stalls of about 90 ms; their cause is not known.
