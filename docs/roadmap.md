# PSModern - Roadmap

**First approved by the maintainer on 2026-11-09; reorganized and re-decided
with the maintainer on 2026-11-13; milestones 10 to 14 renumbered on
2026-11-14.** It orders the stages of
[design](design.md) section 8 and names what each needs; it starts nothing.
The maintainer starts each milestone explicitly, and each gets its own plan.
It is revised at each milestone closure, most recently that of milestone 17
(2026-11-22).

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

Releases: 0.1.0 (start screen, 2026-11-11), 0.2.0 (adds the automap,
2026-11-12), 0.3.0 (adds the changeable controls, 2026-11-13), 0.4.0
(adds language selection and shared saves, 2026-11-13), 0.5.0 (adds
widescreen, 2026-11-14), 0.6.0 (the world map and towns in C++,
2026-11-15) and 1.0.0 (complete C++ reimplementation with no Z80,
milestones 12 to 14, 2026-11-19), 1.1.0 (Windows audio recovery and the
Android version in the source, milestones 15 and 16, 2026-11-21) and 1.2.0
(first official signed Android APK alongside Windows x64 plus the documentation
refresh, 2026-11-21). The latest published release covers milestone 16 and
ships both Windows and Android packages; milestone 17 is not yet released.

## Next milestones

Milestones 1 through 17 are complete. The maintainer chooses and starts the
next milestone explicitly.

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

## Throughout

- Each rewritten routine adds its chapter and catalog entry; the
  [coverage census](coverage.md) shows how much runs in C++.
- New recordings are added when a milestone needs a route the existing ones
  miss, US first; Brazilian only for a Brazilian difference. Where no
  recording reaches a path, state probes cover it.
