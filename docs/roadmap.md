# PSModern - Roadmap

**First approved by the maintainer on 2026-11-09; reorganized and re-decided
with the maintainer on 2026-11-13; milestones 10 to 14 renumbered on
2026-11-14.** It orders the stages of
[design](design.md) section 8 and names what each needs; it starts nothing.
The maintainer starts each milestone explicitly, and each gets its own plan.
It is revised at each milestone closure, most recently that of milestone 19
(2026-11-25).

## Done so far

[Project status](project-status.md) describes what runs now and
[project history](project-history.md) lists each closed milestone.

| Milestone | Delivered |
| --- | --- |
| 1 to 3 | The virtual Master System, the lockstep verification and the study base |
| 4 | Encounter checks, experience and random numbers in C++; encounter and experience rates |
| 5 | In-game options saved beside the unchanged cartridge save; battle meseta follows the experience rate |
| 6 | Fast-forward (1x/2x/4x) with music at normal tempo |
| 7 | Dungeon walking in C++ and the automap |
| 8 | Full controller support: the Controls screen and `controls.txt`; encounters at 25% and the floor label |
| 9 | Text in C++, live English/Portuguese switching, localized host screens and shared saves with preserved legacy progress |
| 10 | Widescreen (344 x 192) on the world map, in towns and in the spaceship flight, as an option; a read-only view, nothing rewritten |
| 11 | Walking on the world map and in towns in C++ (153 pieces: the picture's update, steps and camera, sprites and objects, the ground with vehicles and ice, entrances, transfers, map loading and the road); three new US recordings; a widescreen fix for broken ice |
| 12 | Menus and windows in C++ (323 pieces: the window engine and printers, the windows, choosing with the cursor, the field menu, the hospital, church and shops, and the palette fades); two new US recordings; the Options screen redone as three game-style windows with Language as a live row, and Controls and the start screen on the same windows |
| [13](milestone-13-plan.md) | Remaining game routines in C++, including battles, items, dungeon drawing, sound, story scenes, saves and the ending, verified on both cartridges |
| [14](milestone-14-plan.md) | Removing the Z80 from the program: the game runs on C++ alone with exact instruction timing and interrupts; the Z80 remains only in the tests as the referee |
| [15](milestone-15-plan.md) | Recovery from Windows audio stalls without added silence. Five cartridge fixes were tested and withdrawn by maintainer decision |
| [16](milestone-16-plan.md) | Personal Android APK with touch controls, cartridge import and saves shared with Windows |
| [17](milestone-17-plan.md) | The Japanese cartridge's FM music and effects as an option, on Windows and Android, from the player's Japanese ROM |
| [19](milestone-19-plan.md) | A recorded playthrough to the ending on each cartridge, checked against the original and the reference; no stop found, nothing playable changed, no release needed. Milestone 18 was withdrawn |

Releases: 0.1.0 (start screen, 2026-11-11), 0.2.0 (adds the automap,
2026-11-12), 0.3.0 (adds the changeable controls, 2026-11-13), 0.4.0
(adds language selection and shared saves, 2026-11-13), 0.5.0 (adds
widescreen, 2026-11-14), 0.6.0 (the world map and towns in C++,
2026-11-15) and 1.0.0 (complete C++ reimplementation with no Z80,
milestones 12 to 14, 2026-11-19), 1.1.0 (Windows audio recovery and the
Android version in the source, milestones 15 and 16, 2026-11-21), 1.2.0
(first official signed Android APK alongside Windows x64 plus the documentation
refresh, 2026-11-21), 1.3.0 (the Japanese FM soundtrack, milestone 17,
2026-11-22) and 1.3.1 (the fix of the stop at the petrified companion,
2026-11-23). The latest published release covers milestone 17 and ships both
Windows and Android packages.

## Next milestones

Milestones 1 through 17 and 19 are complete; 18 was withdrawn. The
maintainer chooses and starts the next milestone explicitly.

Candidates left by milestone 19, none decided and none needing a release:

- **Narrowing the reasons.** 102 of the 426 reasons for segments no recording
  reaches, or only US recordings reach, say the trigger is unknown; a later
  study could settle some by a run or a reading of the code.
- **The reference capture's intermittent failure.** It failed twice inside
  `tools/reference.lua` under heavy load and passed when repeated.
- **Trimming [project status](project-status.md)** back to its size target;
  the maintainer chose on 2026-11-25 to update it now and trim it later.

## Longer-term direction

- A native Linux build stays deferred. The Windows release ran under Wine.

- **Easier modding.** Requested by the maintainer on 2026-11-11 as a
  direction, with no design yet and no milestone. A first step could be a
  patch format applied to the player's ROM at run time, so that mods ship no
  SEGA data; later, tables such as enemy stats or prices could move to
  editable files. Text changes become practical after milestone 9, and
  changes anywhere in the game now that the Z80 is gone (milestone 14).
- **A new look for the game's own menus.** Optional and undecided
  (Maintainer decision, 2026-11-15): the field menu, the shops and the other
  windows of the game keep the cartridge's look, and the maintainer may later
  decide to offer a new one as an option. It has no design and no milestone,
  and is not part of the current order; a design session with a mockup would
  come first.

## Decided against

Recorded so that they are not raised again without a new reason.

- **Several levels per award** (2026-11-11): one level per fight stays.
- **Walking speed** (2026-11-13): fast-forward covers it.
- **A marker for defeated bosses** (2026-11-12): bosses and guardians are
  never marked on the automap.
- **Browsing other floors on the automap** (2026-11-13): the map shows the
  current floor only.
- **Keeping an unusable map file under its own name** (2026-11-13): the
  `.automap.bak` behaviour stays.
- **Television pixel shape** (2026-11-24): the picture keeps square pixels.
  [Milestone 18](milestone-18-plan.md) was withdrawn before any code: an
  eighth Options row needs a scrolling list, and the 8/7 stretch blends
  columns at 1920 x 1080 and takes columns from Widescreen.

## Throughout

- Each rewritten routine adds its chapter and catalog entry; the
  [coverage census](coverage.md) shows how much runs in C++.
- New recordings are added when a milestone needs a route the existing ones
  miss, US first; Brazilian only for a Brazilian difference. Where no
  recording reaches a path, state probes cover it.
