# Session handoff

Last updated: 2026-11-21

[Milestone 16](milestone-16-plan.md) is complete and accepted. The Samsung
Galaxy S24 Ultra has `1.0.0-m16.4` installed. The matching APK is in the
phone's Downloads and locally in `../m16-playtest/`. Its SHA-256 is
`10d1cb3e0afeed18069f1c30d458d3001aa9db0f616df7ced29f42bf5dabc516`.

The maintainer passed the m16.2 play-test. Wireless ADB on the same phone
verified the final build, including ROM imports, save and automap loading,
speed, controller/touch switching, rotation and return from Home. The
player's original files were restored and all 13 SHA-256 hashes matched.
Device evidence and the Windows save round trip are in
`../m16-playtest/verification/`. The controller used for the automated
check was an Android CTS virtual gamepad.

Independent Tier A reviews approved the plan, implementation and final
Android activity fix. The Codex test runner awaited the original CTest
process through exit. With UCRT64 on PATH,
`ctest --test-dir build-rel -j 16 --output-on-failure --no-tests=error`
passed 390/390, zero skips, exit 0 (1472.02 sec). The log is
`build-rel/m16-final-launch-complete.log`.

Android changes are local on `main`. HEAD `247e218` is the desktop
fast-forward test fix. The possible cleanup in `Stage::~Stage` remains a
separate maintenance decision.
