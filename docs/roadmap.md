# PSModern - Roadmap

**First approved by the maintainer on 2026-11-09; revised at the closure of
milestone 7 (2026-11-12).** Written from the milestone 3 study base.
It orders the stages of [design](design.md) section 8 and names what each
needs; it starts nothing. The maintainer starts each milestone explicitly,
and each gets its own plan.

## Where the project stands

The whole game runs on the virtual Master System. Controller polling,
random numbers, encounter checks, experience, battle setup, money addition
and walking in the dungeons run in C++, verified against the original. In-game
options retain encounter and experience/meseta rates and the automap choice
beside the unchanged cartridge save. The native style window preserves the
exploration scene and continuous music. The optional automap, opened with M
or controller X, shows the explored part of the current dungeon floor and is
kept with each saved game. Session-only fast-forward cycles 1x/2x/4x with an
indicator, and music and effects keep normal tempo while accelerated.
[Milestone 7](milestone-7-plan.md) was accepted on 2026-11-12. The 3D
dungeon drawing and the dungeon load remain on the Z80.

**Releases.** Version 0.1.0, the first public Windows release, with a start
screen that remembers the player's ROMs, was accepted on 2026-11-11; see its
[plan](release-0.1.0-plan.md). It does not change the milestone numbering.

## Later milestones

Each line is the stage's goal, then what the study base suggests about it.
The numbers are the milestones'. Milestones 5 and 6 were added to the
stages of [design](design.md) section 8.

8. **Text and language selection.** Rewrite the text system and read the US
   and Brazilian text from the player's ROM. Known so far: text uses a word
   dictionary and control codes; the Brazilian control codes change where
   conversations stop and their timing (design 5.5). Needs Brazilian
   recordings of text-only paths.
9. **Full controller support.** Remapping and shortcuts. Touches only the
   input layer, so it may move earlier whenever the maintainer wants.
10. **World map and towns, then widescreen.** Map loading, scrolling and NPC
   activation, then the wider view (design 5.4).
11. **Menus and windows, then the new interface.**
12. **The rest.** Battle, the 3D dungeon drawing, the sound driver, scenes,
    the opening and the ending, until no Z80 routine is left. Routes the
    six recordings never reach (the ending, rare battle and sound paths)
    need new recordings here.
13. **Removing the Z80.** The game runs on C++ alone, with a per-frame time
    budget that keeps the original slowdowns (design 5.2).

## Possible later options

Raised during milestones 4, 6 and 7, and after release 0.1.0. Each is for the
maintainer to place in a milestone, change or drop; none is ordered or
scheduled here. The money multiplier and the in-game settings are delivered
in milestone 5.

- **Several levels per award.** Declined by the maintainer at the closure of
  milestone 4 (one level per fight stays).
- **Walking speed.** The Switch easy mode has it. Dungeon steps now run in
  C++ (milestone 7), but their animations and the world-map and town
  movement are still on the Z80 and how a faster walk would fit is not
  studied.
- **Easier modding.** Requested by the maintainer on 2026-11-11 as a
  direction, with no design yet. Rewritten C++ routines and the how-it-works
  chapters already make the game easier to study and change. A first step
  could be a patch format applied to the player's ROM at run time, so that
  mods ship no SEGA data; later, tables such as enemy stats or prices could
  move to editable files. Text changes become practical after milestone 8,
  and changes anywhere in the game once the Z80 is gone (milestone 13).

Raised in milestone 7; none is decided:

- **Browsing other floors on the map.** The map shows the current floor
  only; the explored floors of the game are already kept.
- **A floor label** on the map screen. Left out of milestone 7.
- **A marker for defeated bosses.** Declined by the maintainer for now;
  bosses and guardians are never marked.
- **Keeping an unusable map file under its own name,** so that it outlasts
  the next session's first save instead of living in `.automap.bak`.

The Switch release's encounter rate was studied separately, outside this
repository; nothing from that study is decided or recorded here.

## Throughout

- Each rewritten routine adds its chapter and catalog entry; the
  [coverage census](coverage.md) shows how much runs in C++.
- New recordings are added when a milestone needs a route the existing ones
  miss, US first; Brazilian only for a Brazilian difference. The six US
  recordings reach few encounter paths and one level gain, which state
  probes covered in milestone 4; milestone 7 added three US dungeon
  recordings, and state probes alone cover the dungeon paths none reach.
- This roadmap is revised at each milestone closure.
