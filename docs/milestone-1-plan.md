# Milestone 1 plan - Foundation: the original game runs

**Tier A. Status: completed and accepted (2026-11-08).** Planned and
reviewed on 2026-11-04 (`e9ee50b`, `689a680`); implemented from `8224972` to
`4585634`; closed by the commit that condensed this plan. First stage of
[design](design.md) section 8.

## Goal

The player starts `psmodern.exe` with their own ROM, and Phantasy Star runs
in original mode with the picture, sound, controls and saves of the
cartridge. The whole ROM runs on a virtual Master System; nothing is
rewritten yet. The machine must be exact, deterministic and observable,
because every later milestone checks its rewritten routines against it.

## Scope delivered

- **The console:** a Z80 (`redcode/Z80` at `dcc3f28d`), the mapper and its RAM
  mirror, 8 KiB of battery-backed cartridge RAM, partially decoded ports,
  the 315-5124 VDP in mode 4 at 192 lines, the SN76489 PSG, two pads, Pause
  and Reset. One 64-bit cycle counter drives everything, and the whole
  machine copies as one value.
  - Ports decode partially: `$00`-`$3F` memory and I/O control, `$40`-`$7F`
    V/H counters (read) and PSG (write), `$80`-`$BF` VDP, `$C0`-`$FF` pads.
  - The mapper keeps `$0000`-`$03FF` on bank 0 and masks bank numbers to the
    ROM's 32 banks. Writes to `$FFFC`-`$FFFF` also land in RAM. Enabled
    cartridge RAM replaces slot 2.
- **The program:** an SDL window with integer scaling and fullscreen, sound,
  keyboard and a basic game controller, save files, and refusal of unknown
  ROMs. Both the US Rev-A and the Brazilian cartridge run.
- **Verification:** input recordings, a headless replay runner, and a driver
  that replays the same recordings in a patched MesenCE. Tests compare the
  two frame by frame.
- **Hooks for milestone 2,** tested but unused: next IRQ/NMI query, advancing
  the machine without instructions, and an instruction-boundary fetch hook.
- **The first how-it-works chapter,** about the console:
  [master-system.md](how-it-works/master-system.md).

## Key decisions

- **Start without the BIOS** (maintainer decision, 2026-11-04). The cartridge
  starts from a fixed, documented state; it fixes the random sequence just
  as a BIOS would, without asking the player for a BIOS dump.
- **Rewritten routines keep R** (maintainer decision, 2026-11-04; design
  section 5.2). They must reproduce the cycles and every CPU-visible effect,
  including the Z80 R register.
- **The reference is a patched MesenCE 2.2.1,** with Emulicious as the
  independent judge (maintainer decision, 2026-11-05). Three patches fix CE
  faults, each resting on sources independent of PSModern: wrong opcode
  durations, interrupt sampling at the instruction boundary, and block I/O
  flags. The accepted build is pinned by SHA-256.
- **VDP memory access follows the reference's slot grid** where SMS1
  hardware is not measured; the background is fetched per tile column.
  Disputed cases stay labeled Unknown in [development](development.md).
- **After the implementation review** (maintainer decisions, 2026-11-08):
  - no CRAM dot during the active display, like the reference;
  - checkpoints also compare the CPU state and the sound writes;
  - a `.bak` copy of the save before the first write of each session;
  - only one running copy per save file;
  - input reaches the game one frame after it is sampled, as in the
    reference;
  - the basic controller is always on; only full controller support
    (remapping, shortcuts) is optional.

## Reference emulator

MesenCE 2.2.1 (`20ba206c`) runs both ROMs headless with Lua and
`--testRunner`, from empty firmware and save folders. It is set to SMS1
(315-5124), NTSC, zero-filled RAM, FM off and two pads. Before the first
instruction the driver sets the VDP to line 0. A readback at that point
gave line 0, VDP cycle 1 and CPU cycle 0, with PC=`0000`, AF=`AB42`,
BC=`0002`, DE=`A51B`, HL=IX=IY=`0000`, SP=`DFF0`, alternate
AF/BC/DE/HL=`0000`/`0200`/`C700`/`C75F`, I=`00`, R=`1D`, both IFFs clear and
IM 1. Other start values come from CE's initializers. Each run starts from zero or from a given
cartridge RAM image, checked before the first instruction. The driver and
the fixed start are in [development](development.md#reference-driver).

## What the game uses

ROM static, reference emulator and ROM run observations, identical in both
versions at every cited address.

- **The random draw reads R** at `00:05C8`. It is the only confirmed read of
  R; other byte matches are graphics data.
- **The game polls the Reset button** in its interrupt handler at `00:010F`
  and restarts at `00:0084`, the same place power-on reaches.
- **Mode 4 at 192 lines only,** with the frame interrupt; no line interrupt,
  no V/H counter reads, no FM, no IM 0/2 and no code run from RAM on the
  observed routes. The game never executes HALT in the twelve checkpoints.
  Whole-game absence is not established: 228 US / 227 Brazilian HALT byte
  matches and six IM 0/2 matches have no established instruction boundary.
- **Cartridge RAM:** enabled at `00:07DC`; an 8 KiB image per version.

## Results

- **Frame by frame:** twelve checkpoint recordings, six per version, match
  the reference on every frame: 124,920 frames. Each frame compares seven
  things: the picture, RAM, VRAM, CRAM, cartridge RAM, the CPU state and the
  sound writes.
  - Three checkpoints start at power-on: the opening, the first battle and
    the first dungeon.
  - Three start from saves made in the reference by the maintainer: a later
    town, a boss fight and a deep dungeon. The Brazilian images were written
    back by the Brazilian game.
  - Each checkpoint recording ran twice with byte-identical output. The
    suite repeats the short recordings and a 1,200-frame run.
- **Conformance:** the Z80 suites by Cringle, Rak and Woodmass pass on the
  integrated core. The pinned VDPTest passes its 55 automated SMS1 results.
- **Reviews:** independent reviews of the plan (2026-11-04) and of the
  implementation (2026-11-08). Fixes and maintainer decisions closed the
  implementation review's findings; a second review accepted the fixes.
- **Play-test** (2026-11-08): the maintainer played the US version and
  found picture, sound, keyboard, controller, Pause, Reset, saves, power
  cycle and the second-copy refusal correct. By maintainer decision, the
  Brazilian version was not play-tested. Its checkpoints and the shared save
  code cover it.
