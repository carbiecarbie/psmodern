# PSModern - Design

**Status: approved by the maintainer on 2026-11-04**, after an independent
read and the maintainer's decisions on timing, the Brazilian ROM and the
public repository. The proposals of the draft were approved with it. Amended
on 2026-11-04 by the maintainer: the annotated disassembly moved to a
separate private repository (section 6); rewritten routines also reproduce
the Z80 R register (section 5.2). Amended on 2026-11-09 by the maintainer:
the US ROM is the main target and the Brazilian ROM is checked by its
recordings (section 5.5); section 3, stage 3 and section 9 were updated
with the milestone 3 study of both disassemblies. Amended on 2026-11-10
(factual updates, authorized by the maintainer): sections 5.1, 5.2 and 9
mark the items milestones 1 and 2 settled and name the reference emulator
in use; no decision changed. Amended on 2026-11-11 by the maintainer
(milestone 7 planning): the automap is a map screen opened by a button that
reads state the original keeps, so it no longer waits for the movement
rewrite (sections 2, 5.1, 5.3, 8 and 9), and its 8x8 icons are a named
exception to "No new art" (section 2). Amended on 2026-11-13 by the
maintainer (milestone 8 closure): encounters also accept 25 and the
recommended pace is encounters 25 with experience 400 (section 9); full
controller support is delivered, and its default is the original mapping
(section 2 and stage 7). Amended on 2026-11-14 by the maintainer
(milestone 10 planning): widescreen reads state the original keeps, so it
no longer waits for the map rewrite (sections 5.3 and 8), and section 5.4
records the study's findings, the 344-pixel width, the town edges and the
filled left column. Amended again on 2026-11-14 by the maintainer after his
milestone 10 play-test: the spaceship flight between planets widens too
(sections 2 and 5.4). Changes to this design need the maintainer's approval
and are recorded here.

## 1. What it is and why

PSModern is a faithful reimplementation of **Phantasy Star** (Sega Master
System, 1987/88) in C++, running natively on modern computers from the
player's own ROM.

Why it exists:

- The game is personally special to the maintainer.
- The main goal is **the process itself**: studying the disassembly and, at
  the end, **knowing how the game works inside**. The maintainer does not aim
  to become a reverse engineering specialist; they want to understand the
  game.
- On top of the faithful game, add the comfort features of the SEGA AGES
  Switch release (less grinding) and others the maintainer wants.

The maintainer is a programming beginner who works by coordinating AI
sessions, as in MMModern (`D:\Projetos\MModern\mmodern`), which is the model
for this project's organization and workflow.

### What success looks like

1. The whole game runs in PSModern in **original mode**, identical to the
   Master System in picture, sound, rules and timing.
2. All game code has been rewritten as readable C++; no Z80 code executes at
   the end, and the ROM is only a data source (graphics, music, text, maps).
3. Documentation explains every system of the game (encounters, battle,
   dungeons, text, sound, saving and so on).
4. The new features work as options, off by default.

## 2. Goals, non-goals and anti-goals

### Goals (maintainer decisions)

- **Original mode:** a 256x192 picture identical pixel for pixel, identical
  sound, the original rules (encounter rate, experience, randomness) **and the
  same timing**: the original's slowdowns (3D dungeon drawing, battles, screen
  loads) and the frame on which each button is read are kept. See 5.2.
- **Original mode in English (US Rev-A ROM) and in Portuguese (Tec Toy's
  Brazilian ROM)**, each identical to its own cartridge. See 5.5.
- **Optional features**, all off by default (for the controls, the default
  is the original mapping):
  - **Adjustable experience** (a multiplier), in the spirit of the Switch
    release.
  - **Adjustable random encounters**, including turning them **off**.
  - **Automap** in the dungeons: a map screen, opened by a button, of the
    explored part of the current floor (see 5.3).
  - **Real widescreen** on the **world map, in towns and in the spaceship
    flight between planets**, showing more of the area with the existing
    graphics.
  - **New interface** (menus and windows).
  - **Language selection in the options**: English and Portuguese to begin
    with.
  - **Full controller support**: every key and controller button can be
    changed, and nothing changes until the player saves a choice.

