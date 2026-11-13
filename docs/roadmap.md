# PSModern - Roadmap

**First approved by the maintainer on 2026-11-09; reorganized and re-decided
with the maintainer on 2026-11-13.** It orders the stages of
[design](design.md) section 8 and names what each needs; it starts nothing.
The maintainer starts each milestone explicitly, and each gets its own plan.
It is revised at each milestone closure.

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

Releases: 0.1.0 (start screen, 2026-11-11) and 0.2.0 (adds the automap,
2026-11-12). Releases do not change the milestone numbering.

## Next milestones

The numbers are the milestones'. The order was decided on 2026-11-13:
controller support moved ahead of text, as design section 8 allows.

8. **Full controller support.** Remapping and shortcuts. Touches only the
   input layer. Creates `docs/player-controls.md`. Two small decided items
   ride along:
   - **Encounters at 25%.** The choices become 0, 25, 50, 100 and 200, and
     the recommended pace becomes encounters 25 with experience 400. A
     study of the Switch release (kept outside this repository) found that
     its easy mode uses a quarter of the encounters, not half, with four
     times the experience and battle money. The design, the README, the
     development notes and the party chapter, which call 50 and 400 the
     Switch pace, are corrected in the same change.
   - **A floor label** on the automap screen.
9. **Text and language selection.** Rewrite the text system and read the US
   and Brazilian text from the player's ROM. Known so far: text uses a word
   dictionary and control codes; the Brazilian control codes change where
   conversations stop and their timing (design 5.5). Needs Brazilian
   recordings of text-only paths.
10. **World map and towns, then widescreen.** Map loading, scrolling and NPC
    activation, then the wider view (design 5.4).
11. **Menus and windows, then the new interface.**
12. **The rest.** Battle, the 3D dungeon drawing and the dungeon load, the
    sound driver, scenes, the opening and the ending, until no Z80 routine
    is left. Routes the recordings never reach (the ending, rare battle and
    sound paths) need new recordings here.
13. **Removing the Z80.** The game runs on C++ alone, with a per-frame time
    budget that keeps the original slowdowns (design 5.2).

## Longer-term direction

- **Easier modding.** Requested by the maintainer on 2026-11-11 as a
  direction, with no design yet and no milestone. A first step could be a
  patch format applied to the player's ROM at run time, so that mods ship no
  SEGA data; later, tables such as enemy stats or prices could move to
  editable files. Text changes become practical after milestone 9, and
  changes anywhere in the game once the Z80 is gone (milestone 13).

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
