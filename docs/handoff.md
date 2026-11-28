# Session handoff

Last updated: 2026-11-28

## Milestone 20

[Milestone 20](milestone-20-plan.md) is complete and the play-test is approved.
Commit `151d7b8` contains the implementation approved for `main`.
[Hosted Windows and universal macOS CI](https://github.com/carbiecarbie/psmodern/actions/runs/46155599061) passed.

The final Windows launchers and signed Android package are under
`../m20-playtest-final/`.
The dedicated test runner's completion notifications and local logs confirm
450/450 regression tests and 378/378 playthrough cases, all with zero skips
(`build-rel/m20-continue-complete.log` and `m20-continue-playthrough.log`).
The no-ROM check verified all 325 required skips. The paired images for the
Continue fix are under `../m20-temporal/title-picker/`.

## Other open work

Two Android Speed taps within one frame still count as one.

The approved [Milestone 21 plan](milestone-21-plan.md) covers the player's
SMS Power! v2.6.1 source ZIP, both script variants and translator credits.
Its implementation needs a separate start by the maintainer.

Release 1.4.0 is current. The macOS packaging contribution is tracked in
[issue 11](https://github.com/carbiecarbie/psmodern/issues/11). Keep the
already-accepted milestone 19 follow-ups in the [roadmap](roadmap.md).
The reference executable and manifests for this checkout are in
`../m19-reference/`. The published packages are in `../release-1.4.0/`.
