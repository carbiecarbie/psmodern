# Milestone 5 plan - Options inside the game

**Tier A. Status: completed and accepted (2026-11-11).**
Implemented in `d9ae7d7`; closed by `8e010dd`; private annotations in `8db0a92`.

## Delivered

- Escape/controller Y opens Options only during free exploration in towns,
  the world map and dungeons, with controls released. Other contexts ignore
  requests instead of delaying them.
- The current scene stays behind a black window using the ROM font, frame
  and cursor. The live native field menu keeps music running while gameplay
  waits. Closing restores caller state and consumes input through cleanup.
- Two rows: encounters (0/50/100/200%) and experience/meseta (100/200/400%).
  Each 100% is DEFAULT; 50% encounters and 400% experience are RECOMMENDED.
  Confirm persists and closes; cancel discards. There are no preset/action
  rows or permanent button-help footer. Missing punctuation is composed at
  runtime from ROM strokes; no extracted assets are stored in the repository.
- Versioned companion settings retain preferences beside unchanged raw SRAM.
  Digest associations, staged publication and backups support crash recovery.
  Launch flags override individual saved fields until explicit confirmation.
- Options activate at the native dispatcher after a complete game operation;
  a battle's reward display and all awards use one rate. Recording ignores
  Options; raw-Z80 sessions remain read-only and preserve saved preferences.
- Battle setup and money addition run in C++. Battle meseta follows the
  experience rate, saturating reward and party total at 65,535. Scripted zero
  rewards and nonbattle sources stay unchanged. Original state and cycle
  behavior remain verified with rates at 100%.
- Player/developer documentation, party explanation, generated coverage and
  private annotations/catalog were updated. Automap, remapping, walking speed
  and other roadmap features remain outside this milestone.

## Validation and acceptance

Independent Tier A plan and implementation reviews passed, including the
native-menu revision. The final complete configured suite passed 107/107,
with zero failures or skips, in 315.01 seconds. A lightweight runner executed
`ctest --test-dir build-rel -j 16 --output-on-failure --no-tests=error`
and awaited completion through its process session. Both ROMs were tested.
Coverage includes original lockstep/state/cycle checks, storage crash recovery,
reward timing, actual SDL sessions, native exploration entry/exit, rejected
contexts, input release, continuous sound and scene preservation.

The maintainer approved the functional play-test, requested the integrated
window revision, then played the revised fixed US build and confirmed on
2026-11-11 that it matched the requested behavior. He explicitly accepted
closure and authorized commit and push for milestone 5. The tested binary's
SHA-256 is `ce335c73d8149b678890ef4100e83880ded14d5decde35054fd1bf954bc98015`.
