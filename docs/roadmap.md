# PSModern - Roadmap

**Approved by the maintainer on 2026-11-09.** Written from the milestone 3
study base. It orders the stages of [design](design.md)
section 8 and names what each needs; it starts nothing. The maintainer
starts each milestone explicitly, and each gets its own plan.

## Where the project stands

The whole game runs on the virtual Master System; controller polling is the
first routine rewritten in C++ and verified call by call. The private study
base has a data-free disassembly that rebuilds the ROM, a catalog of
routines and RAM fields, and chapters on
[random numbers and encounters](how-it-works/random-numbers.md),
[the party's records](how-it-works/party.md) and
[the sound driver](how-it-works/sound.md).

## Milestone 4 - First win: encounters and experience

Rewrite the encounter checks and the experience award in C++, verified
against the original, then add the first two options, off by default:
an encounter rate (including off) and an experience multiplier.

What the study base already gives:

- **ROM static.** The map check (`01:5F63`) and the dungeon check
  (`00:0F3C`) each compare one draw with a threshold, and later stages pick
  the monsters and their number. The award (`00:170D`, `00:1754`) gives the
  whole reward to every eligible character and raises at most one level.

What its plan must decide:

- **The random-number routine.** Both checks call it. A C++ routine cannot
  call original Z80 code yet, so the plan either rewrites the random-number
  routine too (Tier A, it drives every draw) or extends the mechanism.
- **Options on calls that fall back.** A call that runs on the Z80 because
  an interrupt could fall inside it would skip an option built into the C++
  routine. The plan must make an option apply on every call, not only on
  most.
- **How options are set** before a new interface exists (for example a
  settings file), and their defaults and ranges (design section 9).
- **The feel of the options** (maintainer decisions): whether turning
  encounters off avoids the draw, which changes later random results;
  whether a large experience award may raise several levels at once.
- **Recordings.** The six routes reach one level gain and few encounter
  paths; the plan names any new US recordings it needs.

## Later milestones

Each line is the stage's goal, then what the study base suggests about it.

5. **Dungeons and the automap.** Rewrite dungeon state and movement, then
   a minimap and automap. Known so far: community leads for position,
   direction and the dungeon layout in RAM; the dungeon encounter check
   reads a nearby cell. Needs its own study part first.
6. **Text and language selection.** Rewrite the text system and read the US
   and Brazilian text from the player's ROM. Known so far: text uses a word
   dictionary and control codes; the Brazilian control codes change where
   conversations stop and their timing (design 5.5). Needs Brazilian
   recordings of text-only paths.
7. **Full controller support.** Remapping and shortcuts. Touches only the
   input layer, so it may move earlier whenever the maintainer wants.
8. **World map and towns, then widescreen.** Map loading, scrolling and NPC
   activation, then the wider view (design 5.4).
9. **Menus and windows, then the new interface.**
10. **The rest.** Battle, the 3D dungeon drawing, the sound driver, scenes,
    the opening and the ending, until no Z80 routine is left. Routes the
    six recordings never reach (the ending, rare battle and sound paths)
    need new recordings here.
11. **Removing the Z80.** The game runs on C++ alone, with a per-frame time
    budget that keeps the original slowdowns (design 5.2).

## Throughout

- Each rewritten routine adds its chapter and catalog entry; the
  [coverage census](coverage.md) shows how much runs in C++.
- New recordings are added when a milestone needs a route the existing ones
  miss, US first; Brazilian only for a Brazilian difference.
- This roadmap is revised at each milestone closure.
