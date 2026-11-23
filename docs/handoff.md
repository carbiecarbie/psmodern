# Session handoff

Last updated: 2026-11-23

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
  uncommitted; its three checkers pass, the payload check was not run. Its
  commit waits for the maintainer.
- No recording walks through the petrified companion's rescue, and nobody
  has played through it on 1.3.1; it needs a save near that place.
- `docs/images/start-screen.png` still shows the 1.3.0 footer.

## Milestone 18

The maintainer started [milestone 18](milestone-18-plan.md). The bilingual
Options mockup is ready in `../m18-mockup/`. The `pt-options.png` and
`en-options.png` previews show the eighth row with square and TV pixels.
The corresponding `*-open.png` images show the list before scrolling.
The proposal keeps seven rows visible, adds a scroll hint on the border
and labels the new row PIXELS DE TV / TV PIXELS.

Next, get the maintainer's feedback on the layout and labels. Once approved,
start the sizing and stretch functions in the plan.
