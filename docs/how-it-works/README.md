# How Phantasy Star Works Inside

These chapters explain how **Phantasy Star** for the Sega Master System works
under the hood, written in plain language for readers who do not program.

Every claim carries a bold evidence label defined in
[the documentation rules](../AGENTS.md#evidence-labels). Original code is cited
by `bank:address` of the US Rev-A cartridge (and the Brazilian address where it
differs); no ROM data or assembly listings are included here.

Since milestone 14, `psmodern.exe` runs pure C++ with no Z80 interpreter. Some
older chapters still describe what stayed on the Z80 at the milestone they were
written for, noted by their **Since milestone 14** banner.

## Console

- [The Master System](master-system.md) — the console hardware (Z80, VDP, PSG,
  mapper, save chip), one frame from start to end, and why exact timing matters
  for this game.
- [Interrupts, reset and frame waits](interrupts.md) — the 60 Hz frame
  interrupt, the Pause button (NMI), console Reset, and VDP status waits.

## Game systems

- [Reading the controller](controls.md) — held vs newly pressed buttons and
  exact read timing.
- [Random numbers and encounters](random-numbers.md) — the RNG state, the Z80
  `R` counter, world-map and dungeon encounter checks, and the encounter-rate
  option.
- [The party's records](party.md) — character stats, inventory, money, post-battle
  experience/meseta rewards, and the experience-rate option.
- [The sound driver](sound.md) — bank `0C`, sound requests, voices, envelopes,
  and normal-tempo audio during fast-forward.
- [Walking in the dungeons](dungeons.md) — 3D dungeon layouts, movement, doors,
  chests, pitfalls, exits, and how the automap reads them.
- [Text](text.md) — compressed text streams, word dictionary, control codes,
  tile printing, and live English/Portuguese language switching.
- [The world map and the towns](world-map.md) — scrolling, metatiles, objects,
  vehicles, ice fields, entrances, and how 16:9 widescreen reads the map.
- [Menus and windows](menus.md) — the window engine, cursors, field menu,
  hospitals, churches, shops, and palette fades.
- [Battles, magic and items](battle.md) — battle flow, spells, items, turn
  execution, and effects.
- [Town and dungeon interactions](interactions.md) — NPC dialogues, doors,
  story events, and purchases.
- [Title, saved games and story scenes](scenes.md) — title and Continue screens,
  opening sequence, spaceship flights, story scenes, and the ending.
