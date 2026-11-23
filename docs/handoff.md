# Session handoff

Last updated: 2026-11-23

## Maintenance fix (priority over milestone 18)

A player's report showed the game stopping at `01:5538`. Two one-instruction
targets of the interaction code table, `01:5535` and `01:5538`, had no
segment since milestone 14, so every release from 1.0.0 to 1.3.0 stops at the
petrified companion of the first cave (a mandatory story point) and at five
other item places.

The fix registers both segments, adds the test `rom_<version>_static_walk`
(every code-table target and every declared exit target, call return and
fall-through must begin a segment) and a lockstep test of the six event
routes in the joins tests. The complete suite passed 432 of 432 with no
skips, run in a background shell and awaited by its completion notice
(`build-rel/hotfix-complete-suite.log`); the joins tests and the fast group
(190 of 190) passed again after the test-only route addition.

Pending:

- Release 1.3.1 with this fix. The maintainer authorized a Sonnet agent to
  prepare it, commit and push.
- `../psmodern-re` has the two catalog rows and an allow rule for the table
  uncommitted; its three checkers pass. Its commit waits for the maintainer.
- No recording walks through the petrified companion's rescue yet.

## Milestone 18

The maintainer started [milestone 18](milestone-18-plan.md). The bilingual
Options mockup is ready in `../m18-mockup/`. The `pt-options.png` and
`en-options.png` previews show the eighth row with square and TV pixels.
The corresponding `*-open.png` images show the list before scrolling.
The proposal keeps seven rows visible, adds a scroll hint on the border
and labels the new row PIXELS DE TV / TV PIXELS.

Next, get the maintainer's feedback on the layout and labels. Once approved,
start the sizing and stretch functions in the plan.
