# Milestone 1 plan - Foundation: the original game runs

**Tier A. Status: planned (2026-11-04); amended after the independent plan
review (2026-11-04); both maintainer decisions approved (2026-11-04);
awaiting the maintainer's start.** First stage of [design](design.md) section 8.

## Goal

The player starts `psmodern.exe` with their own ROM, and Phantasy Star runs
in original mode with the picture, sound, controls and saves of the
cartridge, from power-on through the whole game. In practice it is an
emulator dedicated to this game: the whole ROM runs on the virtual Z80, and
nothing is rewritten yet. It is also the base every later milestone builds
on, so it must already be exact, deterministic and observable.

## Moves against the design's order of stages

The design's stages are a suggested order. This milestone takes three items
from later stages, because each is cheap here or needed by what milestone 1
builds:

- **The Brazilian boot** (stage 2): it needs nothing beyond ROM
  identification, and the play-test covers both versions at once.
- **Input recordings and the reference comparison** (stage 2): without them
  milestone 1 cannot show that the machine is exact. Milestone 2 keeps the
  same recording format and adds the lockstep run, cycle-level comparison and
  registered routines.
- **The first how-it-works chapter** (stage 3): `master-system.md`, about the
  console rather than a game system, because it explains what this milestone
  builds.

## Maintainer decisions (approved 2026-11-04)

1. **Start without the BIOS.** Every real export console (US Master System,
   Tec Toy) runs its BIOS before handing over to the cartridge. PSModern
   starts the cartridge directly, from a fixed, documented state, as
   emulators do without a BIOS. The game's random numbers read the Z80 R
   register (below), so the start state fixes the sequence: with a BIOS it
   would be another fixed sequence, not a "more correct" one, and on a real
   console the player's own timing changes it anyway. Using a BIOS would mean
   asking the player for a copyrighted BIOS dump too. **Approved: start
   without the BIOS.**
2. **Design amendment, section 5.2:** a rewritten routine must reproduce the
   cycles **and every other CPU-visible effect of the original, including the
   Z80 R register**, which advances on each opcode fetch. A routine that
   matches the cycles but not R silently changes every later random draw.
   **Approved**; recorded in [design](design.md) section 5.2.

## Evidence already settled

All ROM static, US Rev-A, the same bytes in the Brazilian ROM where checked.

- **The random number generator reads R:** `00:05C8` is `LD A,R` /
  `XOR L` / `LD ($C20C),HL`. Two more `LD A,R` byte matches exist in banks
  20 and 22 (`0x52717`, `0x5B4E0`), not yet confirmed as code.
- **The game reads the Reset button:** the interrupt handler at `00:010F`
  reads port `$DD` bit 4 and, on a press, jumps to `00:0084`. So Reset is a
  button the game polls, not a CPU reset.
- **Mapper registers are read back from RAM:** the handler at `00:0102`-`0109`
  reads `$FFFC` and `$FFFF`.
- **Both headers give region code 4** (Master System export). The reset
  vector is `DI` / `IM 1` / `LD SP,$CB00` / `JR $0084`.
- No direct FM detection read (`IN A,($F2)`) was found; a read through
  `IN r,(C)` is still possible (Part 0).

## Design decisions

- **Same toolchain as MMModern:** MSYS2 UCRT64, GCC, CMake with Ninja, SDL2,
  CTest, GitHub Actions on Windows. The versions on the maintainer's machine
  are pinned and recorded in a new `docs/dependencies.md`. C++20.
- **Layout:** `src/hw/` (Z80 wrapper, mapper and memory, I/O ports, VDP, PSG,
  controller), `src/machine/` (the console as one object and its frame loop),
  `src/rom/` (identification), `src/replay/` (input recordings, headless
  runner), `src/app/` (SDL window, audio, input, save files, command line),
  `third_party/<z80 core>/` with its license, `tests/`.
- **Console model:** Master System 1, export region, VDP 315-5124, no FM
  unit, two standard pads, Reset button present. The Tec Toy release runs on
  the same model; Brazil's PAL-M consoles keep 262 lines of 228 cycles, so
  machine state is the same. Both ROMs use the NTSC clock (3,579,545 Hz); a
  possible 0.1% PAL-M clock difference would only affect host pacing and
  pitch, and is out of scope.
- **Exact hardware scope**, nothing beyond it:
  - **Ports with partial decoding and mirrors:** `$00`-`$3F` even/odd =
    `$3E`/`$3F`; `$40`-`$7F` = V/H counters (read) and PSG (write);
    `$80`-`$BF` = VDP data/control; `$C0`-`$FF` = `$DC`/`$DD`. Port `$3F`
    TH/TR output read back as an export console.
  - **Mapper:** `$0000`-`$03FF` always bank 0; bank numbers masked to the
    ROM's 32 banks; writes to `$FFFC`-`$FFFF` also land in RAM, and reads
    return RAM; cartridge RAM, when enabled, replaces slot 2 for reads and
    writes. The cartridge RAM size is settled in Part 0; the save file is the
    chip image at that size.
  - **RAM:** 8 KiB at `$C000`, mirrored at `$E000`.
  - **VDP:** mode 4 at 192 lines only (other modes, 224/240 lines out):
    control port latch, read buffer, CRAM, frame and line interrupts at their
    exact cycle, line counter reload, status flags and their clearing, sprite
    limit, overflow and collision, scroll locks, left column blank, V and H
    counters, the 315-5124 register masks.
  - **PSG:** SN76489 as Sega built it (16-bit noise register).
  - **Z80:** exact R (bit 7 kept, prefixes counted, HALT counted), I, IFF1/2,
    IM, EI delay, HALT, IM 1 and NMI acceptance; IM 0/2 only if Part 0 finds
    them used.
  - **Pause** as NMI.
