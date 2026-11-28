# Session handoff

Last updated: 2026-11-28

## Optional retro interface

[Milestone 20](milestone-20-plan.md) is on the authorized `test/milestone-20` branch, based on `20d7a5a`. The maintainer approved the opening rhythm and reported that all
play-test items passed except two on the Continue screen. Both are fixed.
The maintainer subsequently confirmed that the play-test is approved.
The tested 2 px border remains. The user's report did not request another
border width.

- Title Continue preserves the cartridge's fixed two-row prompts, including
  the US implicit row break and the scroll that removes the previous choice.
  The save picker follows the title's direct draw path. Slot names, highlight
  and blinking cursor are visible, and loading or deleting releases the list.
- Actual US and BR title sessions selected and loaded the second save. The
  focused screen, text and gameplay tests passed 6/6, including distinct
  saved names and both slot instructions. Paired images are under
  `../m20-temporal/title-picker/`. The earlier prompt capture and isolated
  build records are under `../m20-continue-fix/`.
- The current Windows launchers in `../m20-playtest-final/windows/` use the
  corrected executable without changing the player's profile. The signed
  test APK in the same package's `android/` directory covers both ARM ABIs.
- The dedicated runner ran `ctest --test-dir build-rel -j 16 --output-on-failure
  --no-tests=error`, then enabled the playthrough group and repeated the
  command with `-L playthrough`. Its completion notifications and logs
  confirmed 450/450 in 1,778.17 s and 378/378 in 1,916.88 s with zero skips
  (`m20-continue-complete.log`, `m20-continue-playthrough.log`).
  Windows without ROMs passed separately, including its 325 required skips
  (`build-norom/m20-continue-junit.xml`).
- Independent review rechecked these title fixes, the SRAM reader, cursor,
  retry, cancellation and native passivity. The maintainer authorized publication of the test branch
  for hosted Windows and macOS verification.

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
