# PSModern - Project History

One short entry per completed milestone. Details are in each condensed plan
and in Git.

## Milestone 1 - Foundation (2026-11-04 to 2026-11-08)

Phantasy Star runs from the player's ROM on a virtual Master System, in
both versions, with picture, sound, controls and saves. Twelve recordings
match a patched MesenCE reference on every frame. An independent review
led to fixes and to six maintainer decisions. The maintainer accepted the
milestone after playing the US version.
[Plan](milestone-1-plan.md). Last implementation commit: `4585634`; closed
by `408077f`.

## Milestone 2 - Verification (2026-11-09)

Controller polling is the first C++ replacement, with the same controls and
timing as the original. Per-call and frame comparisons pass all twelve
checkpoints; all 93,555 observed calls run in C++. Speed meets the limit,
independent Tier A reviews passed, and the maintainer's US play-test passed.
The Brazilian recordings pass automatically. The maintainer accepted the
milestone after playing the US version.
[Plan](milestone-2-plan.md). Last implementation commit: `70c3454`; closed
by `f6d0fc7`.

## Milestone 3 - Study base (2026-11-09)

A private, data-free disassembly rebuilds the player's ROM exactly, with a
catalog of routines and RAM fields that every public citation is checked
against. The encounter checks, the experience award and the sound driver
are found and explained in three new chapters. The design was corrected
from the study and the roadmap written. The maintainer accepted the
milestone after reading the chapters and the roadmap.
[Plan](milestone-3-plan.md). Last implementation commit: `24410ee`; closed
by `1d4c6a1`.

## Milestone 4 - First win (2026-11-10 to 2026-11-11)

The random-number routine, the random-encounter checks and the experience
award now run in C++, checked call by call against the original, on all
twelve recordings and by state probes over every input. Two options, off by
default, set the encounter rate (0, 50, 100 or 200%) and the experience
gained (100, 200 or 400%); with them off the game is identical to the
original. Speed meets the limit and three independent reviews passed. The
maintainer accepted the milestone on 2026-11-11 after playing the US version
with the launchers and finding everything correct. He chose fixed values for
the options instead of ranges, with encounters 50 and experience 400 as the
Switch-like setting.
[Plan](milestone-4-plan.md). Last implementation commit: `3609f6b`; closed by
`4066cb2`.

## Milestone 5 - Options inside the game (2026-11-11)

Options now open over the current exploration scene with continuous music,
retain encounter and experience/meseta rates beside the unchanged save, and
scale battle meseta with experience. Battle setup and money addition run in
verified C++. Independent reviews and all 107 configured tests passed with
no skips. After approving the functional test and playing the revised native
interface, the maintainer accepted closure and authorized publication.
[Plan](milestone-5-plan.md). Implementation: `d9ae7d7`; closed by `8e010dd`; private study: `8db0a92`.

## Milestone 6 - Fast-forward (2026-11-11)

Tab or the right shoulder toggles whole-game 1x/2x/4x speed, with a small
indicator and unchanged emulated timing. The initial delivery muted music and
effects while accelerated, following maintainer feedback. Independent reviews and the final 109-test suite
passed without skips. The maintainer played the revised build, approved it,
and authorized closure and publication. [Plan](milestone-6-plan.md).
Implementation and closure: `106ea66`.

The normal-tempo audio follow-up was accepted later on 2026-11-11. A separate
execution of the ROM sound driver keeps music and effects at normal tempo
through acceleration and the return to 1x, without changing gameplay state.
Independent reviews and all 111 configured tests passed without skips; the
maintainer reported that the isolated play-test worked perfectly.

## Release 0.1.0 - Start screen and first public release (2026-11-11)

