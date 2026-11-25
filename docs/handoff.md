# Session handoff

Last updated: 2026-11-25

## Milestone 19 closed

The maintainer accepted [milestone 19](milestone-19-plan.md) (playthrough to
the ending) on 2026-11-25, after the independent implementation review and
its eight required changes. The plan is condensed, and
[project status](project-status.md), [project history](project-history.md)
and the [roadmap](roadmap.md) are updated. Nothing playable changed, so no
release follows.

- Commits: `88aa2cc` (implementation), `d8cc530` (review changes) and
  `3b6e970` (closure). The branch `milestone-19` was merged into `main`; the
  maintainer authorized the push of both repositories on 2026-11-25.
- `build-rel` is configured with `PSMODERN_PLAYTHROUGH_TESTS=OFF` (438 tests),
  the four-patch reference executable
  (`../m19-reference/mesen-publish/Mesen.exe`) and the reference folder
  `../m19-reference/reference/`. The older folder `../m1-partE3/reference`
  and executable `../m1-partD6/source/d6-publish` are no longer used; they
  were left in place.
- The playthrough group (378 tests, 40 to 43 minutes at `-j 16`) runs only at
  a milestone closure: see [the suite protocol](../tools/AGENTS.md).

## Working material outside the repository

- `../m19-playthrough/` and `../m19-playthrough-br/`: the driver that plays
  the game from scripts, the route, the link scripts and each chain's notes.
  A new link or extra recording is made with them.
- `../m19-triage/`: the extra recordings' scripts, the instruction-coverage
  evidence and the generator of `tools/coverage-reasons.tsv` (with the
  location inventory the public file must not hold).
- `../m19-wt/`: the review (`review-codex.md`), the closing summary, the
  patches, and the worktree `part0`, which can be removed.
- `../working-log.md` has the whole history of the milestone.

## Pending

- Candidates left by milestone 19 are in the [roadmap](roadmap.md), none
  decided: the 102 reasons whose trigger is unknown, the reference capture
  that failed twice under suspected load, the extras' start class, the
  group's duration, and trimming [project status](project-status.md) to its
  size target.
- A save near the petrified companion is no longer needed for a recording;
  the player's offer in
  [PR #3](https://github.com/carbiecarbie/psmodern/pull/3#issuecomment-7108021206)
  can be answered.
- The macOS port (PR #2: a universal app of 1.3.1, Apple silicon and Intel,
  macOS 11+, no gameplay change) has the
  [requested changes](https://github.com/carbiecarbie/psmodern/pull/2#pullrequestreview-6034626295):
  its branch includes current `main` (with the dungeon fix and milestone 19),
  `tools/package-macos.sh` stages in a fresh temporary folder and leaves the
  output folder's other files alone, and the ZIP was rebuilt from `b3c4d8c`
  ([contributor reply](https://github.com/carbiecarbie/psmodern/pull/2#issuecomment-7131446820)).
  Later merges of `main` changed no game code, so that ZIP still matches. It
  awaits review.
- `../m18-mockup/` can be deleted.
- The maintainer chooses and starts the next milestone explicitly.
