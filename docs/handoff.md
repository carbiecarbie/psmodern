# Session handoff

Last updated: 2026-11-19

## Android play-test

The [milestone 16 plan](milestone-16-plan.md) passed its Tier A plan review.
The maintainer approved Parts 1 and 2 and chose the fixed touch cross.
The signed 1.0.0-m16.2 APK is installed over m16.1 on the Samsung SM-S928B
(API 37, 3120 x 1440). Both APKs, the Portuguese play-test guide and the
touch mockups are in `../m16-playtest/` outside Git. The m16.2 SHA-256 is
`5337497ecd16cdfc3ecd870c98c16e7d39bde25996b3fb711337d92e6aec84aa`.
Both packaged ARM libraries passed the zero-Z80/oracle and build-ID checks.
The signature and 16 KiB alignment passed (`build-rel/m16-cross-android-verification.log`).

The BR ROM imported from Downloads matched the source SHA-1. Button 1
launched the game, the portrait start screen showed the cross and both
buttons, and the game rendered in both orientations. Background/foreground
resumed the same process and audio. The game created its 8192-byte save.
The maintainer's landscape and Widescreen cross feedback, save/continue
and audio feel are still pending. The local `docs/milestone-16-plan.md`
lists the remaining device checks. Part 3 still needs the independent
Android implementation review and closure after device acceptance.

## Desktop verification

The US and BR fast-forward failures shared a test defect. At the start of
the wide session, the test compared the previous narrow session's discarded
time with the new frame clock's counter. In the US reproduction the counter
appeared to fall from 0.942 s to 0.333 s, in BR from 0.906 s to 0.355 s.
The dummy SDL audio queue caused waits during both intervals
(`build-rel/m16-speed-probe.log`).

`tests/fast_forward_session.cpp` now takes both readings when the current
clock issues a batch of frames. It checks 1x, 2x and 4x even after a
catch-up rebase, subtracting only the time discarded within that interval.
The allowed duration is 70% to 140% of the expected time. Both ROMs passed
with two injected 600 ms waits, one inside an interval and one at its end.
Running the 4x interval at 2x failed as expected (2.012 s against 0.997 s).
The temporary probes were removed and the independent review of the timing
test passed. Evidence is under
`build-rel/` in `m16-speed-boundary-before.log`, `m16-speed-boundary-after.log`
and `m16-speed-wrong-rate.log`.

On the local milestone 16 build each ROM passed three consecutive runs, for 36 checked
speed intervals across both canvases (`build-rel/m16-speed-final-repeat.log`).
The fast suite passed 182/182 (`build-rel/m16-speed-final-fast.log`). The
Codex test runner ran `ctest --test-dir build-rel -j 16 --output-on-failure --no-tests=error`
with UCRT64 on PATH and awaited the same shell session through exit. The
complete suite passed 390/390, with zero skips, exit 0, in 23 min 47.80 s
(`build-rel/m16-speed-final-complete.log`).

The earlier `shared_save_unit` timeout was fixed by the test cleanup helper,
which tolerates files disappearing during a walk (300 runs passed).
Production cleanup after a failed import in `Stage::~Stage` still uses
`remove_all`. A separate Tier A change awaits the maintainer's decision.

Only the fast-forward timing test fix and this handoff are in the current
commit. The other milestone 16 changes are in the local working tree on
`main`.
