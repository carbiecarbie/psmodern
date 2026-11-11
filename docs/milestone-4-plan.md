# Milestone 4 plan - First win: encounters and experience

**Tier A. Status: completed and accepted (2026-11-11).** Planned and
reviewed in `f831a09` (independent Tier A plan review, 2026-11-10);
implemented in `3609f6b`; closed by the commit that condensed this plan.
Fourth stage of [design](design.md) section 8 ([roadmap](roadmap.md)).

## Goal

The random-number routine, the encounter checks and the experience award run
in C++, verified against the original call by call. Two options, off by
default, set the encounter rate (off, half or double) and multiply the
experience gained (twice or four times). With them off the game is the
cartridge, US and Brazilian.

## Scope delivered

- **Random numbers** (`00:05B1`), with the exact cycle cost of the original.
- **Map encounter check** (`01:5F63`) with its shared monster-group choice
  (`01:5FD8`) and failure ending; the **dungeon game-mode handler** that holds
  its check (`00:0F3C`); the **experience award** (`00:170D`) with its
  per-character helper (`00:1754`). In all, 24 segments in six units.
- **Two options** on the command line of `psmodern.exe` and of the replay
  runner: `--encounter-rate` (0, 50, 100 or 200) and `--experience-rate`
  (100, 200 or 400), in percent of the original, 100 meaning unchanged and
  being the default; any other value is refused. How they work:
  [random numbers](how-it-works/random-numbers.md#the-encounter-rate-option),
  [party](how-it-works/party.md#the-experience-rate-option),
  [development](development.md#options).
- **Mechanism, census and docs:** segments in the registered-routine
  mechanism, a census with forced-call counts, updated chapters and coverage.
- **Left out:** battle setup and reward production (`01:5FFE`), the cell test
  (`01:6BCA`), text, record refresh (`00:17BA`), choosing the options inside
  the game and saving them, and money (both are wanted later; see the
  [roadmap](roadmap.md)).

## Key decisions

- **Segments ending on the core.** The checks and the award call original code
  that waits for frames, shows text or starts battles, so a routine is
  registered as several segments: one at its entry and one at each address
  where control returns from such a call. Each segment is atomic, and its last
  instruction always runs on the Z80 core, as the return did in milestone 2.
  No C++ imitates a core step or keeps state across one, so machine copies and
  the per-call verifier keep working. Rejected: a C++ coroutine and patches on
  the native path.
- **Faithful at every entry.** A segment renders the original instructions
  from its address for any machine state; the shared helpers are one function.
- **Options on every call.** Each option acts in a segment of exactly one
  original instruction run by the core (the two threshold comparisons, the
  reward load and the per-character addition), with an adjustment around it.
  Such a segment adds no time, so it is admitted at any interrupt or frame
  window when its option is active. At 100 no adjustment code runs.
- **Census rule.** 90% C++ from 20 calls; no forced calls in original mode;
  every segment a recording reaches needs a C++ call or a written reason.
- **Random numbers in C++** (Tier A), since both checks call them.
- **Maintainer decisions (2026-11-11).** The maintainer asked not to be
  consulted during the work; five choices were provisional until closure, and
  he decided them on 2026-11-11 after playing the build (Maintainer decision):
  1. Whether reduced or off encounters still consume the random draw:
     Confirmed: the draw is still consumed and only the outcome changes, so
     the option does not affect other things in the game.
  2. Encounter rate (drafted as 0 to 400, default 100); threshold scaled, at
     most 255, the map's vehicle reduction first: Changed: fixed values 0, 50,
     100 or 200 percent instead of a range, 100 the default; the formula is
     unchanged.
  3. Experience rate (drafted as 25 to 1000, default 100); one scaled reward for
     the message and every character, never zero, at most 65,535, stored
     reward untouched: Changed: fixed values 100, 200 or 400 percent instead of
     a range, 100 the default; the formula is unchanged.
  4. Levels per award: one, as the original; with the option on, a character at
     the 65,535 ceiling still gains one level per award up to 30: Confirmed,
     including that a character at the ceiling keeps gaining one level per
     fight; several levels per award is declined.
  5. Options by command-line flags only, refused with `--no-routines` and
     `--record`, not saved: Accepted for this milestone. Wanted in a later
     milestone: choosing the options inside the game and saving them with the
     game, and battle money that follows the experience rate (see the
     [roadmap](roadmap.md)).
- **Feel reference** (Maintainer decision, 2026-11-10): the Switch
  release's easy mode. No official numbers exist; fan measurements give about
  x4 experience (Community source) and none for encounters (Inference:
  placeholder). Values preferred: encounters 50, experience 400
  (Maintainer decision, 2026-11-11). They are named constants in
  `src/game/options.h`; the future in-game selection must show the default and
  this recommendation.

## Results

- **No change to play in original mode:** all twelve checkpoints pass the
  per-call verifier, the routines on/off comparison and the milestone 1
  reference, with no forced calls. Both flags at 100 give byte-identical runs.
- **Recording coverage (ROM run):** random numbers ran in C++ on 1,084 calls
  and fell back on 1; the dungeon handler on 12,012 calls with 6 fallbacks
  (its busiest piece 11,902 with 110); the map check on 142 with none; the
  award on 4 with none. Every fallback was a frame boundary under the window
  rule; the lowest share is 99.084%. The recordings are thin: two of 192 map
  regions and one level gain.
- **State probes carry exactness**, US and Brazilian, whole machine, each
  asserting that C++ ran: 132,770 random-number states per ROM; all 192 map
  region records, all 65,536 comparison inputs, 135,424 dungeon cases and
  whole checks; all 65,536 reward values, 236,921 sums, 3,000 whole awards.
- **Options:** the four encounter rates (0, 50, 100, 200) and the three
  experience rates (100, 200, 400) over every input match the formulas: both
  compares at each encounter rate over all 65,536 inputs, and all 65,536
  rewards at each experience rate. Values outside the lists (for example
  encounters 25 or 400, experience 25 or 1000) are refused by the option check,
  the flag parser and the programs, with a message naming the allowed values.
  The clamp at 255 is covered at rate 200 and the clamp at 65,535 at rate 400
  (a reward above 16,383). Rate 0 consumes the draw and never starts a random
  fight; the displayed reward equals each character's gain; forcing admission
  at every cycle of interrupt, NMI and frame windows leaves the machine
  identical. Enhanced runs (encounters 50 with experience 400, encounters 0 with
  experience 200, and experience 400 alone) on all twelve recordings: no
  guard fired, no fallback in an option segment, no failed forced call.
- **Original ceiling (ROM static, Inference):** no sequence of battles reaches
  the experience ceiling before the top level (largest reward found 200,
  smallest margin 2,535); one fixed fight was not enumerated (Unknown).
  Five-digit rewards print intact in both versions (ROM run; a stored reward
  of 12,500 at 400 shows 50,000). See
  [original quirks](original-quirks.md).
- **Speed:** headless deep-dungeon medians of 5 runs: +0.18% (US) and -0.95%
  (Brazilian) against the same tree with routines off; +2.54% and +3.10%
  against the previous build with routines off (noise about 5%; limit 10%).
- **Validation and reviews:** the complete ROM-enabled suite passes, 90 of 90
  with no skip. The plan, mechanism and final implementation reviews each
  ended "accept with required changes", all applied; the last compared all 24
  segments with the original and found nothing that changes original mode.
- **Play-test** (2026-11-11): the maintainer played the options with the
  launchers in the US version and reported that everything was correct.
