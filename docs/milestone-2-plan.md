# Milestone 2 plan - Verification: rewritten routines and the lockstep run

**Tier A. Status: completed and accepted (2026-11-09).** Planned, reviewed
and approved in `43b05b9`; implemented from `60a6f9e` to `70c3454`; closed
by `f6d0fc7`, which condensed this plan. Second stage of [design](design.md)
section 8, completing the verification base begun in milestone 1.

## Goal

A routine of the original program can run as C++ instead, with proof that
the player gets the same behavior, picture, sound and timing. The first
replacement reads the controls; the rest of the game still runs on the Z80.

## Scope delivered

- **Registered routines:** C++ replacements selected by ROM `bank:address`,
  with call and fallback counters outside the copyable machine state.
- **Exact timing:** each replacement accounts for the original cycles,
  opcode fetches, R register and bus accesses. The original return executes
  on the core to preserve its internal state. Cost overruns and forbidden
  mapper or interrupt-source writes stop the run.
- **Per-call verification:** a copy runs the original routine from the same
  entry, then compares the whole machine. A difference identifies the
  recording, frame, cycle, routine, call and fields, without exposing values
  that could contain ROM data. Defaulted component equality also catches
  fields missing from the diagnostic list.
- **Per-recording verification:** routines on and off produce the same
  seven-component manifests on every frame, and still match milestone 1's
  reference recordings. [Coverage](coverage.md) is generated from the
  twelve checkpoint censuses.
- **First routine:** controller polling at `00:0339`, shared by both ROMs,
  records held and newly pressed buttons. Its plain-language explanation
  is in [controls](how-it-works/controls.md).

## Key decisions

- **Interrupt window rule** (Maintainer decision, 2026-11-09): a replacement
  runs only if its declared worst-case window cannot accept an interrupt or
  cross a frame boundary. Otherwise the original handles that call. This
  keeps timing exact without splitting or speculatively running routines.
- **Routines on by default** (Maintainer decision, 2026-11-09): verified
  replacements are part of original mode. A developer switch disables them
  for diagnosis; this does not enable any optional game feature.
- **Full machine equivalence:** hidden state is compared too. The existing
  core exclusions cover only values overwritten before use; the derived VDP
  IRQ cache is excluded. The diagnostic backstop preserves those exclusions.
- **Narrow first scope:** replacements cannot change interrupt sources or
  mapping, adjust the stack, have interior entries or exit without returning.
  Naming in the private disassembly waits for milestone 3.

## Results

- **No change to play:** all twelve checkpoints, six per version, match the
  reference on all 124,920 frames, with no per-call divergence. Controller
  polling runs in C++ on all 93,555 observed calls: 100% in each checkpoint,
  above the 90% requirement, with no fallback. This is recording coverage,
  not a census of every possible route through the game.
- **Controls checked exhaustively:** all 65,536 previous/current button
  combinations per ROM match the original, including exact machine state
  and timing. Authored programs also exercise interrupt fallbacks, guards
  and deliberately faulty replacements. ROM-free tests cover the diagnostic
  backstop and preserve redaction and equivalence exclusions.
- **Speed:** median headless time changed by +0.466% for US and -1.455% for
  Brazilian, within the 10% slowdown limit. These small differences do not
  establish a speedup. The production loop measured about 59.92 fps without
  a clock rebase.
- **Validation and reviews:** the complete ROM-enabled suite passes.
  The ROM-free build passed the verification tests too. Independent Tier A
  reviews of the plan and implementation passed, including the final
  diagnostic and test-cleanup fixes.
- **Play-test** (2026-11-09): the maintainer's US session passed. The
  Brazilian version remains checked automatically by its six recordings;
  development and play-tests use US.
