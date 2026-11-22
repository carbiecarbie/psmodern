# Session handoff

Last updated: 2026-11-22

Milestone 17 (Japanese FM soundtrack) is completed and accepted: the
maintainer passed all seven play-test items on Windows and on the Galaxy S24
Ultra. The closure documents are written (condensed
[plan](milestone-17-plan.md), project status, history, roadmap). Everything
is uncommitted on `main` over `e98a89b`, in the primary checkouts. The latest
published release is 1.2.0; milestone 17 is not released. Release packages
and signing material stay outside Git in `../releases/` and
`../android-signing/`.

The complete suite passed 430/430 with zero skips on the final build
(`build-rel/m17-complete-suite.log`); no production code changed since. The
play-test builds are in `../m17-plan/packages-ui/` and `../m17-device/builds/`.

`psmodern-re` also holds uncommitted milestone 17 work: the Japanese driver
annotation (`catalog/japanese-sound.json`) and the catalog and citation
checker changes for `JP:` addresses, plus its untracked memory files
(`AGENTS.md`, `CLAUDE.md`, `docs/memoria/`). Its own handoff is
`../psmodern-re/docs/memoria/handoff.md`.

## Resume here

1. With the maintainer's authorization: commit `psmodern` (implementation
   and closure together, including `third_party/nuked-opll/`) and
   `psmodern-re` (its own commit), then a small `psmodern` commit adding the
   commit hashes to the plan's status line and the history entry.
2. Push only if he asks. A release with milestone 17 is his separate call.
