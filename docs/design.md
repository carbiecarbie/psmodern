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
in use; no decision changed. Changes
to this design need the maintainer's approval and are recorded here.

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
- **Optional features**, all off by default:
  - **Adjustable experience** (a multiplier), in the spirit of the Switch
    release.
  - **Adjustable random encounters**, including turning them **off**.
  - **Minimap / automap** in the dungeons.
  - **Real widescreen** on the **world map and in towns** only, showing more
    of the area with the existing graphics.
  - **New interface** (menus and windows).
  - **Language selection in the options**: English and Portuguese to begin
    with.
  - **Full controller support.**

### Non-goals

- **No new art.** Nothing that depends on drawing graphics.
- Widescreen in battles, 3D dungeons and portrait scenes: they keep their
  original size, centered.
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
   experience routine, or the minimap drawn from the dungeon state.
5. **Presentation:**
   - **Original renderer:** draws the virtual VDP at 256x192.
   - **Enhanced renderer:** the same source, with a wider area on the world
     map and in towns, and overlays (minimap, new interface).
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
| Minimap / automap | Dungeon state (map, position, facing) and movement |
| Language selection | The text system (encoding, pointers, font, windows) |
| Widescreen on the world map and in towns | Map loading, scrolling, NPC activation and the camera |
| New interface | Menus and windows |
| Full controller support | Only the input layer; can come early |

### 5.4 Known widescreen issues

- **The edge of the map:** small towns will show what lies beyond them. The
  camera stops before showing the void, or the edge tile is repeated; the
  final choice belongs to the widescreen milestone.
- **NPCs outside the original area:** if the game only activates NPCs near
  the 256-pixel screen, with more area visible they may appear frozen. The
  study must find out; if it happens, activation follows the new visible
  area, in enhanced mode only.

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
- Changing the language takes effect at the next game start, not mid-game.
  The save layout is the same in both versions, and US saves load in the
  Brazilian game (Reference emulator), so a save works in both languages.

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
5. **Dungeons:** state and movement, then minimap and automap.
6. **Text:** the text system in both versions, reading the US/Brazilian text
   from the player's ROM, and language selection.
7. **Full controller support:** remapping, shortcuts. May come earlier.
8. **World map and towns:** then widescreen.
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
  game) as the default for both; the recommended pace, like the Switch
  release, is encounters 50 and experience 400. The Switch release is a
  reference for feel, not exact values, unless the maintainer decides to
  examine the NSP with their own keys.
- The exact design of the new interface and the minimap (each gets its own
  milestone).
- How to store the new features' settings without changing the original save
  format. The maintainer wants the settings chosen inside the game and saved
  with it (a later milestone); until then the options are set only by
  command-line flags and are not saved.
