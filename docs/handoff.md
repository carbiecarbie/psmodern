# Session handoff

Last updated: 2026-11-21

[Milestone 16](milestone-16-plan.md) (Android version) is closed, committed
and pushed: implementation `20e317a`, desktop fast-forward test fix
`247e218`. The Samsung Galaxy S24 Ultra has `1.0.0-m16.4` installed; the
matching APK and the device evidence are outside the repository in
`../m16-playtest/` (APK SHA-256
`10d1cb3e0afeed18069f1c30d458d3001aa9db0f616df7ced29f42bf5dabc516`).

Milestones 1 through 16 are complete. No milestone is active; the
maintainer starts the next one explicitly.

Pending, each a separate decision for the maintainer:

- The possible cleanup in `Stage::~Stage` (maintenance task).
- Old worktrees from milestones 5-14 (under `~/.codex/worktrees/` and
  `../m14-wt/`) hold no uncommitted work of value and can be removed.