Double-clicking `psmodern.exe` now opens the Windows Open dialog once, then
a start screen in the original window style that remembers the US and
Brazilian ROMs, opens the saves folder and starts the game in the same
window. The program became a GUI-subsystem executable with UTF-8 command-line
paths, and `tools/package-release.ps1` builds the Windows zip from a clean
export of the commit. An independent review's fixes were applied, and all 116
configured tests passed without skips. The maintainer play-tested the release
zip, found everything correct and authorized publication as GitHub release
v0.1.0. [Plan](release-0.1.0-plan.md).

## Milestone 7 - Dungeons and the automap (2026-11-11 to 2026-11-12)

Dungeon walking runs in C++ (eleven routines in 53 pieces, three new exit
kinds), checked call by call on fifteen recordings, three of them new, and
by state probes. An optional automap, opened with M or controller X, shows
the explored part of the current floor and is kept with each saved game;
tracking changes nothing in the game. Independent reviews passed and all
155 tests passed without skips. The maintainer played the US build, found
everything right and accepted it on 2026-11-12, confirming the map's rules.
[Plan](milestone-7-plan.md). Plan: `39f14a3`; implementation: `674c4fa`;
closed by `391f1f1`.

## Release 0.2.0 - The automap released (2026-11-12)

The second public Windows release carries milestone 7: dungeon walking in
C++ and the optional automap, off by default. The release notes gained the
map's key and option. All 155 tests passed without skips, and the published
zip is the package the maintainer play-tested. Published as GitHub release
v0.2.0. Prepared by `222fa76`.

## Milestone 8 - Full controller support (2026-11-13)

Every key and controller button can be changed on an in-game Controls
screen, reached from the start screen and from a new CONTROLS row of the
Options window, and kept in `controls.txt`; the triggers can be set like
buttons, and with no file the game plays exactly as release 0.2.0. Encounters
gained a 25% choice, now recommended with experience 400, and the automap
names each floor. An independent review's fixes were applied and all 159
tests passed without skips. The maintainer played the US build, found
everything right and accepted it on 2026-11-13. [Plan](milestone-8-plan.md).
Implementation: `a645dcd`; closed by `0b34074`.

## Release 0.3.0 - Changeable controls released (2026-11-13)

The third public Windows release carries milestone 8: the Controls screen
and `controls.txt`, triggers and controller shortcuts, encounters at 25%
and the automap's floor label. The player's README and the start screen
picture follow. All 159 tests passed without skips. The maintainer asked
to publish without a release play-test, having played the milestone 8
build. Published as GitHub release v0.3.0. Prepared by `488a43f`.

## Milestone 9 - Text and language selection (2026-11-13)

Text runs in C++ with both cartridges' timing, and English/Portuguese can
change during exploration without restarting. Localized host screens use
one PLAY entry and shared saves, options and automap; one-time legacy import
preserves both originals. Independent Tier A reviews approved the work;
178 tests passed without skips, and the ROM-free CI configuration passed.
The maintainer completed the isolated play-test and reported everything
correct on 2026-11-13. [Plan](milestone-9-plan.md). Implementation and
acceptance: `778aebf`; private text annotations: `1b98bc5` in `psmodern-re`.

## Release 0.4.0 - Language selection and shared saves (2026-11-13)

The Windows release carries milestone 9 and saves Options automatically on
exit. All 178 tests passed without skips. At the maintainer's request the
exact release zip was published without a manual release play-test. Published
as GitHub release v0.4.0 from `98d6a8f`.

## Milestone 10 - Widescreen on the world map, in towns and in the spaceship flight (2026-11-14)

An Options row, off by default, shows the world map, the towns and the
spaceship flight at 344 x 192 (16:9): the original 256 pixels in the centre
and 44 more per side drawn from the map with the cartridge's own tiles. It is
a read-only presentation layer (`WideView`, `MapScreens`) beside the original
picture; the machine is equal with it on or off, and settings gained format 3.
Independent reviews of the plan, the implementation and the spaceship addition
approved the work, and the complete suite passed 206 of 206 with no skips. The
maintainer play-tested the world map, towns, transfers, fades, battles and the
window, then the flight, and accepted it on 2026-11-14; vehicles were not
play-tested because no save reaches one. [Plan](milestone-10-plan.md).
Implementation: `581b4f3`; closed by `6101dd9`.

