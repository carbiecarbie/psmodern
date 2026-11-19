# Milestone 14 plan - Removing the Z80

**Tier A. Status: completed and accepted (2026-11-19).** The maintainer
played the twelve play-test items on both cartridges in the accepted Windows
build (SHA-256
`60d835d95f03f05773b6bdafd44567047150b259d698aaa2bb46392770c18179`) and found
everything correct. Implemented in `4aeec95`; the closing commit amends
design and agent instructions. Fourteenth milestone of the [roadmap](roadmap.md); stage
11 of [design](design.md) section 8. Started 2026-11-16.

## Goal

The game runs on C++ alone. The program contains no Z80 interpreter and never
reads a byte of the cartridge to decide what to execute: the ROM is a source
of data. Original mode stays identical to the cartridge in picture, sound,
rules and timing, on both the US Rev-A and the Brazilian versions.

## Where milestone 13 left it

Every game routine had a C++ body, but the virtual Z80 still did four jobs
(ROM run, measured on the 27 recordings):

- it executed the last instruction of every C++ call (393 million times);
- it ran 5.5 million instructions that join one C++ piece to the next, at 459
  addresses, plus the reset and Pause vectors;
- it took over whenever an interrupt or a frame end fell inside a C++ piece
  (448,563 times);
- it accepted every interrupt (221,595 frame interrupts and 52 Pause
  interrupts).

The fast-forward sound also ran a second, private Z80 on the sound driver.

## Design decisions

1. **Exact, not approximate.** Each C++ piece is written as the original
   instructions in order, each with its address, its memory and port accesses
   and its cycles. The program uses that to do what the Z80 did: the start of
   each instruction is a point where an interrupt can be accepted with the
   exact program counter and registers. [Design](design.md) 5.2 expected a
   per-frame time budget model; none is needed, and the exact model keeps the
   original slowdowns by construction (Inference from the kits' structure,
   confirmed by the lockstep results below).
2. **An interrupt inside a piece** is accepted at the original instruction:
   the registers are written back, the return address is pushed, and the
   handler's pieces run until they return to that instruction; then the piece
   continues. The frame wait is split into single-instruction pieces so that
   its interrupt, the usual one, arrives between pieces. If the handler never
   returns (Reset), the interrupted piece is abandoned.
3. **Final instructions and joining code are written in C++** like every
   other instruction: calls, jumps, returns, the interrupt switches and the
   plain instructions that end a piece. Their operands still come from the
   player's ROM as data. The code that joins one piece to the next became
   segments of its own (275 joining segments, found by a static walk of every
   exit).
4. **Frames.** The machine is driven by its cycle counter. A run of one frame
   returns at the first boundary between pieces after the frame end, which
   can be a piece late (at most 5,918 cycles in the recordings, the longest
   step 16,718). What the player sees and hears, the sound writes, the
   widescreen copy and the save are taken at the exact instruction where the
   frame ends, and the comparisons with the reference emulator are made
   there.
5. **The Z80 stays as the referee in tests only.** The pinned Z80 core, a
   machine that runs the original with no rewrite, and the conformance tests
   live in a test-only library (`psmodern_oracle`). Each C++ run is compared
   with the original running beside it ("lockstep"): registers, cycles,
   memory writes and port accesses, boundary by boundary.
6. **No silent fallback.** If execution reaches cartridge code that has no
   C++ body, or a machine state a body does not handle, the program stops the
   game with a message and keeps the save from the last frame end. A census
   of the instructions executed and a static walk bound these cases.
7. **The raw-Z80 developer mode leaves the program** (`--no-routines` and the
   read-only Options state in `psmodern.exe`). The replay runner keeps
   `--no-routines`, now running the oracle.

## Results

- **Recordings.** All 27 recordings (twenty US, seven Brazilian, 222,246
  frames) equal the reference emulator on all seven components on every frame
  and equal the original in lockstep, once at every instruction boundary
  (strict mode). The census of the 27 recordings is empty: no instruction ran
  outside C++, no call was refused, no step went to the Z80 (ROM run). The
  recordings reach 1,956 of 3,154 registered segments; probes reach the rest.
- **Interrupts.** The C++ code accepted all 221,595 frame interrupts (14,789
  of them inside a piece) and all 52 Pause interrupts (11 inside a piece).
  Forced interrupt sweeps put an interrupt at every cycle of the hand-written
  pieces and of sample pieces. The full sweeps of every piece are behind the
  CMake option `PSMODERN_FULL_SWEEP_TESTS` (off by default, label
  `full_sweep`; one test goes from 38 seconds to 594).
- **Coverage of the code.** About 3,150 segments are registered (3,154),
  including 275 joining segments. A static walk of all 3,900 declared exits
  found no target without a segment, and every target of the game's jump
  tables is one (ROM static). The 206 places only probes reached were
  converted or identified: 78 are data, not code, and stop the game if
  executed.
- **Complete suite.** 389 of 389 tests pass with both cartridges and the
  reference assets, with no skips, in 23 minutes 36 seconds (milestone 13:
  353 tests in 39 minutes).
- **Reviews.** The plan and the implementation were reviewed independently
  (engine, kits and conversions, program and tests). No blocker; the findings
  were fixed, apart from a few minor ones and the restructuring noted below.

## What it costs

The new program is larger and slower than milestone 13's, and this is a
measured cost, not a gain:

| | Milestone 13 | Milestone 14 |
| --- | --- | --- |
| Time to emulate one frame, 1x / 4x (US first battle, best of three) | about 1,495 / 1,520 microseconds | about 1,720 / 1,750 microseconds |
| `psmodern.exe`, stripped | 5.5 MB | 9.2 MB |

A frame lasts 16,667 microseconds on the console, so the program still uses
about a tenth of the time it is given. The reason is that every instruction
that the Z80 used to execute is now written out in each piece, and each piece
is built twice (once to run, once to estimate its length), which makes the
code larger and each step slightly dearer. An attempt to cut the estimate
cost gave no measurable gain and was reverted. Removing the estimate run
altogether would be a restructuring of the engine; the maintainer accepted the
cost as it is, since it leaves the game far within its time (maintainer
decision, 2026-11-19).

## Risks that remain

- Cartridge code that no evidence reaches is an error instead of running on
  the Z80. The static walk and the census make this unlikely, not impossible;
  a stop names the place so it can be fixed.
- Probes cover rare paths with sampled interrupt positions by default; the
  full sweeps are an opt-in.

## Acceptance

- The program links no Z80 interpreter; a test checks the link. Met.
- All 27 recordings equal the reference emulator on all seven components on
  every frame and equal the original in lockstep. Met.
- Every probe test passes on both ROMs, with the interrupt sweeps. Met.
- The complete suite passes with both ROMs and no skips; its duration is
  reported. Met (above).
- Independent reviews of this plan and of the implementation. Done.
- One maintainer play-test of the game on the new core. Passed on
  2026-11-19.
