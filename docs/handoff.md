# Session handoff

Last updated: 2026-11-19

The maintainer withdrew the five cartridge fixes after playing the milestone
15 prototype. The six-row menu and original game rules are restored. The
Windows audio repair remains. Existing play-test profiles keep their other
settings. The [milestone plan](milestone-15-plan.md) identifies both builds
and the recovery patch. The local launcher is `../m15-playtest/Jogar.cmd`.

The lightweight runner reported completion of
`ctest --test-dir build-rel -j 16 --output-on-failure --no-tests=error` with
exit 0 after 1,534.15 seconds. All 389 tests passed with zero skips.
`build-rel/m15-audio-only-ctest.log` holds the full output. All 2,057 RISE
recording frames matched the cartridge, and cancelling the spell left Noah
with 58 MP.

The original 1,810.24-second WASAPI exercise with this queue policy is in
`../m15-audio/accepted-build-30m-final/`. A separate 35-second WASAPI check
of the revised kit recorded zero underruns or resets. Its log is
`../m15-audio/audio-only-wasapi/host.log`. The milestone is closed.
