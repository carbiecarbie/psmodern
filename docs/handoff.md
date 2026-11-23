# Session handoff

Last updated: 2026-11-23

Release 1.3.0 (milestone 17, the Japanese FM soundtrack) is published as
GitHub release v1.3.0 from `8158573`, with
`PSModern-1.3.0-windows-x64.zip` and the signed
`PSModern-1.3.0-android-arm.apk` (`versionCode 10300`, same signing key as
1.2.0). Packages, logs and release notes are in `../releases/`; signing
material stays in `../android-signing/`.

Milestone 17 is closed and accepted (`061ff49`). On 2026-11-23 the
maintainer chose the next subject, the television pixel shape (8:7), asked
for the plan only and approved its four decisions.
[Milestone 18 plan](milestone-18-plan.md) is committed (`912cae4`) and
pushed; no production code changed.

A contributor pull request proposes version 1.3.1, a universal macOS app
(Apple silicon and Intel, macOS 11+) with no gameplay change: the platform
layer `src/app/platform_macos.mm`, the app bundle and a static SDL2 in
`CMakeLists.txt`, `tools/package-macos.sh`, a macOS CI job, a portable
`launcher_tests` and strict UTF-8 checking of remembered ROM paths
(`rom_list.cpp`; Windows already refused invalid UTF-8). On macOS the
data-free suite passed 122/122 and the Brazilian ROM was played from the
packaged app; the ROM-dependent suite was not run there.

## Resume here

1. Milestone 18 waits for the maintainer's explicit start.
2. Its first task is the Options mockup with the eighth row; production code
   starts only after he approves it.
3. Review the macOS pull request. `rom_list.cpp` changed, so a complete
   Windows suite is due before 1.3.1 is published; its project-history entry
   is written at publication.
