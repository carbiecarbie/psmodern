# Session handoff

Last updated: 2026-11-24

## Release 1.3.1 (published)

Release 1.3.1 is published as GitHub release v1.3.1 from `68d8a33`, with
`PSModern-1.3.1-windows-x64.zip` and the signed
`PSModern-1.3.1-android-arm.apk` (`versionCode 10301`, same signing key as
1.3.0); both matched their checksums after download. It fixes the stop at
`01:5538` (fix `1840de2`): two targets of the interaction code table had no
segment since milestone 14, so releases 1.0.0 to 1.3.0 stopped at the
petrified companion of the first cave and at five other item places.
Packages, logs and release notes are in `../releases/`.

Pending:

- `../psmodern-re` has the two catalog rows and an allow rule for the table
  committed and pushed (`2437608`, all its checks passed). Its own handoff
  still says "not pushed"; correct that line in its next commit.
- A save near the petrified companion is still needed to record the rescue
  on the official release. A player got past `01:5538` on the Brazilian ROM
  using the contributor's fixed macOS build. The duplicate fix was closed.
  [Player follow-up in PR #3](https://github.com/carbiecarbie/psmodern/pull/3#issuecomment-7108021206)
- The macOS port awaits a new ZIP built from current `main` with the dungeon
  fix, and a packaging change to preserve an existing staging folder.
  [Requested changes in PR #2](https://github.com/carbiecarbie/psmodern/pull/2#pullrequestreview-6034626295)

## Milestone 18

The maintainer withdrew [milestone 18](milestone-18-plan.md) (television
pixel shape) on 2026-11-24 after seeing the Options mockup, before any
production code. The plan records the reasons and the
[roadmap](roadmap.md#decided-against) lists it as decided against. The
Options page stays at seven rows.

The mockup in `../m18-mockup/` is outside the repository and can be deleted.

No milestone is active. The maintainer chooses and starts the next one.
