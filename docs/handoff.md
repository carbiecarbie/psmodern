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
(1.3.2 fixed it on his device) and 6 (the touches were accidental). Issues 8 and
9 were answered again with 1.4.0 (above). Issue
[10](https://github.com/carbiecarbie/psmodern/issues/10) (a shop helper)
has no decision and no answer. The S24 holds a debug-signed
test build and will not take the official APK without a backup, an uninstall
and a restore.

## Release 1.4.0 published (2026-11-27)

Published with the maintainer's word: `main` pushed to `c2b4b3c` ("Prepare
release 1.4.0", version only), annotated tag `v1.4.0` on it, GitHub release
"PSModern 1.4.0" (Latest) with the Windows zip (SHA-256 `6852f611...f8c2`)
and the release-signed APK (SHA-256 `f7b25bc1...3967`, versionCode 10400,
same certificate as 1.3.2); the downloaded copies match. Packages in
`../release-1.4.0/`, notes in `../releases/release-notes-1.4.0.md`. The
complete suite of `71840d4` (439 of 439) was reused; on `c2b4b3c` the build
had no warning and the fast group passed 193 of 193. Recorded in the
[project history](project-history.md), status and roadmap. The answers on
issues 8 and 9 are posted; both stay open for the reporter. No release
play-test of the exact packages; the S24 holds a debug-signed build and will
not take the release APK without a backup, an uninstall and a restore.

## Issues 9 and 8, released in 1.4.0 (2026-11-27)

Both were done as one maintenance round (no milestone) and are in `main`
since `71840d4`, pushed on 2026-11-27 with the maintainer's word (`HEAD` =
`origin/main` at that push). CI run 45970838417 of that push passed in both
jobs, Windows and macOS. CI has no ROM: on macOS 438 tests are registered
and the 315 ROM tests skip, so the Mac check is the build and the tests that
need no ROM.

- **Issue 9:** the speed indicator shows the new speed for 2 seconds after
  each press of Speed, `1X` included, then hides; a power cycle or the
  Android background hides it (`SpeedIndicator`, `src/app/host.*`).
- **Issue 8:** FILL (TOTAL in Portuguese) is the third Widescreen choice
  (`WidescreenMode`, `src/app/widescreen_mode.h`). It fits the 344x192
  picture into the output keeping its proportion (`fill_viewport`,
  `src/app/host.cpp`) through a whole-multiple intermediate texture with
  linear filtering (`src/app/sdl_host.cpp`). A file that holds FILL is
  settings format 5; releases up to 1.3.2 refuse it and play with default
  options until Options is applied, and the cartridge save is not affected.
  `../issue8-fill/design.md` has the plan, the decisions and the review.
- **Maintainer decisions after his play-test (2026-11-27):** the names FILL
  / TOTAL and the help text are approved; FILL stays as it is with the touch
  controls visible in landscape, where it gains almost nothing (on his S24
  2413x1347 against ON's 2408x1344, and 2580x1440 with the controls hidden
  by a controller); the three older findings of the review are only
  recorded (see Pending).
- **Checked:** the complete suite, 439 of 439 with no skip
  (`build-rel/issue8-9-suite.log`); the independent review of issue 9 and of
  the settings part (GPT-6.2 Astra: approve with changes, the three about
  tests taken); on this PC's Direct3D the presented pixels read back through
  a resize and a fullscreen toggle in FILL, 2560x1440 showing 2560x1429
  (`../issue8-fill/render/resize_fill.cpp`); on the S24 the measurements
  above, from screen captures (`../issue8-9-playtest/android/captures/`).
  The maintainer's PC session ended with ON written, so nobody has judged
  FILL by eye on a monitor. Nobody has seen FILL on a Mac.
- The S24 holds this round's test build (`../issue8-9-playtest/build.txt`),
  signed with this PC's debug key. `../issue8-9-playtest/android/pad.sh`
  drives a virtual gamepad for checks without a controller.
- The round's worktrees were removed on 2026-11-27 with the maintainer's
  word; the executor's logs and mutation scripts are kept in
  `../issue8-fill/executor-logs/`.

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
- Three findings of the issue 8/9 review, older than that round, recorded by
  the maintainer's decision of 2026-11-27 and not fixed: on POSIX the
  settings file is renamed into place before the directory flush, so a flush
  error reports a failed Apply whose choice is already on disk
  (`publish_bytes`, `src/app/save_store.cpp`); two complete taps on the
  Android Speed button inside one frame count as one; a lazy automap-only
  `SaveStore::choose` on a fresh store drops a stored widescreen mode (only
  tests reach it). The first and third are Tier A fixes.
- Issue 10 (a shop helper) has no decision.
- The macOS package above.
- `../m18-mockup/` can be deleted.
- The maintainer chooses and starts the next milestone explicitly.
