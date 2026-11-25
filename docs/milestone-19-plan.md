# Milestone 19 plan - Playthrough to the ending

**Tier A. Status: implemented on 2026-11-25; the independent implementation
review and the maintainer's acceptance are pending.** The plan was approved
by the maintainer on 2026-11-24, including the changes from the independent
plan review. Nineteenth milestone. Number 18
belongs to the withdrawn [television pixel shape](milestone-18-plan.md) and
is not reused.

## Goal

Show, on the US Rev-A and on the Brazilian cartridge, one route through the
game from a new game to the title after the credits, continued from in-game
saves with edited strength, that never stops at cartridge code with no C++
version. Also make the part of the game that only state probes reach much
smaller. Nothing the player sees changes unless the work finds a stop. A stop
is then fixed inside this milestone.

## Where 1.3.1 leaves it

ROM run: the routine census of the 27 checkpoint recordings,
`build-rel/routine-census/`, 2026-11-23.

- The recordings reach 1,956 of 3,156 segments: 1,920 on the US cartridge
  and 1,369 on the Brazilian one. 1,200 are probe-only, including the two 1.3.1
  joins `01:5535` and `01:5538` ([project status](project-status.md#known-gaps)
  says 1,198). They are mostly story scenes (about 320), interactions (about
  290), battle (about 220) and items (113).
- 587 segments are reached by a US recording and by no Brazilian one: shops,
  healing places, the field menu, scenes, the map and dungeons. No Brazilian
  recording goes past the deep dungeon.
- The static walk (`tests/static_walk_tests.cpp`) proves the declared exits,
  call returns, slice resumptions and the 17 listed code tables all lead into
  segments. It does not prove five things. A body path no test ran may take an
  undeclared exit. A pushed continuation may lead elsewhere. A table may be
  longer than listed. A guard or bound may refuse an unseen state. A paged
  target passes if any bank has a segment at that address, not the bank that
  is actually mapped. ROM static and Inference.
- `--record` refuses the rate flags and holds fast-forward at 1x, so a
  recording made by hand costs real hours. A stop keeps the last completed
  frame's cartridge RAM (`src/app/main.cpp`, `src/app/frame_driver.cpp`).
- On the maintainer's machine, `../sweep-2026-11-23/report.md` holds a stop
  sweep run from prepared states on both cartridges. It drove the interaction
  table, most dungeon object and exit records, the map scenes, items, spells
  and commands, and found no new stop. It left an unapplied patch and a list
  of unresolved gaps. It is an agent's report, not yet re-verified.

## Design decisions

1. **Agents make the recordings** (Maintainer decision, 2026-11-24).
   - The driver runs the C++ game headless, so a stop shows up as it is met,
     and reads the machine's state. It is working material outside the
     repository (`../m19-playthrough/`).
   - The driver never presses Reset, which the reference refuses. It follows
     the one-frame input mapping ([development](development.md#input-recordings)).
     Each `.psrec` is replayed from power-on with no state restore.
   - A player's save is optional and used only with consent. Its image stays
     outside the repository, and its typed names are never committed.
2. **A playthrough is a chain of links,** one chain per cartridge, starting
   from power-on with empty cartridge RAM. Each link continues from the
   previous save and ends some frames after its own save returns to play.
   Links are 20,000 to 40,000 frames. A committed **chain test** takes
   link N's cartridge RAM at the end of its replay and applies link N+1's
   edits. The SHA-1 of the result must equal the `cart-ram` header of
   link N+1, and the first link's header must be the empty image.
3. **Only strength and money may be edited** (Maintainer decision,
   2026-11-24). This is enforced as an allow-list:
   - Only level, experience, current and maximum HP and MP, attack and
     defense in the active party records may change, plus the meseta. Every
     other byte stays equal to link N's output, and the chain test checks
     this.
   - Each value must be one a player could reach at that point: consistent
     with the level table and within its caps. A dead member is never
     revived.
   - Each link's edits are kept as a committed data file of offsets and
     values. That file holds no ROM data.
4. **The Brazilian cartridge gets its own playthrough** (Maintainer decision,
   2026-11-24). It follows the US route with its own inputs and its own
   saves, made after the US chain. The Portuguese text changes timing and
   random draws.
5. **The group runs only at milestone closure** (Maintainer decision,
   2026-11-24).
   - Every new recording is registered behind the CMake option
     `PSMODERN_PLAYTHROUGH_TESTS`, off by default, with the label
     `playthrough`, as `PSMODERN_FULL_SWEEP_TESTS` is. That keeps the
     complete suite unchanged.
   - `tools/AGENTS.md` gains the group's command, its log under `build-rel`,
     a no-skip rule (a missing manifest fails the run) and when it runs: at
     closure, not before a release.
   - Each link runs as a checkpoint: strict lockstep, the seven reference
     components at every frame and no refused call. It also runs once without
     verification flags, as `psmodern.exe` would. Links get no `_options` or
     automap passivity runs.
6. **The sweep is re-verified, not trusted.** From its patch, the complete
   suite gains:
   - the stricter static walk: exact slot-2 mapping at the three
     fixed-to-paged transfers, the five pushed continuations, completion of
     each block slice;
   - the interrupt sweep of the item-place calls;
   - the guard test (both sides of the 71 entry predicates);
   - the positive-control route.

   Prepared-state routes join the group only where they reach what no
   recording does. Its unresolved items go into the reasons list.
7. **The census tells the evidence apart.** `tools/generate_coverage.py`
   reports the checkpoints, the playthrough group (links and part 3's
   recordings), the prepared-state routes and probes-only. Regenerating
   without the group's census keeps the last committed group figures and
   says so. It never falls back silently. A committed data file gives the
   reason for each segment left probe-only or reached by US recordings only:
   the rare branch, or ROM static evidence that play never reaches it. That
   file uses only `bank:address` and prose.
8. **A stop found is fixed here.** The fix registers the missing segment. It is
   proved by lockstep, extends the static walk to the case it missed and
   updates the how-it-works chapter and the `psmodern-re` catalog. A 1.3.2
   release is proposed only after the group passes with the fix.

## Parts

- **0 - Harness (Opus):** the sweep tests, the CMake option, the chain test,
  the census categories, `tools/AGENTS.md` and
  [development](development.md). Tier A core work.
- **1 - US playthrough (agents):** the driver, the US chain and the reference
  manifests. Early on, measure time a frame, manifest size and whether MesenCE
  captures a 40,000-frame link. Runs beside part 0.
- **2 - Brazilian playthrough:** the same route, after part 1.
- **3 - Triage:** short recordings from edited saves, and routes, for rare
  branches, then the reasons file.
- **4 - Fixes:** only if a stop or a divergence is found.
- **Closure:** the complete suite and the group pass. Then
  [coverage](coverage.md) is regenerated, the independent implementation
  review runs and the documents are updated.

## Risks

- **Generated play can stall** in dungeons, puzzles or battles. The agent
  stops and reports. The maintainer is asked before he would ever be asked to
  play.
- **Size and time.** Estimated at 0.4 to 0.7 million frames per cartridge
  (Inference). At 12 ms a frame under strict lockstep plus the plain run,
  that is about 15 to 30 minutes at `-j 16`. The manifests take about 290
  bytes a frame, up to about 400 MB outside the repository. Above 1 million
  frames per cartridge, or a group run over 45 minutes, the maintainer
  decides.
- **What one route does not show.** It does not cover long play without a
  power cycle, since unsaved work RAM restarts at every link. HP and MP edits
  leave healing places unneeded, so part 3 covers them. Other routes and
  optional places are not covered unless part 3 adds them. Between closures,
  a maintenance fix runs only the complete suite. The next closure's group
  run would catch what it breaks.
- **A late divergence from the reference** follows the existing rule: the
  rewrite is wrong unless the virtual hardware is shown at fault.
- **The acceptance figures are estimates.** If part 1 shows one cannot be
  met, work stops and the maintainer decides.

## Acceptance

1. **US chain:** power-on with empty cartridge RAM to the title after the
   credits, through the petrified companion's rescue (`01:5538`). The chain
   test passes. Every link matches the reference on all seven components at
   every frame and runs under strict lockstep with no refused call. Every
   link also runs without verification flags with no stop.
2. **Brazilian chain:** the same, on the Brazilian cartridge.
3. **Probes only:** at most 400 of the 3,156 segments (now 1,200) are reached
   by no recording on either cartridge. Recordings means the checkpoints, the
   chain links and part 3's recordings, not prepared-state routes. Each
   remaining segment has its reason.
4. **Brazilian gap:** at most 100 segments (now 587) are reached by US
   recordings and by no Brazilian one, each with its reason.
5. The re-verified sweep tests pass on both cartridges. The complete suite
   passes with no skip, in at most 3 minutes over 1.3.1's 1,804 seconds of
   real time.
6. The independent implementation review is done. Acceptance is by the
   maintainer reading the closing summary, which states the route's limits.
   There is no play-test, since nothing playable changes. If a fix changes the
   game, the maintainer decides whether to play it.
