# Session handoff

Last updated: 2026-11-21

Release 1.2.0 is published on GitHub (tag `v1.2.0` on `c292948`). It ships
both the Windows x64 zip and the first official signed Android APK (ARM64 and
ARMv7, `versionCode 10200`), along with the README/documentation overhaul
(`b3f59bc`) and `tools/package-android-release.ps1`:

- `PSModern-1.2.0-windows-x64.zip` SHA-256:
  `de520f035fc40582c0cf199fc13206c2295611345415989442caf07afc101cc2`
- `PSModern-1.2.0-android-arm.apk` SHA-256:
  `706f894bf1577b8eabe1b1a6c903fcea8bcd4abb2f801d3d4a0ce321508186ef`
- Android release certificate SHA-256:
  `30d98c6879947a5f9b45f80e892bea3c3eb56527ce85a1e715c588bdfd29edfe`

Both assets downloaded from GitHub matched their local hashes. Packages, logs
and notes live in `../releases/`. The Android release keystore and credentials
live outside Git in `../android-signing/` (must be kept backed up for in-place
updates); extracted `SDL2-2.32.10` lives in `../android-deps/SDL2-2.32.10`.

Under the release rule, the complete Windows suite was reused from `9736e2c`
(390/390); the Windows 1.2.0 build was warning-free and `ctest -L fast` passed
182/182.

Note kept from the 1.1.0 release checks: `launcher_unit` once failed on its own
right after `ctest -L fast` (then passed 330x in a row). If it recurs, capture
output and check the `steady_clock` suffix in `tests/launcher_tests.cpp`.

No milestone is active; the maintainer starts the next one explicitly.
