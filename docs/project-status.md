# PSModern - Project Status

**Milestone 1 (foundation) is complete,** accepted on 2026-11-08. Phantasy
Star runs from the player's own ROM on a virtual Master System, in original
mode, in both the US and the Brazilian version. Nothing of the game is
rewritten yet: the whole original program runs on the virtual Z80.

The next stage in [design](design.md) section 8 prepares the first
rewritten routines: a way to run a C++ routine in place of the original and
check, cycle for cycle, that the game behaves exactly the same, tested first
on a trivial routine. The maintainer starts each
milestone explicitly.

## What can be played

The whole game, as on the cartridge.

- **Starting:** `psmodern.exe <rom-path>`, with an uncompressed `.sms` file of
  the US Rev-A or the Brazilian Tec Toy cartridge. Any other file is refused
  with a message. The window title names the version.
- **Picture:** the original 256x192 picture, scaled by whole steps in a
  resizable window, with black bars. Alt+Enter toggles fullscreen.
- **Sound:** the original sound chip, resampled for the computer's audio
  device.
- **Controls:**

  | Console | Keyboard | Game controller |
  | --- | --- | --- |
  | Directional pad | Arrows | D-pad or left stick |
  | Button 1 / 2 | Z / X | A / B (bottom / right) |
  | Pause | Enter | Start |
  | Reset | Backspace | Back |
  | Power off and on | F5 | - |

  Keyboard and controller work together. The first controller found is used,
  also when it is plugged in during play.
- **Saves:** the game saves through its own menu, as on the cartridge. Each
  version keeps one file in `%APPDATA%\PSModern\`. Before the first write of
  a session, the previous file is copied to a `.bak` file. Only one running
  copy of PSModern may use a save file; a second one is refused.

There are no options yet: every comfort feature of the design is still to
come.

## Architecture overview

The design is in [design](design.md) section 5. Milestone 1 built its lowest
layer.

| Part | Folder | What it does |
| --- | --- | --- |
| Console | `src/hw/`, `src/machine/` | Z80, memory and mapper, cartridge RAM, ports, VDP, PSG, pads, Pause and Reset, driven by one cycle counter |
| ROM | `src/rom/` | Identifies the two supported cartridges by SHA-1 |
| Program | `src/app/` | SDL window, sound, input, save files, frame pacing |
| Recordings | `src/replay/` | Input recordings, the headless replay runner |
| Reference | `tools/` | The driver that replays recordings in a patched MesenCE, and the comparison tool |
| Tests | `tests/` | Unit, conformance, ROM and checkpoint tests |

Some properties matter for the next milestones:

- **Exact and deterministic.** Nothing from the host reaches the machine.
  The same recording always gives the same frames, byte for byte.
- **Copyable.** The whole machine copies as one value, so a run can be
  forked and compared.
- **Ready for rewritten routines.** The machine can already stop at any
  point of the original code, run something else instead and keep the
  timing exact. This is tested but not used yet.

How the console works, in plain words, is in
[how the Master System works](how-it-works/master-system.md). Building,
testing, recordings and the reference are in [development](development.md).

## Rewritten and original systems

| Game system | State |
| --- | --- |
| Everything (map, dungeons, battle, menus, text, sound driver, saves) | Original Z80 code |

No routine is rewritten yet; that starts after the verification stage.

## How it is verified

- **Frame-by-frame comparison** with a patched MesenCE 2.2.1, on twelve
  recordings, six per version: the opening, the first battle, the first
  dungeon, a later town, a boss fight and a deep dungeon. All 124,920 frames
  match in the picture, RAM, video memory, palette, cartridge RAM, CPU state
  and sound writes.
- **Conformance programs:** the Cringle, Rak and Woodmass Z80 suites, and
  the VDPTest results for the SMS1 video chip.
- **CI** on a clean Windows machine runs every test that needs no ROM or
  external test file. The
  ROM and reference tests run locally, as [development](development.md)
  describes.

## Known gaps

- **No options and no rewritten routines yet.**
- **Windows only** for the save location.
- **The Brazilian version is checked by its recordings, not by play.**
  Development and play-tests use the US version (design 5.5); the Brazilian
  version's six checkpoints run in the tests.
- **Some details of the first Master System's video chip are unmeasured.**
  PSModern follows the reference there, and [development](development.md)
  labels them Unknown. The game reaches two of them, video memory accesses
  at two moments of the US recordings, where PSModern matches the reference
  but the real console's behavior is not known.
- **Controls are fixed.** Remapping and the second pad are later work, as
  is aspect correction of the picture.
- **Sound stalls** of about 90 ms were seen once; two later runs of about
  two minutes each did not show them.
