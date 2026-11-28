# Session handoff

Last updated: 2026-11-28

## Optional retro interface & SMS Power! v2.6.1 plans (M20 and M21)

Visual design, distribution decisions and independent Codex plan review are
complete; both chained **Tier A** plans were approved by the maintainer on
2026-11-28 (implementation not started yet):

- **Visual design approved (v2.2, 2026-11-28):**
  `../new-ui-brainstorm/.superpowers/brainstorm/session-1/content/proposta-interface-v2-2.html`
  passed all objective criteria after three council rounds:
  - 256x192 at native 1x pixel scale (single layout across 4:3, 16:9 and
    portrait Android), proportional pixel bitmap font (VWF, clean `l`,
    guaranteed 1 px gap under uppercase PT-BR accents), 2 px outer rounded
    borders + 1 px inner dividers (final 2 px vs. 1 px phone check during M20
    S24 portrait play-test).
  - Strict source fidelity: never renames or translates ROM item/spell/enemy/party
    names (US Rev-A keeps `CRC. SLD`/`LAC. SWD`/`ATTK`; BR Tec Toy keeps
    `ESC. CER`/`ESP. LC`/`NV`/`PE`/`PF`/`PM`/`ATQ`/`DEF`/`PRÓX.`; SMS Power!
    uses `Alisa`/`Myau`/`Tylon`/`Lutz` and full names).
  - Subtle player-requested aids only: full names + balanced wrapping, spell MP
    costs + one-line effect descriptions (without repeating name/cost), shop
    comparison + direct original purchase flow updating Meseta and Bag before
    "Anything else?", inventory `18/24` + action + vertical Up/Down equip
    member selector (`ATK`/`ATQ` for weapons, `DEF` for armor/shields), Status
    `NEXT`/`PRÓX.`, and battle with up to 8 enemy HPs, `w=92` magic/target box
    (`Power Boost`/`10` 11 px gap, `Magic Waller`/`6` 18 px, `Super Heal`/`6 MP`
    12 px), vertical Up/Down ally targeting, and automatic 2-line pagination
    (`▼`) over tall bosses' feet (verified on real Medusa `y=144` and Lassic
    captures) while keeping all four heroes visible.
  - Explicitly excluded: bestiary, permanent overworld HUD, menu-flow rewrites,
    gameplay/rule changes.