## Release 0.5.0 - Widescreen (2026-11-14)

The Windows release carries milestone 10: the widescreen option for the world
map, towns and spaceship flight. All 206 tests passed without skips and CI was
green. At the maintainer's request the exact release zip was published without
a manual release play-test. Published as GitHub release v0.5.0 from `e45597a`.

## Milestone 11 - World map and towns in C++ (2026-11-15)

Walking on the world map and in towns runs in C++ in both versions: the
picture's update, steps and camera, sprites and objects, the ground (vehicles
and ice), entrances, transfers, map loading and the road, as 153 short pieces
checked against the original on every call. The mechanism was unchanged; the
stretches with interrupts off, the frame wait where Options opens, the long
block copies, the palette fades and the spaceship scene stay on the Z80. Three
new US recordings (road, vehicles, ice) match the reference emulator on every
frame and exposed a widescreen defect, a broken-ice trail lost at a screen
change, now fixed. An independent implementation review approved the work with
no required changes, and the complete suite passed 237 of 237 with no skips.
The maintainer play-tested it in one US session, on foot, in the Land Rover,
the hovercraft and the Ice Digger, on the road and in a cave, noticed
nothing different from the original and accepted it on 2026-11-15.
[Plan](milestone-11-plan.md).
Implementation: `78254af`; closed by `f5cc348`; private annotations: `dd72681` in `psmodern-re`.

## Release 0.6.0 - World map and towns (2026-11-15)

The Windows release carries milestone 11: the world map and towns on C++ code, with no change to how the game looks or plays, and the widescreen broken-ice fix. At the maintainer's request the complete suite was not rerun for the release (it had passed 237 of 237 at milestone 11, and 0.6.0 changes only the version text); the build was warning-free and the 123 fast tests passed. The exact zip was published without a manual release play-test. Published as GitHub release v0.6.0 from `2b6fef6`.

## Milestone 12 - Menus and windows, then the new interface (2026-11-15)

The game's menus and windows run in C++ in both versions: windows, the cursor
and every choice, the field menu with its commands and chests, the hospital,
the church, the shops and the palette fades, as 323 short pieces checked
against the original on every call. The mechanism was unchanged; the interrupt
switches, the save copy, the points the program watches, the dispatchers and
frame waits stay on the Z80, and what items and spells do, the battle commands,
name entry and the title menus wait for milestone 13. Two new US recordings
(field menu; hospital, church and shops) match the reference emulator on every
frame. The Options screen was redone as three windows in the game's own style,
with Language as a live row; Controls and the start screen follow. An
independent implementation review approved the work with required changes (the
field menu's code rewritten into one function per command, the documentation of
what stays native), all applied, and the complete suite passed 266 of
266 with no skips. The maintainer play-tested the kit build in one US
session, noticed nothing different from the original, accepted the milestone on
2026-11-15 and decided that the play-test need not be repeated after the
review's fixes. [Plan](milestone-12-plan.md).
Implementation: `eacc0d2`; closed by `467e589`; private annotations: `1c5579b` in `psmodern-re`.

## Milestone 13 - The remaining game routines (2026-11-16)

Battles, items, spells, dungeon graphics, sound, story scenes and saved-game
menus now run in C++ on both cartridges. All 222,246 frames across 27 recordings
matched the reference. The maintainer played the US dungeon and Pause, the
Brazilian saved-name flow, and the US final battle and credits, and accepted
the milestone on 2026-11-16. The [plan](milestone-13-plan.md) identifies the
accepted build and records the test results.
Implementation: `762a48d`; closed by `a278d54`; private annotations: `c93b373` in `psmodern-re`.

