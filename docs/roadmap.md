# PSModern - Roadmap

**First approved by the maintainer on 2026-11-09; reorganized and re-decided
with the maintainer on 2026-11-13; milestones 10 to 14 renumbered on
2026-11-14.** It orders the stages of
[design](design.md) section 8 and names what each needs; it starts nothing.
The maintainer starts each milestone explicitly, and each gets its own plan.
It is revised at each milestone closure, most recently that of milestone 11
(2026-11-15).

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

Releases: 0.1.0 (start screen, 2026-11-11), 0.2.0 (adds the automap,
2026-11-12), 0.3.0 (adds the changeable controls, 2026-11-13), 0.4.0
(adds language selection and shared saves, 2026-11-13), 0.5.0 (adds
widescreen, 2026-11-14) and 0.6.0 (the world map and towns in C++,
2026-11-15). Releases do not change the milestone numbering.

## Next milestones

The numbers are the milestones'. The order was decided on 2026-11-13:
controller support moved ahead of text, as design section 8 allows. On
2026-11-14 the maintainer split the old milestone 10: widescreen only reads
state the original keeps, so it came first and the map rewrite followed
(design 5.3). Milestone 11 is complete (2026-11-15); milestone 12 is
next and still needs explicit authorization.

12. **Menus and windows, then the new interface.** The maintainer wants the
    Options menu redone here (Maintainer decision, 2026-11-14, after the
    milestone 10 play-test). The palette fades, which every screen change
    uses, and the tile animators that are not the map's stay on the Z80 after
    milestone 11 and are left for milestones 12 and 13 (Maintainer decision,
    2026-11-15); the plan of milestone 12 says which of them it takes.
13. **The rest.** Battle (with the battle object types the map shares the
    object engine with), the 3D dungeon drawing and the dungeon load, the
    sound driver, the spaceship scene, scenes, the opening and the ending, the
    tile animators that are not the map's and the code the map left native
    (the stretches with interrupts off, the frame wait where Options opens,
    the long block copies), until no Z80 routine is left. Routes the
    recordings never reach (the ending, rare battle and sound paths) need new
    recordings here.
14. **Removing the Z80.** The game runs on C++ alone, with a per-frame time
    budget that keeps the original slowdowns (design 5.2).

## Longer-term direction

- **Easier modding.** Requested by the maintainer on 2026-11-11 as a
  direction, with no design yet and no milestone. A first step could be a
  patch format applied to the player's ROM at run time, so that mods ship no
  SEGA data; later, tables such as enemy stats or prices could move to
  editable files. Text changes become practical after milestone 9, and
  changes anywhere in the game once the Z80 is gone (milestone 14).

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
