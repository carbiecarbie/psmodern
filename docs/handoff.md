# Session handoff

Last updated: 2026-11-26

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
each of issues 6, 7 and 8, pointing to 1.3.2. All three stay open: 6 and 7
until the reporter says whether 1.3.2 helps on his device (7 asks whether
left and right were always smooth), 8 until the fill choice exists. The S24
holds a debug-signed test build and will not take the official APK without a
backup, an uninstall and a restore.

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
([milestone 18](milestone-18-plan.md)). It is larger than a fix and has no
plan; the widescreen switch is a 0/1 token of the shared settings file
(`src/app/settings.cpp`), which a third value would change.

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
- The macOS port awaits a new ZIP built from current `main` with the dungeon
  fix, and a packaging change that preserves an existing staging folder
  ([requested changes in PR #2](https://github.com/carbiecarbie/psmodern/pull/2#pullrequestreview-6034626295)).
- `../m18-mockup/` can be deleted.
- The maintainer chooses and starts the next milestone explicitly.