- **One cycle counter drives everything.** A single 64-bit Z80 cycle counter;
  the VDP and PSG are event-scheduled from it and can advance N cycles
  without the CPU. Memory and I/O accesses inside an instruction happen at
  their real cycle offset (an `OUT` lands on its last machine cycle).
- **Hooks milestone 2 needs, built and tested now** (nothing uses them yet):
  - a query for the cycle of the next IRQ and the next NMI;
  - advancing the machine by N cycles without executing instructions;
  - an instruction-boundary fetch hook that reports PC and what is mapped
    there (bank 0 below `$0400`, the slot's ROM bank, cartridge RAM or RAM),
    and can continue execution at another address.
- **One machine, one value.** The whole console state is copyable as a
  complete snapshot. The ROM is shared through an immutable pointer, and any
  core callback or context pointers are rebound after a copy. A test copies a
  running machine, runs both copies and compares them.
- **Deterministic by construction.** Nothing from the host reaches the
  machine. Input enters at a defined point: the button state for frame N is
  latched at the start of frame N, defined as the first cycle of line 0; the
  reference driver uses the same boundary.
- **Fixed initial state** (decision 1): every Z80 register
  including R, I, IFF1/2, IM, AF and SP; the VDP registers and the line and
  cycle at the first instruction; the RAM fill; mapper registers; port
  `$3E`; the cartridge RAM fill when no save exists. Documented in
  `docs/development.md` and matched by the reference emulator's
  configuration (no BIOS, power-on randomization off, FM off, export region,
  the same console model).
- **Machine outputs**, the same in the app and in tests:
  - the picture, drawn line by line at the cycle each line is shown, with
    horizontal scroll latched per line and vertical scroll per frame, as
    256x192 CRAM values (6-bit colors);
  - the sound, as a log of PSG writes stamped with their cycle, plus samples
    at a fixed internal rate made from that log. Resampling to the device rate
    happens only in `src/app/`.
- **Program:** the original renderer only, integer-scaled in a resizable
  window, Alt+Enter for borderless fullscreen. Keyboard and SDL game
  controllers mapped to the pad, Pause and Reset (port `$DD` bit 4), plus a
  host power-cycle. Remapping and aspect correction are later work.
- **Saves:** the machine never touches the file system; the app passes the
  save path in. The app keeps the cartridge RAM in
  `%APPDATA%\PSModern\`, one file per ROM version, flushed at the end of any
  frame in which it changed and at exit, by writing a temporary file and
  renaming it. The image loaded at start is part of the initial state.
- **Command line:** `psmodern.exe <rom-path>`; `.sms` files only (the player
  unzips the Brazilian ROM). Identified by SHA-1
  ([design](design.md) section 3); anything else is refused with a clear
  message.
- **Input recordings**, the format milestone 2 keeps: a small text file with
  the ROM's SHA-1, the initial cartridge RAM's hash, and one line per change
  of the button state, with its frame number. Authored with a record mode in
  the app (`--record <file>`); replayed headless by a runner
  (`psmodern_replay <rom> <recording> --dump <frames> --out <folder>`), and in
  the reference emulator by a script driver kept in `tools/`. Each ROM has its
  own recordings, because text lengths change the timing.
- **Nothing derived from the ROM in Git:** frame, RAM and VRAM dumps,
  reference outputs and save images made for tests stay under
  `D:\Projetos\Phantasy Star\`; the repository holds recordings, scripts and
  hashes only.
- **Tests without and with the ROM.** CMake option `PSMODERN_ROM_TESTS`
  (`ON` by default) with `PSMODERN_ROM_US`, `PSMODERN_ROM_BR` and
  `PSMODERN_REFERENCE_DIR` paths. ROM-dependent tests go through one helper,
  `psmodern_rom_test()`. When the option is `ON` and a path is missing, those
  tests skip with CTest's skip code, never fail. CI sets it `OFF`.

## Parts

### Part 0 - Checks before code (read-only)

Results are recorded in this plan before Part A starts.

1. **Z80 core.** Requirements: GPL-3.0-or-later compatible license; passes
   ZEXDOC, ZEXALL and a flags/MEMPTR suite; exact cycle counts, R and
   interrupt timing; memory and I/O accesses at their cycle offset inside an
   instruction; the boundary hook above. Candidates: `floooh/chips` `z80.h`
   (zlib, cycle-stepped, `z80_prefetch`) and `redcode/Z80` (LGPL-3.0,
   instruction-stepped, passes the Rak, Woodmass and Cringle suites).
2. **Reference emulator.** A primary one that runs headless from a script,
   feeds input per frame, and dumps CRAM-index frames, RAM and VRAM; and a
   second one to settle disputes. Check whether Mesen 2 (archived, GPL-3.0)
   or its continuation (named on the Mesen 2 repository as MesenCE) offers
   SMS, Lua and a headless runner; otherwise Emulicious for manual and
   tracing use. Record versions and the configuration matching the fixed
   initial state.
3. **What the game touches**, in both ROMs, by reference traces and ROM
   static reads: every `LD A,R`, `LD A,I` and `HALT`; FM port use, including
   through `IN r,(C)`; IM 0/2; line interrupts; V/H counter reads; reads of
   RAM before it is written; VDP modes used; code running from RAM or
   cartridge RAM; the cartridge RAM size. Each answer adjusts Part B.
4. **Hardware documentation** followed (SMS Power development
   documentation, Charles MacDonald's VDP notes, the SN76489 notes), listed
   in `docs/dependencies.md`.

### Part A - Repository, build and CI

- CMake project, the layout above, a minimal `psmodern.exe` that identifies
  a ROM and exits, identification tests with synthetic files.
- `docs/dependencies.md`, `.github/workflows/ci.yml` (clean Windows machine,
  `PSMODERN_ROM_TESTS=OFF`, actions pinned by commit SHA, read-only token),
  `docs/development.md`, `tools/AGENTS.md` (complete-suite protocol adapted
  from MMModern).

### Part B - The machine

- Z80 core integration, memory and mapper, cartridge RAM, ports, VDP, PSG,
  controller, Pause and Reset, the cycle counter and scheduler, the
  milestone-2 hooks, the snapshot, the fixed initial state.
- Unit tests with the project's own small Z80 test programs: every mapper
  and RAM item above; port mirrors; VDP latch, read buffer, CRAM, interrupt
  timing to the cycle, line counter, status flags, sprite overflow and
  collision, per-line horizontal scroll; R after prefixes, HALT and reset;
  PSG tone and noise; the hooks; snapshot copy-run-compare.
- Z80 conformance: the core's own suites, in CI where their licenses allow,
  otherwise locally from files outside the repository.

### Part C - The program and recordings

- SDL window, scaling, fullscreen, audio, keyboard and controller, save
  files, refusal messages, frame pacing.
- Record mode, the recording format, the headless runner and the reference
  driver.

### Part D - The ROM runs, compared with the reference

- **From power-on, frame by frame:** each ROM, on recordings that reach the
  title screen and a new game in Camineet, compared with the reference every
  frame (CRAM-index picture cropped to 256x192, RAM, VRAM), reporting the
  first frame that differs.
- **Checkpoints across the game:** the first battle and the first dungeon by
  recording; late-game points (a later town, a 3D dungeon deep in the game, a
  boss) started from cartridge RAM save images made in the reference and kept
  outside the repository.
- A difference is a defect of the machine unless shown to be the
  reference's, with hardware documentation, the second emulator or a ROM
  static read, and recorded.
- **Determinism:** the same recording twice gives identical state, picture
  and sound log.
- **`docs/how-it-works/master-system.md`:** the console in plain words (the
  Z80, VDP, PSG, mapper, cartridge RAM, how a frame happens, why R matters for
  the game's randomness), with evidence labels; `docs/AGENTS.md` updated in
  the same change.

## Risks

- **VDP and interrupt timing details.** Exact unit tests per behavior, then
  frame-by-frame comparison from power-on.
- **R and initial-state drift.** One cycle or one fetch of difference at boot
  changes the random sequence and every later checkpoint. Exact R tests, the
  fixed initial state, and the reference configured identically.
- **The reference emulator is not exact either.** A second emulator, hardware
  documentation and ROM static reads settle disputes; each case is recorded.
- **The reference cannot be scripted headless.** Then frame-by-frame
  comparison falls back to the reference's trace and manual dumps at fewer
  points; Part 0 says which, and the plan is amended.
- **Audio crackle or drift** from host pacing; confined to `src/app/`.
- **Scope creep into a general emulator.** The hardware list above is the
  whole scope; no debugger UI, no other games.
- **Toolchain drift** from MMModern. Reuse the same pinned packages.

## Tests and acceptance

- CI green with `PSMODERN_ROM_TESTS=OFF`; the complete local suite green with
  both ROMs and the reference outputs.
- Both ROMs match the reference frame by frame from power-on on the opening
  recordings, and at every later checkpoint, or each difference is explained
  as the reference's and recorded.
- Independent review of this plan (done 2026-11-04) and of the implementation
  after Part D (Tier A).
- Maintainer play-test, one session after Part D, guide in Portuguese: the
  picture and sound feel right, keyboard and controller work, Pause and Reset
  behave, a save made in the game survives quitting and restarting, in both
  versions. Each item says why it needs a play-test.
- At closure: condense this plan, write `docs/project-status.md` and create
  `docs/project-history.md`.
