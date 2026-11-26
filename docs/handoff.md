# Session handoff

Last updated: 2026-11-25

## Maintenance done: the playthrough group's harness

After [milestone 19](milestone-19-plan.md) closed (accepted on 2026-11-25),
the maintainer asked on 2026-11-25 for two of the candidates it left. Both
are done, reviewed and merged into `main`; he authorized the push.

- **Each extra's start class is inside its `.start` file** (format version
  2, with `class` and `basis` lines). `tools/extra-start-classes.tsv` is
  gone. [Coverage](coverage.md) still reports 6 chain saves, 79 edited and 4
  artificial start images.
- **The playthrough group is shorter.** The 89 start-image tests no longer
  replay a chain link: the mode `start` rebuilds the link's end from images
  already in the reference folder and uses it only once its SHA-1 is the one
  in the link's reference manifest. A start-image test alone says nothing
  about the C++ game; the link's strict test in the same run does. The whole
  group, option on, `-j 16`: 378 of 378 passed, no skip, 1,926 s
  (`build-rel/playthrough-harness-pkg.log`); before, 2,410 s. The long
  strict link tests now set its length.
- The negative cases run in the normal suite as `playthrough_start_rules`
  (439 tests now) and in the coverage tool tests.
- The independent review (an agent with a fresh context) approved with two
  required changes, both made: the search for a byte that the next link also
  edited covers one byte, with a test at its limit; and the maintainer
  authorized the correction of the group's duration in
  [project status](project-status.md). After them, the 237 playthrough tests
  that use the changed test program passed, and the 193 fast tests.
- No game code changed, and the complete suite was not run again: only test
  code, tools, recordings' recipes and documents changed.

## State of the build and the working material

- `build-rel` is configured with `PSMODERN_PLAYTHROUGH_TESTS=OFF` and uses
  the four-patch reference executable
  (`../m19-reference/mesen-publish/Mesen.exe`) and the reference folder
  `../m19-reference/reference/`. The older `../m1-partE3/reference` and
  `../m1-partD6/source/d6-publish` are no longer used; they were left in
  place.
- `../m19-playthrough/` and `../m19-playthrough-br/`: the driver that plays
  the game from scripts, the route, the link scripts and each chain's notes.
- `../m19-triage/`: the extra recordings' scripts, the instruction-coverage
  evidence and the generator of `tools/coverage-reasons.tsv`.
- `../m19-wt/`: the milestone's review, the closing summary, the patches and
  the worktree `part0`, which can be removed.
- `../working-log.md` has the history.

## Pending

- Candidates of milestone 19 not started, in the [roadmap](roadmap.md): the
  102 reasons whose trigger is unknown (49 of them are segments only US
  recordings reach, mostly through checkpoints that exist for the US
  cartridge alone; Brazilian recordings of those actions would settle most),
  the reference capture that failed twice under suspected load, and trimming
  [project status](project-status.md) to its size target.
- The complete suite takes about 29 minutes; nobody has looked for savings
  in it since milestone 14.
- A save near the petrified companion is no longer needed for a recording;
  the player's offer in
  [PR #3](https://github.com/carbiecarbie/psmodern/pull/3#issuecomment-7108021206)
  can be answered.
- The macOS port awaits a new ZIP built from current `main` with the dungeon
  fix, and a packaging change that preserves an existing staging folder
  ([requested changes in PR #2](https://github.com/carbiecarbie/psmodern/pull/2#pullrequestreview-6034626295)).
- `../m18-mockup/` can be deleted.
- The maintainer chooses and starts the next milestone explicitly.
