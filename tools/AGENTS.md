# Complete CTest suite protocol

Read this before running or delegating the complete suite or rerunning failures.
The root instructions apply. Run the complete suite on `build-rel` after the
final production build of each task. A complete run needs both ROM paths
(`PSMODERN_ROM_US`, `PSMODERN_ROM_BR`) and `PSMODERN_REFERENCE_DIR`,
`PSMODERN_Z80_TEST_DIR`, `PSMODERN_VDP_TEST_DIR` and
`PSMODERN_REFERENCE_EXECUTABLE` configured (see `docs/development.md` and
`docs/dependencies.md`). On the maintainer's machine a complete run has no
skipped test: any skip makes the run incomplete. CI without ROMs does not
replace it. Avoid redundant complete runs; a later production change requires a
new run.

In Codex delegate the suite to one lightweight test runner (`gpt-6-luna`, low
effort, no inherited context); in Claude Code start the command in a background
shell and wait for its notification. Run `ctest --test-dir build-rel
--output-on-failure --no-tests=error` with UCRT64 on PATH and redirect output to
a file under `build-rel`. The runner reports the command, exit code, counts
(passed, failed and skipped), failed and skipped test names and output path; it
does not diagnose, edit or rerun on its own. Do not change the checkout or build
during the run. If a runner or background execution is unavailable, report that
validation is pending.

Investigate failures before rerunning. For test-only expectation fixes, rerun
failed tests and `ctest -L fast`; production fixes require the complete suite
again. Every test that needs a ROM also carries the label `rom`. Keep failure
evidence under the build directory. In the handoff state the runner or
background command, result, output path and how completion was awaited.
Additional runs explicitly requested by the maintainer are separate.
