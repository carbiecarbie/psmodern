# PSModern - Roadmap

**First approved by the maintainer on 2026-11-09; revised at the closure of
milestone 5 (2026-11-11).** Written from the milestone 3 study base.
It orders the stages of [design](design.md) section 8 and names what each
needs; it starts nothing. The maintainer starts each milestone explicitly,
and each gets its own plan.

## Where the project stands

The whole game runs on the virtual Master System. Controller polling,
random numbers, encounter checks, experience, battle setup and money addition
run in C++, verified against the original. In-game options retain encounter
and experience/meseta rates beside the unchanged cartridge save. The native
style window preserves the exploration scene and continuous music.
[Milestone 5](milestone-5-plan.md) was accepted on 2026-11-11.

## Later milestones

Each line is the stage's goal, then what the study base suggests about it.
The numbers are the milestones'. From milestone 6 on they are one above the
stage numbers of [design](design.md) section 8, because milestone 5 was added
at the closure of milestone 4.

6. **Dungeons and the automap.** Rewrite dungeon state and movement, then
   a minimap and automap. Known so far: community leads for position,
   direction and the dungeon layout in RAM; the dungeon encounter check
   reads a nearby cell. Needs its own study part first.
7. **Text and language selection.** Rewrite the text system and read the US
   and Brazilian text from the player's ROM. Known so far: text uses a word
   dictionary and control codes; the Brazilian control codes change where
   conversations stop and their timing (design 5.5). Needs Brazilian
   recordings of text-only paths.
8. **Full controller support.** Remapping and shortcuts. Touches only the
   input layer, so it may move earlier whenever the maintainer wants.
9. **World map and towns, then widescreen.** Map loading, scrolling and NPC
   activation, then the wider view (design 5.4).
10. **Menus and windows, then the new interface.**
11. **The rest.** Battle, the 3D dungeon drawing, the sound driver, scenes,
    the opening and the ending, until no Z80 routine is left. Routes the
    six recordings never reach (the ending, rare battle and sound paths)
    need new recordings here.
12. **Removing the Z80.** The game runs on C++ alone, with a per-frame time
    budget that keeps the original slowdowns (design 5.2).

## Possible later options

Raised by milestone 4. Each is for the maintainer to place in a milestone,
change or drop; none is ordered or scheduled here. The money multiplier and
the in-game settings are delivered in milestone 5.

- **Several levels per award.** Declined by the maintainer at the closure of
  milestone 4 (one level per fight stays).
- **Walking speed.** The Switch easy mode has it; the movement code has not
  been studied.

## Throughout

- Each rewritten routine adds its chapter and catalog entry; the
  [coverage census](coverage.md) shows how much runs in C++.
- New recordings are added when a milestone needs a route the existing ones
  miss, US first; Brazilian only for a Brazilian difference. The six US
  recordings reach few encounter paths and one level gain, which state
  probes covered in milestone 4.
- This roadmap is revised at each milestone closure.
