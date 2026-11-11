# Release 0.1.0 plan - start screen and first release

**Tier B. Status: accepted (2026-11-11)** after the maintainer's play-test of
the release zip, which found everything correct. Not a numbered milestone; the
roadmap is unchanged.

## Goal

A player downloads a zip, double-clicks `psmodern.exe`, picks their own ROM
once and plays. PSModern 0.1.0 is the first public Windows release.

## Maintainer decisions (2026-11-11)

- The launcher is a start screen inside `psmodern.exe`: no script, no second
  program.
- The version is 0.1.0, published as a Windows x64 zip in a GitHub Release,
  only after the maintainer's play-test of the exact zip and his explicit
  authorization.
- He is consulted only at the end, for the play-test and for each of commit,
  push, tag and release.

## Parts

- **A - Version and argument modes.** Project version, generated version
  header; no arguments open the start screen, `--version` prints the version.
- **B - Windows platform layer.** UTF-8 arguments, GUI subsystem with
  console attachment (Ctrl+C in the shared terminal is ignored), Open dialog,
  saves folder.
- **C - Remembered ROMs.** `%APPDATA%\PSModern\roms.txt`, written atomically.
  A ROM is forgotten only when it is definitely gone or is another ROM; one
  that cannot be read now (offline share, unplugged drive) stays remembered
  and is not offered that session.
- **D - Start screen and flow.** Original-style window, entries and input;
  the flow (identify, drop missing, pick, retry) runs behind a platform
  interface so tests need no dialogs.
- **E - Program structure.** ROM loading, display and play are separate
  steps; the start screen and the game share one window; held buttons do not
  leak into the game. The command line behaves as before.
- **F - Packaging.** `tools/package-release.ps1` and the player's
  `README.txt`. The script builds `HEAD` from a clean `git archive` export
  (`-BuildDir` only with `-AllowDirty`, for testing), checks the exact DLL
  set and `--version`, writes the zip through a `.partial` file, and names
  the source commit in `THIRD-PARTY.txt`; tag `v0.1.0` must point to it.
- **G - Documentation.** README, development, dependencies, play-tests and
  this plan.

## Tests

- `app_unit`: argument modes and UTF-8 arguments.
- `launcher_unit` (fast, no ROM): ROM list format and corruption, atomic
  write, start screen entries and input, the flow with a fake platform.
- `version_process` (fast): exact output of `--version`.
- `rom_us_start_screen`, `rom_br_start_screen`: labels the ROM font can draw,
  rendering, non-ASCII ROM path.
- `rom_us_unicode_process` (labels fast and rom, the last `RUN_SERIAL` test):
  a real process with non-ASCII ROM and `APPDATA` folders stays alive.

## Verification

The complete suite passes on the final tree; `git diff --check`; the zip is
made from the clean commit; extracted in a clean folder it runs
`--version` with `PATH` limited to System32; the maintainer play-tests that
zip ([release play-test](playtests.md#release-play-test)).

## Release steps

Each only with the maintainer's authorization: local commits; package and
play-test (fixes go in new commits and the zip is rebuilt); push with CI
green; tag `v0.1.0`; GitHub Release with the zip and release notes;
afterwards check that the published asset's SHA-256 matches the zip.

## Out of scope

Executable icon and version resource, code signing, other platforms, and
returning to the start screen after quitting the game.
