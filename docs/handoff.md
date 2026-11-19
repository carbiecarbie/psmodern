# Session handoff

Last updated: 2026-11-19

## Done in the last session

- Discussed the next direction with the maintainer. Chosen order: milestone
  15 (bug fixes option and the sound stalls), then an Android build for the
  maintainer's family. Modding waits until the requester says what they want
  to change; a new look for the menus is not wanted now.
- Wrote [milestone 15 plan](milestone-15-plan.md) and added milestone 15 and
  the Android direction to the [roadmap](roadmap.md). Documentation only.

## Current state

- Release `v1.0.0` is published; no production code changed since.
- Milestone 15 is planned and not started. The maintainer will hand it to a
  Codex session.

## Pending / next steps

- The fix list in the plan is the planner's proposal: the maintainer may move
  entries between "fix", "investigate first" and "not fixed" before the start.
- Part 2 of the plan holds an unconfirmed lead on the sound stalls
  (`Audio::submit` / `Audio::reset` in `src/app/sdl_host.cpp`); confirm by
  measurement before changing anything.
- Old milestone 14 worktrees under `D:\Projetos\Phantasy Star\m14-wt\` and
  `r100-readme-images\` can still be pruned.