### Non-goals

- **No new art.** Nothing that depends on drawing graphics. One named
  exception (maintainer decision, 2026-11-11): the automap screen uses a few
  simple 8x8 icons drawn for PSModern (party, chest, opened chest, stairs up,
  stairs down, exit, door, pitfall; the plain, locked and magic doors are
  the one door drawing in three colours). It covers those icons on that screen
  only; it is not a licence for other new art, and any other exception needs
  its own maintainer decision recorded here.
- Widescreen in battles, 3D dungeons and portrait scenes: they keep their
  original size, centered. (The spaceship flight is not among them since
  2026-11-14: see 5.4.)
- A static recompiler (automatic Z80-to-C). Considered and rejected; see 4.
- Producing a binary identical to the ROM from the C++ (not applicable: the
  game was written in assembly).
- Distributing ROMs. The player supplies their own copy, as in MMModern.

### Anti-goals (a failure even if it "works")

- An original mode that behaves differently from the Master System.
- Any new feature on by default, or one that changes original mode.
- Rewriting routines without verifying them against the original.
- A game that runs while its workings stay undocumented: understanding the
  game is the central goal.

## 3. Available material and verified facts

Folder `D:\Projetos\Phantasy Star\` (outside the repository):

| File | Contents | SHA-1 |
| --- | --- | --- |
| `Phantasy Star (USA) (Rev-A).sms` | Reference ROM, 512 KiB | `07fcf297be4f4c9d92cd3f119a7ac48467e06838` (CRC32 `00BEF1D7`) |
| `Phantasy Star (Brazil).zip` -> `.sms` | Tec Toy translation, 512 KiB | `fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd` (CRC32 `75971BEF`) |
| `PS1_Disasm.zip` | pscave disassembly (WLA-Z80, `build.bat`, `chkbitperfect.bat`) | - |
| `SEGA AGES - Phantasy Star Switch NSP Base Game.nsp` | The maintainer's Switch dump, 99 MB | - |

Facts verified on 2026-11-03/04:

- The pscave disassembly's `ps1original.bin` is **byte for byte identical**
  to the US Rev-A ROM (same SHA-1). US Rev-A is the project's reference ROM.
- The game uses the Sega mapper (writes to `$FFFC`-`$FFFF`), including the
  cartridge RAM control at `$FFFC` (battery-backed save).
- The Switch NSP has not been examined. It is encrypted; opening it needs
  the keys of the maintainer's own console.

Facts verified on 2026-11-09 (milestone 3; ROM static, ROM run):

- Both disassemblies rebuild the US Rev-A ROM byte for byte. The pscave one,
  generated by SMS Examine, has about 10,400 lines in its main file (about
  70,000 in all) and only 14 meaningful labels besides its bank markers. It
  leaves about 7,000 bytes of code that the game runs in banks 0-1 written
  as data.
- [sung3r/ps1disasm](https://github.com/sung3r/ps1disasm) shares that
  lineage and is far further along: about 730 meaningful labels, constants,
  and the sound driver in bank `0C` decoded as code. It builds with WLA-Z80
  10.5 / WLALINK 5.20 (10.7 rejects it unchanged). Its repository bundles
  the ROM itself, and its README allows education and information use only,
  not commercial use.
- On the six US checkpoint recordings, code runs only in banks 0, 1 and
  `0C`. Bank `0C` is the sound driver. The other banks executed nothing on
  those routes, which does not prove they hold no code.

## 4. Approaches considered

| Approach | Summary | Decision |
| --- | --- | --- |
| Static recompilation | A tool translates Z80 to C mechanically; the result runs but stays "a Master System inside". Good for encounters, experience and the minimap; poor for widescreen, a new interface and language selection. | Rejected: limits four of the five wanted features and reduces the study. |
| Rewrite by system, from scratch | Study a whole system and write it cleanly in C++, checked against emulator recordings. | Rejected: nothing playable for months, errors hard to locate, integration bugs. |
| New engine from the data formats | An engine of our own built from the maps and texts only. | Rejected: cannot guarantee an identical original mode. |
| **Gradual replacement with a scaffold Z80** | The program runs the original ROM on a virtual Z80 from the start and replaces routines with C++ one by one, checking each against the original. At the end the Z80 is removed. | **Chosen by the maintainer.** The same principle as OpenRCT2. |

Why: the game is never broken, every routine is verified at once, the
maintainer picks the order of study (comfort features arrive early), and it
follows what worked in MMModern: always compare with the original, in small
verifiable milestones.

## 5. Architecture

### 5.1 Layers

1. **Virtual hardware (permanent):**
   - **VDP** (Master System mode 4): VRAM, CRAM, registers, sprites,
     scrolling, frame and line interrupts.
   - **PSG** (SN76489).
   - **Input** (the controller port).
   - **Sega mapper and cartridge RAM** (saves).
   - **Optional:** FM (YM2413), if the game uses it. Not built so far; see 9.

   These parts stay in the project for good. The rewritten game keeps
   "talking" to them as the original talks to the hardware, which is what
   guarantees a pixel-exact original mode without extracting or converting
   graphics.
2. **Virtual Z80 (scaffold, temporary):** runs the ROM. Removed once all game
   code has been rewritten.
3. **Routines rewritten in C++:** registered by entry address (bank +
   address). When the virtual Z80 reaches a registered routine, the C++
   version runs instead. It reads and writes the same state (registers, the
   8 KiB RAM, the virtual hardware) and returns as the original would.
4. **New-feature layer:** options that change behavior only outside original
   mode, for example the experience multiplier inside the rewritten
   experience routine, or the automap drawn from the dungeon state the
   original keeps.
5. **Presentation:**
   - **Original renderer:** draws the virtual VDP at 256x192.
   - **Enhanced renderer:** the same source, with a wider area on the world
     map and in towns, and overlays (the automap screen, new interface).
   - **Window, scaling, sound and controller** through SDL.

### 5.2 Verification (the heart of the project)

- **Determinism:** given the same ROM, the same initial state and the same
  input sequence, the game always produces the same result.
- **Input recordings:** frame-by-frame button sequences that take the game
  to known situations (walking the world map, entering a town, a battle, a
  dungeon, saving).
- **Lockstep run:** the same recording runs on two paths, Z80 only and with
  the rewritten routines enabled. The tool compares RAM, VRAM, CRAM, VDP
  registers and sound output, and reports the first frame and the first
  routine that differ. All new features are off on this path.
- **Virtual hardware validation:** before the Z80-only path is trusted as the
  reference, it is checked against an established emulator on the same
  recordings: the patched MesenCE 2.2.1, with Emulicious as second opinion
  (see [dependencies](dependencies.md#reference-emulators)).
- **Rule for the new features:** with everything off, a run must be identical
  to the original. This is tested too.
- **Timing counts as behavior** (maintainer decision). A C++ routine is
  instantaneous, but the original spends Z80 cycles. Left alone, the slowdown
  frames disappear, buttons are read on a different frame, and anything that
  advances per frame or in a wait loop (the random number generator, the
  encounter roll) drifts. Therefore:
  - every rewritten routine reports how many cycles the original would have
    spent, with the same result for the same input and state, and reproduces
    every other CPU-visible effect of the original, **including the Z80 R
    register**, which advances on each opcode fetch: the game's random
    number generator reads it (`00:05C8`), so a routine that matches the
    cycles but not R changes every later random draw (maintainer decision,
    2026-11-04);
  - the lockstep comparison also checks the cycle and frame at which each
    routine ends, not only the state;
  - the same input recording is valid on both paths, with no separate
    recordings;
  - once the Z80 is removed (stage 11), the final engine needs a "time budget
    per frame" model that reproduces the same slowdowns.
- A routine may compute its cycles with a formula derived from the original
  code (instruction counts per path), checked against the Z80 on the
  recordings. Milestone 2 settled how: see
  [development](development.md#registered-routines).

### 5.3 Dependencies between features and rewritten systems

| Feature | Needs these rewritten first |
| --- | --- |
| Adjustable / disabled encounters | The encounter roll |
| Adjustable experience | Experience gain at the end of a battle |
| Automap | Nothing: it reads state the original keeps (see below) |
| Language selection | The text system (encoding, pointers, font, windows) |
| Widescreen on the world map, in towns and in the spaceship flight | Nothing: it reads state the original keeps (see below) |
| New interface | Menus and windows |
| Full controller support | Only the input layer; delivered in milestone 8 |

The automap reads the floor, position, facing and layout the original keeps
in work RAM, and the object and exit tables of the player's ROM, at native
points the original passes, and tracking changes nothing the game reads
(maintainer decision, 2026-11-11, on the milestone 7 study). It is a screen
opened by a button with the game held as for the Options window, never a
permanent overlay.

Widescreen reads the camera, the area and the decoded map screens the
original keeps in work RAM, the map tables of the player's ROM, and the
video memory, palette and registers as the console uses them for each
displayed frame; it draws the extra columns with the console's own tile
rules. It watches no instruction and changes nothing the game reads
(maintainer decision, 2026-11-14, on the milestone 10 study). The rewrite of
map loading and scrolling is its own later milestone.

### 5.4 Known widescreen issues

From the milestone 10 study and the maintainer's decisions of
2026-11-14:

- **No frozen NPCs expected:** the map load fills the object slots with the
  party or one vehicle only (ROM static). The study found townspeople drawn
  as part of the map and entrances checked as records when a step ends, so
  nothing should depend on distance from the screen; the play-test
  confirms it.
- **The edge of the map:** world maps wrap both ways and have no edge. A
  town lies on a sheet of screens whose other screens are filler, which the
  original already shows near a town's edge; widescreen shows what the
  cartridge has there, with no clamp, no black and no repeated tile.
- **The width:** 344 x 192 with square pixels, about 16:9; the original 256
  pixels stay in the centre, 44 more on each side. Every other screen keeps
  its 256 pixels, centred between black bars.
- **The spaceship flight** (maintainer decision, 2026-11-14, after his
  play-test; it was first left out): the scene between planets (load
  `00:0A5C`, flight `00:086F`) widens, entered only from an interaction exit.
  Take-off and descent show the map around the ship; in space the sides are
  the starfield drawn wrapped, as the video chip would draw it past 256
  pixels, so stars at the right repeat those at the left of the centre. Read
  only, like the rest.
- **The hidden left column:** on the map the original blanks the leftmost 8
  pixels, so it shows 248 pixels of map. Widescreen fills that column from
  the map like the sides; original mode is unchanged.

### 5.5 Languages

The Brazilian ROM is **an original-mode target** (maintainer decision):
original mode in Portuguese behaves exactly like Tec Toy's cartridge.

**How the two ROMs differ** (ROM static, compared 2026-11-09). Their code is
the same except one operand at `01:4181`, which picks a blank tile on the
save-name screen. Rules, enemies, items, shops, encounter tables, maps, music
and the save layout are the same. The rest of the differences are text, item
names, menu, title and opening graphics, and the cartridge header. The text still
changes how the game plays: its control codes decide where some
conversations stop, wait for a button or run into the next message, and its
length changes how long the text takes to print. The game's random draws
depend on that timing, so the same input gives different random draws in the
two versions (ROM run).

**US first, Brazilian checked automatically** (maintainer decision,
2026-11-09):

- The US ROM is the main development target. Rewritten routines, new
  recordings and maintainer play-tests use it.
- The Brazilian recordings stay in the checkpoint and lockstep tests, and a
  rewritten routine must pass them too. A new Brazilian recording is added
  only when a Brazilian difference needs one: the operand above, or a text
  path only the Brazilian script reaches.
- Because the code is shared, one rewritten routine serves both versions. It
  must not assume the US text: Portuguese comes from the Brazilian ROM's own
  text, item names, menus and control codes, never from replacing only the
  words.
- The game must start with the Brazilian ROM alone, the US ROM alone, or
  both. With both, the player picks the language in the options.
- Text is **not extracted into files in the repository**: it is read from
  the player's ROM at startup (see 7). In enhanced mode, the new interface
  and windows must accept texts of different lengths in the two languages.
- Language selection is global and takes effect during the current game
  after Options closes at a proven free-exploration boundary (Maintainer
  decision, 2026-11-13; [milestone 9](milestone-9-plan.md)). Dialogue, battle
  and other native menus must finish first. A change keeps the session's
  shared saves, options and automap independently of the available ROMs. The
  launcher has one PLAY entry. On first use, import an existing legacy US/BR
  set without modifying its originals; different old sets require a one-time
  choice, never a silent merge (Maintainer decision, 2026-11-13). The save
  layout is the same in both versions, and US saves load in the Brazilian game
  (Reference emulator).
- PSModern's own player-facing screens follow the selected language too
  (Maintainer decision, 2026-11-13). Portuguese labels rendered with the
  cartridge font use uppercase without accents; no new font art is added.
  Original game text remains exactly the selected cartridge's text.

## 6. Knowledge as a deliverable

Besides the program, the project produces:

- **Annotated disassembly:** names and comments for routines and RAM
  variables, kept in a **separate private repository**, `psmodern-re`
  (maintainer decision, 2026-11-04), checked out at
  `D:\Projetos\Phantasy Star\psmodern-re`. A disassembly is derived directly
  from SEGA's code, so it never goes into the public repository, as in
  MMModern. The private repository also leaves the ROM's data sections
  (graphics, music, text, maps) out of Git and includes them from the
  player's ROM at assembly time, so checking that the disassembly assembles to
  the identical ROM stays possible whenever the ROM is present. The public
  repository cites original code only as `bank:address`.
- **`docs/how-it-works/`:** one document per game system, written for a
  reader who does not program, explaining how it works, with its evidence.
- **`docs/original-quirks.md`:** odd behavior of the original that original
  mode keeps, as in MMModern.

Every rewritten routine leaves its name in the disassembly and its
explanation in the system's document behind it.

## 7. Constraints and way of working

- **The MMModern model** (maintainer decision): numbered milestones, each
  with a plan in `docs/milestone-N-plan.md`. A Claude session plans, reviews
  and commits; Codex or Claude sessions implement; the maintainer confirms
  commit messages and play-tests. The agent rules are in `AGENTS.md`.
- **Folder:** the repository is `D:\Projetos\Phantasy Star\psmodern`. ROMs
  and research material stay outside it, in `D:\Projetos\Phantasy Star\`.
- **Public repository** (maintainer decision):
  [carbiecarbie/psmodern](https://github.com/carbiecarbie/psmodern). No ROM
  data enters Git: not the ROM, not raw data banks, not extracted text,
  graphics or maps, and no disassembly or assembly listing. ROM data is read
  from the player's ROM at run time; the disassembly lives in the private
  `psmodern-re` repository (see 6).
- **Tests and CI:** CI (without a ROM) builds and runs the tests that need no
  ROM, such as virtual hardware tests with the project's own test programs
  and pure logic. Tests that need the ROM (recordings, lockstep comparison)
  run on the maintainer's machine, as in MMModern. The identical-assembly
  check of the disassembly belongs to `psmodern-re`.
- **Input recordings** may be committed: they are button sequences, not game
  data.
- **ROM identification:** the program takes the ROM path and checks the SHA-1
  of US Rev-A and of the Brazilian ROM. Unknown versions are refused with a
  clear message.
- **Platform:** Windows first, other systems later (as in MMModern).
- **Stack:** C++, CMake, SDL (as in MMModern).
- **License:** GPL-3.0-or-later, as in MMModern. Third-party libraries (for
  example the Z80 core) must have a compatible license.
- **Language:** English for all documentation, code, comments and commit
  messages (maintainer decision, 2026-11-04). Conversations with the
  maintainer are in Portuguese.

## 8. Suggested order of stages

This is the sequence of large stages. A detailed roadmap is written once the
study base (stage 3) shows how the code is organized.

1. **Foundation:** repository, build, CI. Load the ROM with a hash check.
   Z80 + mapper + cartridge RAM + VDP + PSG + input. The whole game runs in
   original mode (in practice, an emulator dedicated to Phantasy Star).
2. **Verification:** input recordings, lockstep run, state **and timing**
   comparison (cycle/frame), validation against an established emulator. The
   registered-routine mechanism, with cycle accounting, tested with a trivial
   routine. The game also starts from the Brazilian ROM.
3. **Study base:** set up `psmodern-re` from the sung3r disassembly with
   the ROM data stripped out (see 6), a private catalog of routines and RAM
   fields, the first RAM map and the first game-system chapters. Then the
   roadmap (`docs/roadmap.md`) takes over the order of the stages below.
4. **First win:** rewrite the encounter roll and experience gain. Ship the
   encounter option (adjustable/off) and the experience option.
5. **Dungeons:** state and movement, and the automap, which does not wait
   for the rewrite (see 5.3).
6. **Text:** the text system in both versions, reading the US/Brazilian text
   from the player's ROM, and language selection.
7. **Full controller support:** remapping, shortcuts. Done in milestone 8,
   ahead of stage 6.
8. **World map and towns:** widescreen, which needs no rewrite (see 5.3),
   then the rewrite of map loading, scrolling and the object engine.
9. **Menus and windows:** then the new interface.
10. **The rest:** battle, 3D dungeon drawing, the sound driver, scenes, the
    opening and the ending, until no Z80 routine is left.
11. **Removing the Z80:** the game runs on C++ alone, and the ROM becomes a
    data source.

## 9. Left to the builder

- Settled in milestone 3 (maintainer decisions, 2026-11-09): the study
  starts from the sung3r disassembly, and `psmodern-re` keeps a private
  catalog of routine and RAM names that this repository's `bank:address`
  citations are checked against.
- Which Z80 core to use (an existing one with a compatible license, or our
  own). Settled in milestone 1: redcode/Z80, vendored
  ([dependencies](dependencies.md#z80-core)).
- The format of input recordings and of the divergence report. Settled in
  milestones 1 and 2 ([development](development.md#input-recordings) and
  [development](development.md#registered-routines)).
- Comparison granularity beyond what 5.2 requires. Settled in milestone 2:
  the whole machine at each C++ call and seven components at each frame
  ([development](development.md#registered-routines)).
- Whether the US version uses the FM chip (YM2413). If it does, the FM chip
  joins the virtual hardware. Settled so far in milestone 1: the game used
  no FM on the observed routes, and the virtual hardware has none
  ([milestone 1 plan](milestone-1-plan.md#what-the-game-uses),
  [the console chapter](how-it-works/master-system.md#the-parts)).
  Whole-game absence is not established.
- Defaults and values of the experience and encounter settings. Settled in
  milestone 4 (Maintainer decision, 2026-11-11): encounters 0, 50, 100 or
  200 percent and experience 100, 200 or 400 percent, with 100 (the original
  game) as the default for both. Revised in milestone 8 (Maintainer decision,
  2026-11-13): encounters also accept 25, and the recommended pace is
  encounters 25 and experience 400, the Switch release's easy mode (a quarter
  of the encounters with four times the experience and battle money; a study
  kept outside the repository). The Switch release is a reference for feel,
  not exact values, unless the maintainer decides to examine the NSP with
  their own keys. A saved 50 stays valid; it just carries no marker.
- The exact design of the new interface (its own milestone). The automap's
  is set by the [milestone 7 plan](milestone-7-plan.md).
- How to store the new features' settings without changing the original save
  format. The maintainer wants the settings chosen inside the game and saved
  with it (a later milestone); until then the options are set only by
  command-line flags and are not saved.