- **Distribution decision (2026-11-28):** after reviewing `maxim-zhao/psrp`
  (no `LICENSE` file, non-commercial/original-condition disclaimer incompatible
  with GPL-3.0, issue #140 asking for Frank Cifaldi's agreement, plus multiple
  translators across `en` and `literal`), the maintainer decided **not** to
  bundle any `psrp` text in the repository or releases. Players import the
  single official **Source code (zip)** from the
  [v2.6.1 release](https://github.com/maxim-zhao/psrp/releases/tag/v2.6.1) —
  which unlocks **both** `en` (adapted) and `literal` variants at once — with a
  direct-link button opening the release page in the system browser beside the
  file picker (Windows, macOS, Android) and full translator credits.
- **Approved milestone plans (2026-11-28):**
  Independent Codex review confirmed the M20 -> M21 split and returned
  `Approve` with no remaining findings; the maintainer approved both plans and
  recorded three key decisions (accepting upstream-only `psrp` entries with
  recorded reasons, falling back to the cartridge's native language with one
  localized notice when a saved pack is missing/corrupt at launch while
  retaining the saved SMS preference and saved `INTERFACE` choice, and
  vendoring reader-only `miniz` (MIT)):
  - [Milestone 20 plan](milestone-20-plan.md) (Tier A, approved): canonical
    VDP + `CleanBackground` with a 6-case Part B0 gate (including changing
    scene pixels under an open window and snapshot pairing); single-fire
    observed `00:31CF`/`00:31D4` dispatch + `$C2D3` append + `00:33C9`
    re-entry distinction; single-owner wait table resuming the actual caller
    (including chest `00:28E1` -> `00:37A3`); concise inline Options labels
    (`SMS EN`/`SMS LIT`) + clamped popup + 5-choice `LanguageScreen`; settings
    format 6 across all retained records + POSIX post-rename flush
    reconciliation; exact ROM formulas (`00:1B87`, `00:17BA`, `00:2B84`),
    5-spell box height and curly-quote glyphs.
  - [Milestone 21 plan](milestone-21-plan.md) (Tier A, approved): pinned
    reader-only `miniz` (including `tools/package-android-release.ps1`
    `-UseWorkingTree` copy + APK audit); bounded ZIP stream/parsed limits
    (64 MiB stream / 2,000 entries / 4 MiB single / 8 MiB total / 1 MiB parsed
    <= 600 records / <= 64 tokens + pinned v2.6.1 member SHA-256 digests
    including `plurals.asm`); full token set (`<end>`, `<player>`, `<item>`,
    `<number>`, `<s>` plural rule) + menu precedence (selected language then
    `all`) + per-variant spell fit (`Power Boost`/`Magic Waller` in `en`,
    `Powma`/`Muwarla` in `literal`); two independent inventories; cold-start
    pre-frame-zero pack publication (launcher + CLI) + live `00:0B14`/`00:0F48`
    switches; post-`01:4668`->`01:4676` host translator-credits card.
- **Next step:** start [Milestone 20](milestone-20-plan.md) in a new coding
  session, beginning with Part A (ROM-free VWF primitives) and the mandatory
  Part B0 `CleanBackground` prototype gate before rolling out screens.

## macOS release package and automation (2026-11-27)

[Issue #11](https://github.com/carbiecarbie/psmodern/issues/11): Felipe
Lippelt supplied a [universal ZIP](https://github.com/flippelt/psmodern/releases/tag/v1.4.0-macos)
from the exact upstream `v1.4.0` commit, `c2b4b3c`, and offered a packaging
PR. The official 1.4.0 release still holds only Windows and Android assets.

- Independently downloaded the ZIP: SHA-256
  `85b38e783a1195227dc89d680d3dbbe390e8f0fc97b75169c2c3c3fe4e965c99`
  matches the issue; `unzip -t` passed. The Mach-O has `arm64` and `x86_64`
  slices; the bundle declares 1.4.0 and macOS 11.0 minimum. The app is
  ad-hoc signed and not notarized. No independent macOS launch was run.
- Felipe reports FILL pixel readback on Metal through resize and fullscreen
  on Apple silicon. On the same code with the BR ROM only he reports 191
  passes, 245 skips and two failures attributed to missing start reference
  images. This is not a full BR/US acceptance run or an independent test
  of the downloaded ZIP.
- The maintainer [asked Felipe to open the packaging PR](https://github.com/carbiecarbie/psmodern/issues/11#issuecomment-7230802768)
  on 2026-11-27. The request includes selecting an existing tag so CI can
  build `v1.4.0`. Next, review Felipe's PR when it arrives. Use the existing
  `tools/package-macos.sh` on `macos-15` (already used by CI; `macos-14`
  was retired). Keep the macOS 11.0 target, both architectures, ad-hoc
  signature, and the no-ROM rules. Have CI build from an exact clean ref
  and upload the ZIP and SHA-256 as reviewable artifacts, with no release
  write permission. On macOS, test the produced ZIP after extraction:
  `unzip -t`, both slices, `codesign --verify --strict`, and `--version`
  matching the ref. Record separately whether the GUI/FILL was tested;
  do not substitute Windows for a macOS launch.
- Attaching Felipe's existing ZIP or publishing anything is a separate
  maintainer decision. If a Mac is unavailable, report the unrun test
  rather than blocking the documentation or claiming a runtime pass.


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
[10](https://github.com/carbiecarbie/psmodern/issues/10) (a shop helper and
magic descriptions) was answered and closed as not planned on 2026-11-27 by
the maintainer's decision: such help fits only if he reworks the interface. The S24 holds a debug-signed
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
  FILL by eye on a monitor. For the contributor's Metal check, see above.
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
- The official macOS package and the packaging workflow remain pending (see above).
- `../m18-mockup/` can be deleted.
- The maintainer chooses and starts the next milestone explicitly.