## Milestone 14 - Removing the Z80 (2026-11-16 to 2026-11-19)

The game runs on C++ alone: the program contains no Z80 interpreter, and the
Z80 stays only in the tests as the referee they compare against. Interrupts
are taken in C++ at the exact instruction, so timing and slowdowns are kept.
All 27 recordings match the reference and the original instruction by
instruction; the complete suite passed 389 tests in 23 minutes 36 seconds. The
program is about 15% slower per frame and its executable larger, a cost the
maintainer accepted. He played both cartridges and accepted the milestone on
2026-11-19. [Plan](milestone-14-plan.md).
Implementation: `4aeec95`; design and agent-instruction amendments in the
closing commit `2f9d4e5`.

## Release 1.0.0 - Complete C++ reimplementation (2026-11-19)

The Windows release brings milestones 12, 13 and 14 together: menus and windows
in C++ with the redesigned Options, Controls and start screens, all remaining
game routines (battles, items, spells, dungeon drawing, sound, story scenes,
saves and the ending), and the complete removal of the Z80 interpreter from
`psmodern.exe`. At the maintainer's request the complete suite was not rerun
for the release (it had passed 389 of 389 at milestone 14, and 1.0.0 changes
only the version strings, READMEs and start-screen picture); the build was
warning-free and all 181 fast tests passed. The exact zip was published without
a manual release play-test. Published as GitHub release v1.0.0 from `60d620d`.

## Milestone 15 - Original bug fixes and sound stalls (2026-11-19)

The Windows host preserves queued audio and resumes with game samples after an
underrun. A 30-minute WASAPI session recorded zero underruns and resets. A
prototype also fixed five cartridge defects and passed all 394 tests. After
playing it successfully, the maintainer chose to keep the original game rules
and six-row Options menu. The [plan](milestone-15-plan.md) records each fix,
its verification and the recovery patch. The audio-only revision passed
all 389 tests with zero skips.

## Milestone 16 - Android version (2026-11-21)

The [personal Android build](milestone-16-plan.md) brought the C++ game to ARM
phones with cartridge-style touch controls, system ROM import and saves that
travel between Android and Windows. The maintainer passed the Samsung play-test.
On the final `1.0.0-m16.4` APK, the S24 continued a Windows save with its
explored dungeon map, sustained 1x/2x/4x and resumed the same game from Home.
Independent Tier A reviews approved the port. The Windows suite passed all 390
tests with zero skips. Implementation: `20e317a`; the desktop fast-forward
test fix is `247e218`.

## Release 1.1.0 - Audio recovery and the Android source (2026-11-21)

The Windows release brings milestones 15 and 16 together: sound that recovers
from Windows audio stalls without added silence, and the safer cleanup of a
failed save import (`9736e2c`). The Android version is in the source; no APK
is published. Under the release rule recorded that day, the complete suite was
not rerun: it had passed 390 of 390 on `9736e2c`, and the release changed only
the version, documents and the start-screen picture. The build was
warning-free and all 182 fast tests passed. The exact zip was published
without a manual release play-test. Published as GitHub release v1.1.0 from
`5afed78`.

## Release 1.2.0 - Official Android APK and documentation refresh (2026-11-21)

The release publishes the first official signed Android APK
(`PSModern-1.2.0-android-arm.apk`, ARM64 and ARMv7, `versionCode 10200`)
alongside `PSModern-1.2.0-windows-x64.zip`, plus the rewritten READMEs and
documentation accuracy pass (`b3f59bc`). `android/build.gradle` now syncs
`versionName`/`versionCode` from `CMakeLists.txt`, configures release signing
from external credentials and bundles GPL-3.0, SDL2 and NDK LLVM
`libc++_shared` notices with commit metadata; `tools/package-android-release.ps1`
automates clean `git archive` packaging with `apksigner`, 16 KiB `zipalign`,
`aapt2` and per-ABI Build ID / zero-Z80/oracle verification. Under the release
rule, the complete Windows suite was reused from `9736e2c` (390/390); the
Windows 1.2.0 build was warning-free and all 182 fast tests passed. Both
packages were verified and published without a separate manual release
play-test. Published as GitHub release v1.2.0 from `c292948`.

