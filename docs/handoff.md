# Session handoff

Last updated: 2026-11-21

Release 1.1.0 is published on GitHub (tag `v1.1.0` on `5afed78`). It covers
milestones 15 and 16 and the shared-save staging cleanup fix `9736e2c`.
`PSModern-1.1.0-windows-x64.zip` has SHA-256
`f0d396fc28c7218e25a9db8df062ac9289a75930bbe07a1d596151a9dfce1c2f`. The
file downloaded from GitHub has the same hash. The zip and its notes are in
`../releases/`. No APK was published; the Android version remains a personal
build.

New rule (in `tools/AGENTS.md` and the release steps in
`docs/development.md`): a release reuses the last complete suite run that
passed when only the version, docs or README images changed since then;
the build and `ctest -L fast` suffice. That applied to 1.1.0 (complete run
390/390 on `9736e2c`; fast 182/182 on the release commit).

Note: once during the release checks, `launcher_unit` failed when run on its
own (`ctest -R "launcher|version"`) right after the fast tests. Its message
was not captured. It then passed 330 times on its own and in two more fast
runs. If it fails again, keep the output and look at the `Temporary` folder
naming in `tests/launcher_tests.cpp`, which uses `steady_clock` as its
unique suffix.

No milestone is active; the maintainer starts the next one explicitly.
