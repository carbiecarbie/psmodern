# Session handoff

Last updated: 2026-11-23

## Release 1.3.1 (prepared, not published)

Release 1.3.1 fixes the stop at `01:5538`: the interaction code table
targets `01:5535` and `01:5538` had no segment since milestone 14, so every
release from 1.0.0 to 1.3.0 stops at the petrified companion of the first
cave (a mandatory story point) and at five other item places. The fix is
`1840de2`; its complete suite passed 432 of 432 with no skips
(`build-rel/hotfix-complete-suite.log`) and is reused. The prepare commit
`68d8a33` only bumps the version; its build is warning-free and the fast
group passed 190 of 190.

Packages, built from `68d8a33` and kept in `../releases/`:
`PSModern-1.3.1-windows-x64.zip` (extracted and `--version` checked with
`PATH` limited to System32) and `PSModern-1.3.1-android-arm.apk`
(`versionCode 10301`, same signing certificate as 1.3.0). The notes draft is
`../releases/release-notes-1.3.1.md`.

Pending:

- The release **waits for the maintainer's word**: then tag `v1.3.1` on
  `68d8a33` and publish. No tag or GitHub release exists yet.
- After publishing, record "Release 1.3.1" in `docs/project-history.md`,
  `docs/project-status.md` and `docs/roadmap.md`.
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
