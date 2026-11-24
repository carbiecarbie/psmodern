# Session handoff

Last updated: 2026-11-23

## Release 1.3.1 (published)

Release 1.3.1 is published as GitHub release v1.3.1 from `68d8a33`, with
`PSModern-1.3.1-windows-x64.zip` and the signed
`PSModern-1.3.1-android-arm.apk` (`versionCode 10301`, same signing key as
1.3.0); both matched their checksums after download. It fixes the stop at
`01:5538` (fix `1840de2`): two targets of the interaction code table had no
segment since milestone 14, so releases 1.0.0 to 1.3.0 stopped at the
petrified companion of the first cave and at five other item places.
Packages, logs and release notes are in `../releases/`.

A contributor pull request (#2) adds a universal macOS app of 1.3.1
(Apple silicon and Intel, macOS 11+) with no gameplay change: the platform
layer `src/app/platform_macos.mm`, the app bundle and a static SDL2 in
`CMakeLists.txt`, `tools/package-macos.sh`, a macOS CI job, a portable
`launcher_tests` and strict UTF-8 checking of remembered ROM paths
(`rom_list.cpp`; Windows already refused invalid UTF-8). On macOS the
data-free suite passed and the Brazilian ROM was played from the packaged
app; the ROM-dependent suite was not run there.

Pending:

- Review the macOS pull request (#2): its branch now includes 1.3.1 and its
  ZIP is rebuilt from the merged revision.
- `../psmodern-re` has the two catalog rows and an allow rule for the table
  committed and pushed (`2437608`, all its checks passed). Its own handoff
  still says "not pushed"; correct that line in its next commit.
- No recording walks through the petrified companion's rescue, and nobody
  has played through it on 1.3.1; it needs a save near that place.

## Milestone 18

The maintainer started [milestone 18](milestone-18-plan.md). The bilingual
Options mockup is ready in `../m18-mockup/`. The `pt-options.png` and
`en-options.png` previews show the eighth row with square and TV pixels.
The corresponding `*-open.png` images show the list before scrolling.
The proposal keeps seven rows visible, adds a scroll hint on the border
and labels the new row PIXELS DE TV / TV PIXELS.

Next, get the maintainer's feedback on the layout and labels. Once approved,
start the sizing and stretch functions in the plan.
