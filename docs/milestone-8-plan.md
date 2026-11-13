# Milestone 8 plan - Full controller support

**Tier B. Status: completed and accepted (2026-11-13).** Implemented in
`a645dcd`.
Eighth milestone of the [roadmap](roadmap.md); stage 7 of
[design](design.md) section 8. Only the host input layer changed; no
original routine was rewritten.

## Goal

Every key and controller button can be changed from an in-game Controls
screen, and a controller can reach every host function the keyboard has.
With no controls file the program behaves exactly as release 0.2.0; the
game, recordings and replay never see the difference.

## Scope delivered

- **Remapping:** 13 actions (the eight console inputs and the Speed,
  Options, Map, Power cycle and Fullscreen shortcuts), each with one key and
  one controller input, kept in `controls.txt` beside `roms.txt`; explained
  for players in the new [controls](player-controls.md) page.
- **The Controls screen,** in the cartridge font and the Options window
  style, reached from the start screen and from a new fourth row of the
  Options window, CONTROLS; press to set, swap on conflict, RESTORE
  DEFAULTS, SAVE and CANCEL ([development](development.md#controls-screen)).
- **Controller:** the triggers can be set like buttons; Power cycle and
  Fullscreen can be given a controller button; menus and the dungeon map
  follow the player's choices.
- **Two decided items:** encounters gain 25 (choices 0/25/50/100/200) and
  the recommended pace becomes encounters 25 with experience 400; the
  automap shows a floor label.

## Key decisions

1. **One key and one controller input per action.** Power cycle and
   Fullscreen have no controller input by default; Alt+Enter stays the fixed
   fullscreen key; the left stick always moves; the six keys of the
   directions and buttons 1 and 2 can never be left empty.
2. **The file:** a short text file written only on SAVE; a damaged or
   partial file never stops the game, skipped lines keep their defaults, and
   the problem is reported only on the console, not in the window. Confirmed
   (Maintainer decision, 2026-11-13).
3. **No lock-out.** Changed: the start screen first followed the bindings;
   after the review it uses fixed keys and buttons, as the Controls screen
   does, so a controller-only player cannot lock themself out. An input held
   when the Controls screen opens is ignored until released, input by input.
   Confirmed (Maintainer decision, 2026-11-13).
4. **The list scrolls.** Changed: planned as one page; seven rows at the
   Options window's spacing with MORE ABOVE and MORE BELOW, because one page
   of close rows was unreadable. Confirmed (Maintainer decision, 2026-11-13).
5. **Positional button names** (A, B, X, Y, LB...) for every pad brand, as
   SDL's standard layout names them. Confirmed (Maintainer decision,
   2026-11-13).
6. **Triggers:** pressed past half travel, released below a quarter; a
   trigger held when a controller opens is seeded as held, on a best-effort
   basis.
7. **Floor label:** stairs always join a floor to the next one up, so each
   dungeon is a stack of floors; FLOOR 1 is the lowest floor with an exit to
   the outside, above it FLOOR 2, 3..., below it FLOOR B1, B2... A
   PSModern convention, not something the original shows. Confirmed
   (Maintainer decision, 2026-11-13).
8. **No quit-to-start-screen shortcut** in this milestone. Confirmed
   (Maintainer decision, 2026-11-13).

## Results

- **Original mode unchanged:** a test shows that the default bindings give
  the same console, menu and start screen input as the 0.2.0 mapping for
  every combination of the default keys and controller buttons; recordings
  and replay are untouched.
- **Tests:** the file (damaged, partial, size limit), the swap rules, the
  Controls screen with both entry flows and a failed SAVE, triggers on a
  virtual controller, and the floor labels on both ROMs.
- **Review** (optional for Tier B): no blockers; three should-fix and
  several minor findings, all applied; two intended behaviour changes were
  documented instead (keys, F5 included, are matched by scancode; release
  0.2.0 refuses an options file with encounters 25). The complete suite
  passed 159 tests, 0 failed, 0 skipped, before and after the fixes.
- **Play-test** (2026-11-13): the maintainer played the build
  (`psmodern.exe` SHA-256
  `08f4c8ef1458dbe9e86d7242c661f833f2e8cec6f374193e86ec08a9d3e22c70`), said
  everything was right and accepted the milestone as it is. His session's
  files show a saved `controls.txt` with Speed moved to the right trigger
  and Fullscreen given the left shoulder, and options saved at encounters
  25, experience 400 with the automap on.
