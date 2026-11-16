# Milestone 13 plan - The remaining game routines

**Tier A. Status: completed and accepted (2026-11-16).** The maintainer
played and approved all three scenarios in the fixed Windows build
(SHA-256 `3f7afe9f4fd3c11b62ac261463ae8c519d580ac023ac46cd415bd41e050661fe`).
Thirteenth milestone of the [roadmap](roadmap.md).

## Scope and decisions

The remaining game routines were rewritten in C++ for the US Rev-A and
Brazilian cartridges. Battles now include command selection, enemy actions,
items, magic, damage, rewards, escape and defeat. Field items, dungeon loads,
the first-person view and its animations, the sound driver, story events,
the title and save menus, space travel and the ending also use C++.

Independent reviews covered the plan, implementation and timing contracts.
Original execution remains selectable for comparison. When an interrupt or
frame boundary splits a copy, the next part resumes with the original
pointers, remaining count and write timing. The Z80 still executes the
documented instruction boundaries and handles calls that cannot run in C++
at that instant. Removing it is the scope of milestone 14.

## Maintainer play-test

On 2026-11-16 the maintainer approved dungeon movement, pause and sound on
the US cartridge, entering and loading a saved name on the Brazilian
cartridge, and the US Darkfalz battle through the credits.

## Integration evidence

Seven recordings joined the earlier twenty: six US (ending, party defeat,
flute, Pause while walking, Pause across frame phases, Pause during play) and
one Brazilian (entering a saved name). The defeat recording checks all three
active party members at zero HP and the return to the title; the ending
recording follows the final victory through the credits. The Pause recordings
use an unmodified game-made save; the ending and defeat saves use the
documented edits in [development](development.md). The Brazilian recording
covers the regional keyboard layout, a correction and a confirmed name in an
empty slot.

All 27 recordings (twenty US and seven Brazilian, 222,246 frames) matched the
reference emulator on all seven frame components. They reached 1,616 of the
2,486 registered segments and made 393,301,816 C++ calls; at those entries the
machine took the original path 448,563 times and no call was forced. The
coverage gate applies to each recording and to their combined counts (see the
[generated census](coverage.md)). The complete suite ran 353 tests with both
cartridges and the reference assets, with no skips.

State probes add evidence for rare dialogue, item, spell and animation paths
that the recordings miss. A private inventory, built from those 27 recordings
and 96 cartridge tests, gives each of the 14,195 listed instruction addresses
a verified C++ body, a verified original final instruction, or an individually
documented role; every instruction the recordings fetched lies inside that
listing. The instructions that stay original are mostly single steps that mark
a boundary the Z80 must still execute: the reset vector, the first
instruction after interrupts are re-enabled, and the frame wait that publishes
the frame-handler index at `00:0052`. They are listed in the private study and
are the starting point of milestone 14.
