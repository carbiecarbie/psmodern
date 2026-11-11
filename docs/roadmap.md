# PSModern - Roadmap

**First approved by the maintainer on 2026-11-09; revised at the closure of
milestone 4 (2026-11-11).** Written from the milestone 3 study base.
It orders the stages of [design](design.md) section 8 and names what each
needs; it starts nothing. The maintainer starts each milestone explicitly,
and each gets its own plan.

## Where the project stands

The whole game runs on the virtual Master System. Controller polling, the
random-number routine, the random-encounter checks and the experience award
are rewritten in C++ and verified call by call ([milestone 4](milestone-4-plan.md)).
The first two options exist, off by default: an encounter rate (0, 50, 100 or
200%) and an experience rate (100, 200 or 400%), set on the command line.

The mechanism can now cut a routine into pieces at its calls into original
code, so later routines that wait for frames, show text or start battles
can be rewritten the same way. The private study base has a data-free
disassembly that rebuilds the ROM, a catalog of routines and RAM fields, and
chapters on [random numbers and encounters](how-it-works/random-numbers.md),
[the party's records](how-it-works/party.md) and
[the sound driver](how-it-works/sound.md).

## Later milestones

Each line is the stage's goal, then what the study base suggests about it.
The numbers are the milestones'. From milestone 6 on they are one above the
stage numbers of [design](design.md) section 8, because milestone 5 was added
at the closure of milestone 4.

5. **Options inside the game.** Maintainer request (2026-11-11). Choose the
   encounter and experience settings inside the game, showing the default
   (100 for both) and the Switch-like recommendation (encounters 50,
   experience 400); save the settings with the game without changing the
   original save format ([design](design.md) section 9); and make the money
   from battles follow the experience rate. The last needs the money award
   rewritten; static leads: the battle reward production is in the setup at
   `01:5FFE` and the award is separate. Its plan must decide:
   - where the choice appears (a menu, the title or the pause screen) and how
     it is drawn with no new art;
   - where the settings are stored, since the original save format must not
     change;
   - what happens to a save used on the original cartridge format, and to a
     save made with the options on when it is loaded without them;
   - the money rule and its ceiling (rounding, a minimum, and the largest
     amount the money counter can hold).
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
the in-game settings are now milestone 5.

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
