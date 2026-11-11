# Milestone 4 plan - First win: encounters and experience

**Tier A. Status: in progress (2026-11-10).** Design section 8, stage 4;
[roadmap](roadmap.md#milestone-4---first-win-encounters-and-experience).

## Goal

The encounter checks, the experience award and the random-number routine run
in C++, verified against the original call by call. Two options, off by
default, let random encounters be rarer, more frequent or off, and multiply
the experience gained. With them off, the game plays, looks, sounds and times
exactly like the cartridge, US and Brazilian.

## Scope

- Random numbers (`00:05B1`); the map check (`01:5F63`) with its shared
  monster-group choice (`01:5FD8`) and failure ending (`01:5FF9`); the whole
  dungeon game-mode handler containing its check (`00:0F3C`); the experience
  award (`00:170D`) with its per-character helper (`00:1754`). The three bare
  calls and returns between dungeon segments contain nothing to rewrite and
  stay on the Z80; the handler still counts as rewritten.
- Options set by command-line flags, in the app and the replay runner.
- Out of scope: battle setup and reward production (`01:5FFE`), the cell
  test (`01:6BCA`), text, record refresh (`00:17BA`), settings files, money.

## Design decisions

**Segments ending on the core** (extends the
[registered-routine mechanism](development.md#registered-routines)). The
checks and the award call original code that waits for frames, shows text and
starts battles. A rewritten routine is therefore registered as several
*segments*: one at its entry and one at each address where control comes back
from such a call. Each segment is atomic, as in milestone 2, and its last
instruction is always executed by the core, as milestone 2's return already
is: a return, a call or jump into original code, or a plain instruction. The
callee runs natively with normal interrupts and frames; the next segment
fires when it comes back, since dispatch happens at every registered
boundary. No C++ code imitates a core step and no C++ state outlives a step,
so machine copies and the per-call verifier keep working and a fallback in
one segment does not affect the others. Rejected: a C++ coroutine (state
outside the machine) and native-path patches for the options.

**Faithful at every entry.** Each segment renders the original instructions
from its address for any machine state, so an interior address means the same
however it is reached. The shared helpers (`01:5FD8`, `00:1754`) use one C++
function for both ways in.

**Permissions and verification.** A segment may write the slot-2 mapping
register only when declared; other mapping, memory-control and interrupt
guards stay. Scratch stack traffic goes through the bus. The per-call
verifier still runs the original first, stops at the declared exit with the
expected stack and compares the whole machine. The census gains a unit column
and separate forced-call counts; 90% C++ applies from 20 calls, and any
segment a recording reaches needs one C++ call or a listed reason.

**Options on every call.** Each option acts in a segment of exactly one
original instruction, executed by the core, with an adjustment before or after
it: the two threshold comparisons (`00:0F5B`, `01:5FB0`), the reward load
behind the displayed number (`00:170D`) and the per-character addition
(`00:1773`). Since the instruction itself runs natively, such a segment can run
inside any interrupt or frame window with **zero** added delay; when its option
is active it is always admitted. The other fallback reasons cannot occur there
(Inference from ROM static and the core source); if one ever did, the call is
counted and falls back. In original mode these entries follow the milestone 2
window rule, and with every option at its default the adjustment code does
not run at all.

## Provisional decisions (to be confirmed by the maintainer at closure)

The maintainer asked not to be consulted during the milestone.

1. **Encounters off or reduced still consume the draw**; only the comparison
   changes. Fixed and story fights are untouched.
2. **Encounter rate** 0 (off) to 400%, default 100: threshold x rate / 100,
   rounded down, at most 255; map and dungeon; the map's vehicle reduction
   applies first.
3. **Experience rate** 25 to 1000%, default 100. One scaled reward serves the
   displayed number and every eligible character: rounded down, at most
   65,535, never zero when the original is nonzero. The stored reward is not
   modified, so it is never scaled twice.
4. **One level per award**, as the original. When the experience rate is not
   100, reaching the 65,535 cap no longer skips the level test, so a
   character keeps gaining one level per award up to 30. Original mode keeps
   the original behavior. Alternative for closure: several levels per award.
5. **Command-line flags only**: `--encounter-rate` and `--experience-rate`;
   omitted means original. No settings file, no save change. Refused with
   `--no-routines` and `--record`, and in the runner with `--lockstep-calls`
   or `--compare-recording`.

A save made with options on loads in original mode with totals the cartridge
alone might not produce; options are not stored in the save. "Switch-like"
play-test values (the maintainer's reference): experience 400% (Community
source: fan measurements of about x4) and encounters 50% (Inference:
placeholder). A money multiplier is a possible later option.

## Risks

- **Experience cap in the original** (Unknown): the margin above the last
  level is small for one character, so the original may reach the cap with
  one large award. Part D establishes the largest original reward and records
  the result in [original quirks](original-quirks.md).
- **Verifier and census changes** could weaken milestone 2 guarantees; every
  existing fault case must keep failing with its message.
- **Hidden assumptions** in a segment would pass the recordings; probes vary
  mapping, index registers, R, stack position and unrelated state.
- **Thin recording coverage** (two of 192 map regions, one level gain; ROM
  run, milestone 4 study); state probes carry exactness.
- **Brazilian five-digit rewards** may print differently; an enhanced
  Brazilian case checks a reward above 9,999.
- **Speed**: headless time within 10% of the milestone 2 baseline.

## Parts

One agent session each (details in the private design notes). A1, A2 and B
are Tier A (one review covers A1 and A2); C, D, E are Tier B; F ends with the
Tier A implementation review.

**A1** mechanism and verifier, ROM-free tests; **A2** census, coverage
generator, checkpoint rule, development notes; **B** random numbers, option
ranges and file skeleton; **C** encounter checks and option; **D** experience
award, option and cap rule; **E** command-line options, in parallel with C
and D; **F** integration, coverage, speed, docs, play-test preparation.

## Acceptance

- **State probes** for every segment, US and Brazilian, against the original
  with whole-machine comparison, each asserting that C++ actually ran:
  exhaustive for the random routine's state, both comparisons, the group
  choice, all 192 map region records and every reward value; sampled with all
  edges for level and experience combinations.
- **Recordings:** all twelve checkpoints pass the per-call verifier, routines
  on/off and the milestone 1 reference, with no forced calls and the coverage
  rules above. Runs with both flags at 100 give identical manifests and
  census to runs without them.
- **Options:** a 0% rate consumes the draw and never starts a random fight;
  the displayed reward equals each character's gain; past the cap a character
  still gains one level per award; every option segment has zero fallbacks on
  the US recordings with options on; forcing admission at every cycle of an
  interrupt, NMI and frame window leaves the machine identical, also a few
  steps later.
- **Mechanism and docs:** ROM-free tests pass in CI and the complete suite
  passes; [random numbers](how-it-works/random-numbers.md),
  [party](how-it-works/party.md), development notes, original quirks,
  generated [coverage](coverage.md) and private names are updated.
- **Play-test** (one US session after Part F): the feel of encounter rates
  (0, 50, 200, 400) on the map and in a dungeon, and the reward message and
  level gains at 400% and 1000%. These need a person because they are about
  how the options feel and read; exactness is settled by the tests. One
  original-mode recording of vehicle travel may be added then; acceptance
  does not depend on it.
