# Session handoff

Last updated: 2026-11-27

## macOS port merged (PR #2)

With the maintainer's word on 2026-11-26,
[PR #2](https://github.com/carbiecarbie/psmodern/pull/2) (a universal
`PSModern.app` for Apple silicon and Intel, macOS 11+) was squash-merged into
`main` as `972cf08`. Its branch conflicted with 1.3.2 only on the version
line of `CMakeLists.txt`; the resolution (`bce44a2`, keeping 1.3.2) was pushed
to the contributor's branch. On `bce44a2`, whose tree `972cf08` matches: the
complete suite 439 of 439, no skip, 1,690.68 s (`build-rel/pr2-suite.log`,
background shell awaited through its notification), and the PR's CI run with
both jobs passed (the first run of `build-and-test-macos`; GitHub holds a
fork's runs until the maintainer approves them).

- **No macOS package on the Releases page yet**, although the README points
  there. The test ZIP on the contributor's fork (`v1.3.1-macos-2`) is built
  from `b3c4d8c`, 1.3.1 code. A package of a release commit needs a Mac: the
  contributor's, or a CI job that builds it.
- `docs/project-status.md` does not mention macOS; it changes at a milestone
  closure or with the maintainer's word.
- **No-ROM test failures fixed, with a CI guard** (`1a27505`, pushed on
  2026-11-27 with the maintainer's word). With no ROM configured, 111 ROM
  tests failed instead of skipping, as reported in the PR: three registration
  paths expanded their command as an unquoted list, which drops an empty ROM
  path or reference folder. The new `psmodern_add_test` keeps empty arguments.
  Both CI jobs now configure `PSMODERN_ROM_TESTS=ON` with no ROM and run
  `tools/check_rom_skips.py`, which fails unless every `rom` test skipped
  with 77. CI run 45936084504 of that push passed both jobs, and each printed
  "All 315 ROM tests skipped without a ROM." (`build-rel/norom-ci-run.log`,
  a background shell awaited through its notification). The complete suite
  was not run: no production code changed.

## Release 1.3.2 published

The maintainer asked for 1.3.2 on 2026-11-26. `main` was fast-forwarded and
pushed to `a090fa9` ("Prepare release 1.3.2", after `0bae9a7` and `44c14e7`
below); the complete suite of `44c14e7` (439 of 439) is reused under the
release rule, and `a090fa9` ran the build and the fast group (193 of 193).
Packages from `a090fa9`, made by the two packaging scripts, are in
`../release-1.3.2/`: the Windows zip (SHA-256 `3899db47...02c9`; its
`--version` prints `PSModern 1.3.2` from a clean folder with a non-ASCII
name) and the release-signed APK (SHA-256 `9acf7464...fd4a`, versionCode
10302, both ABIs checked by the script). Release notes draft:
`../releases/release-notes-1.3.2.md`.

Published on 2026-11-26 with the maintainer's word: annotated tag `v1.3.2` on
`a090fa9`, GitHub release "PSModern 1.3.2" (Latest) with both packages; the
downloaded copies match the checksums. Recorded in the
[project history](project-history.md). Nobody played 2x or 4x on Windows with
the new clock before the release.

**Issue comments posted** on 2026-11-26 with the maintainer's word, one on
each of issues 6, 7 and 8, pointing to 1.3.2. The reporter then closed 7
(1.3.2 fixed it on his device) and 6 (the touches were accidental). Issue 8
stays open until the fill choice exists; his new comment there shows a black
border on all four sides, which is the whole-step scale (see below), not
overscan, and is not answered yet. He also opened issues
[9](https://github.com/carbiecarbie/psmodern/issues/9) and
[10](https://github.com/carbiecarbie/psmodern/issues/10) (a shop helper and
magic descriptions; no decision, no answer). The S24 holds a debug-signed
test build and will not take the official APK without a backup, an uninstall
and a restore.

## Maintenance round in progress: issues 9 and 8 (2026-11-27)

The maintainer asked for both together and left the choice of a milestone to
the agent: no milestone. He asked the agent to orchestrate up to the
play-test, with GPT-6.2 Astra for the plan and OpenAI models for the code.
Nothing of this round is in `main`; the work is uncommitted in the worktree
`../issue8-9-wt` (detached at `1a27505`, its own `build-rel`), one writer at
a time. `../working-log.md` has the running record.

- **Issue 9, the speed indicator.** It covers the enemy's HP window in
  battle (`../issue9-speed-badge/` has the renders and the approved animated
  mockup). Maintainer decision: the indicator shows for 2 seconds after each
  press of Speed and then hides, and it also shows `1X` for 2 seconds on the
  return to 1x. No new option. Being implemented in the worktree.
- **Issue 8, the fill choice** (section below). `../issue8-fill/design.md`
  holds the design: Astra's plan, what the agent checked and its decisions.
  Two points for the maintainer at the play-test: with the touch controls
  visible in landscape FILL gains almost nothing, because the controls keep
  their place beside the central picture; and a file that holds FILL is
  written in a new settings format that 1.3.2 refuses (it then plays with
  default options; the cartridge save is not affected). Not started.

## The three Android issues (in `main` since `a090fa9`)

A player on a Retroid Pocket 5 opened issues
[6](https://github.com/carbiecarbie/psmodern/issues/6),
[7](https://github.com/carbiecarbie/psmodern/issues/7) and
[8](https://github.com/carbiecarbie/psmodern/issues/8) on 2026-11-25.

### Done and checked on the maintainer's S24

- **Issue 8, the bars.** The Android window is fullscreen from creation and
  the layout ignores the system bars' insets while it is (the camera cutout is
  still respected).
- **Issue 6, the touch controls.** No new option (maintainer decision). They
  start hidden when a controller is already connected, keep their state from
  the start screen into the game, and a touch wakes them only when no
  controller input is held and two seconds after the last one. Mouse buttons
  no longer become touches.
- **A measurement line.** The 10-second report prints `present_frames`: how
  many game frames each presented picture carried.
- Evidence: `ctest -L fast` 193 of 193; the complete suite 439 of 439, no
  skip, 1,688.86 s (`build-rel/android-fixes-suite.log`), before the pacing
  change below. On the S24 (`../android-fixes-playtest/`: APK, guide, notes,
  log, captures) the maintainer ran the guide and said everything looked
  correct; captures show the bars hidden in landscape, none shows the app in
  portrait; the touch rule passed with a virtual gamepad
  (`virtual-pad-test.sh`), since he has no controller for the phone.
- The phone held the personal build of milestone 16, signed with this PC's
  Android debug key, so the test APK was re-signed with that key and installed
  over it. An official release will not install over that app without a
  backup, an uninstall and a restore, which needs the maintainer's word.

### Issue 7, even pacing at 2x and 4x

- **Measured** on the S24 at 60 Hz: of 601 pictures in 10 seconds, 43 and 51
  carried one or three game frames instead of two at 2x, and 74 carried three
  or five instead of four at 4x. Why the reporter sees it only vertically is
  still unknown.
- **Maintainer decision (2026-11-26):** at 2x and 4x the clock may follow the
  screen, and the speed may differ by up to 1%; 1x does not change.
- **Design and reviews:** `../issue7-pacing/` holds `design.md`,
  `plan-review.md` and `implementation-review.md`. The independent plan
  review approved with four required changes, all taken. The independent
  implementation review (a fresh agent) approved with one: the audio margin
  must hold for the queue's peak, not its mean. Taken by lowering the slew to
  0.2%, which locks screens from 59.80 to 60.04 Hz (and twice that).
- **Implemented** in `FrameClock` (`src/app/host.h`, `src/app/host.cpp`), with
  tests in `tests/app_tests.cpp` and the fast-forward paragraphs of
  [player controls](player-controls.md) and [development](development.md).
  On the final code: `ctest -L fast` 193 of 193 (`build-rel/pacing-fast2.log`),
  the four tests that use the clock (`build-rel/pacing-named2.log`), the
  citation checker and `git diff --check`.
- **Complete suite on the final build** (background shell, awaited through its
  notification): 439 of 439, no skip, 1,782.20 s
  (`build-rel/pacing-suite.log`).
- **On the S24 at 60 Hz** (second test build, `../android-fixes-playtest/pacing/`):
  in full windows of 601 pictures, 3 were uneven at 4x and 5 and 2 at 2x
  (before: 74, and 43 and 51). Not the zero the tests' model gives; the cause
  of the remainder was not studied, and only about 40 seconds were played.
  The maintainer has not yet said how the scrolling looked.

### For later test builds

A test APK of an uncommitted tree is built with
`tools/package-android-release.ps1 -AllowDirty -UseWorkingTree
-VersionOverride <version>` and re-signed with the debug key for the S24, as
`../android-fixes-playtest/build.txt` records.

### Decided, not started: filling the screen (issue 8)

The black bars come from the whole-step scale (1920x1080 shows the wide
picture at 5x, 1720x960). On the mockups in `../android-fill-mockup/` the
maintainer chose a third choice in the Widescreen row (off, on, fill; names
provisional) that fills the screen with a fractional scale and light
smoothing. The Options page stays at seven rows
([milestone 18](milestone-18-plan.md)). It has no design note yet. What it
touches: the widescreen switch is a 0/1 token of the shared settings file
(`src/app/settings.cpp`), which a third value would change (releases up to
1.3.2 reject any other token); the scale is a whole number in
`viewport` (`src/app/host.cpp`) and `fit_game`
(`src/app/touch_controls.cpp`), drawn with nearest-neighbour scaling. Open
for the design note: what the choice does on screens wider than 16:9, where
keeping the proportion leaves side bars (smaller than today's).

## State of the build and the working material

- `build-rel` is configured with `PSMODERN_PLAYTHROUGH_TESTS=OFF` and uses
  the four-patch reference executable
  (`../m19-reference/mesen-publish/Mesen.exe`) and the reference folder
  `../m19-reference/reference/`.
- `../working-log.md` has the history, including the playthrough harness
  maintenance merged and pushed on 2026-11-25 (`ef16fcb`).
- `../m19-playthrough/`, `../m19-playthrough-br/`, `../m19-triage/` and
  `../m19-wt/` hold milestone 19's working material; the worktree
  `../m19-wt/part0` can be removed.

## Pending

- Candidates of milestone 19 not started, in the [roadmap](roadmap.md): the
  102 reasons whose trigger is unknown, the reference capture that failed
  twice under suspected load, and trimming
  [project status](project-status.md) to its size target.
- The complete suite takes about 29 minutes; nobody has looked for savings
  in it since milestone 14.
- A save near the petrified companion is no longer needed for a recording;
  the player's offer in
  [PR #3](https://github.com/carbiecarbie/psmodern/pull/3#issuecomment-7108021206)
  can be answered.
- The macOS package above.
- `../m18-mockup/` can be deleted.
- The maintainer chooses and starts the next milestone explicitly.
