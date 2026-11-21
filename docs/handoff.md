# Session handoff

Last updated: 2026-11-21

Release 1.1.0 is published on GitHub (tag `v1.1.0` on `5afed78`, zip and notes
in `../releases/`). No APK is published; the Android build stays personal.

Latest change: documentation overhaul and accuracy pass.
- Rewrote `README.md`, `README.pt-BR.md` and `tools/release/README.txt` to be
  shorter, clearer and more readable, highlighting full playability, the C++
  rewrite with no Z80, optional features and installation steps; losslessly
  recompressed `docs/images/options.png` (1.33 MB -> 9.4 KB).
- Added `docs/how-it-works/README.md` index, updated `docs/AGENTS.md`, and
  consolidated detailed player option/automap/save/CLI guides in
  `docs/player-controls.md`.
- Corrected outdated statements, recording counts, duplicates and typos in
  `docs/project-status.md`, `docs/development.md`, `docs/design.md`,
  `docs/original-quirks.md` and `docs/how-it-works/*.md`.

No milestone is active; the maintainer starts the next one explicitly.

Note kept from the 1.1.0 release checks: `launcher_unit` once failed on its own
right after `ctest -L fast` (then passed 330x in a row). If it recurs, capture
output and check the `steady_clock` suffix in `tests/launcher_tests.cpp`.