## Milestone 17 - Japanese FM soundtrack (2026-11-22)

A player who also owns the Japanese cartridge can now hear the
[Japanese release's FM music and effects](milestone-17-plan.md) on Windows
and Android, chosen on a new SOUND row of Options (PSG by default). A private
engine runs the Japanese sound driver, rewritten as 191 C++ pieces checked
against the original on the Z80, and the vendored Nuked-OPLL chip; the game,
its timing and saves are unchanged. Independent reviews approved the plan,
the backend and the Options row. The maintainer passed the play-test on
Windows and the Galaxy S24. The Windows suite passed all 430 tests with zero
skips. Implementation and closure: `061ff49`.

## Release 1.3.0 - Japanese FM soundtrack (2026-11-22)

The release brings milestone 17 to players: with their own Japanese
cartridge added as a sound source, the SOUND row of Options switches between
PSG and the Japanese release's FM music and effects, on Windows and Android.
The READMEs, the release `README.txt` and both README pictures (the start
screen and the seven-row Options) were updated. Under the release rule the
complete suite was reused from milestone 17 (430/430 on the tree committed
as `061ff49`); the 1.3.0 build was warning-free and all 188 fast tests
passed. Both packages were verified (the zip ran with `PATH` limited to
System32 from a non-ASCII folder; the APK passed its signature, alignment,
notice and zero-Z80 checks, with the 1.2.0 signing key) and published
without a separate manual release play-test, the milestone play-test having
covered the same sources. Published as GitHub release v1.3.0 from `8158573`.

## Release 1.3.1 - Fix of the stop at the petrified companion (2026-11-23)

A player reported the game stopping at `01:5538`. Two one-instruction targets
of the interaction code table, `01:5535` and `01:5538`, had had no C++
version since milestone 14, so every release from 1.0.0 to 1.3.0 stopped at
the petrified companion of the first cave, a mandatory point of the story,
and at five other places where an item is used; saves were never affected.
The fix (`1840de2`) registers both and turns the static walk of the registry
into the test `rom_<version>_static_walk`, which fails with exactly those
table entries when the fix is removed; the joins tests also run the six
event routes against the original. The complete suite passed all 432 tests
with zero skips on the fix and was reused under the release rule; the 1.3.1
build was warning-free and all 190 fast tests passed. Both packages were
verified (the zip ran with `PATH` limited to System32 from a non-ASCII
folder; the APK passed its signature, alignment, notice and zero-Z80 checks,
with the 1.3.0 signing key) and matched their checksums after download.
Published without a manual release play-test, and without anyone playing
through the rescue: no recording reaches it yet. Published as GitHub release
v1.3.1 from `68d8a33`; the README start-screen picture was retaken with the
1.3.1 footer after the release.

## Milestone 19 - Playthrough to the ending (2026-11-25)

The tests now [play each cartridge](milestone-19-plan.md) from a new game to
the title after the credits, one route each: 25 US links (673,967 frames) and
27 Brazilian links (685,128 frames), continued from in-game saves with only
strength and money edited, through the rescue of the petrified companion and
both last bosses. 89 shorter recordings reach shops, healing places and rare
branches. No stop was found and no game code changed. Segments reached by no
recording fell from 1,200 to 362 and those reached by US recordings only from
587 to 64. A fourth MesenCE patch fixed a flag difference in the reference.
The independent review approved with eight required changes, all made. The
complete suite passed 438/438 with zero skips; the closure-only `playthrough`
group passed 378 of 378 with no skip in 2,409.73 s. Implementation:
`88aa2cc`; review changes: `d8cc530`.
