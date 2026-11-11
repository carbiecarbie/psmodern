# Milestone 6 plan - Fast-forward

**Tier A. Status: completed and accepted (2026-11-11), commit `106ea66`.**

## Delivered

- Tab or the controller's right shoulder cycles 1x, 2x, 4x, then 1x.
  Each press acts once, including short taps; holding does not repeat.
  A small ROM-font indicator appears only while accelerated.
- Speed changes host pacing only. Every emulated frame keeps its original
  cycle budget, input sampling, sound-chip execution and save handling.
  Catch-up is bounded, with deadlines rebased on speed transitions.
- Music and effects are silent at effective 2x/4x, as requested after the
  maintainer's play-test. The output device pauses and its queue is cleared;
  emulated sound still advances. Returning to 1x restores fresh audio without
  replaying accumulated sound. Device replacement and pause preserve this rule.
- Speed is session-only and starts at 1x. F5 restores 1x; console Reset
  preserves the choice. Recording stays at 1x and ignores the shortcut.
- Options and loss of focus temporarily use 1x, then restore the chosen speed.
  Opening Options takes priority over simultaneous speed input. Focus loss,
  controller replacement and held buttons cannot activate a stale shortcut.
- No save format, game routine, ROM data or emulated hardware was changed.
  Controls and behavior are documented; the roadmap retains a future study
  of normal-tempo music during acceleration, without promising a solution.

## Validation and acceptance

Independent Tier A plan and implementation reviews passed, including the
all-audio mute amendment. Tests cover bounded scheduling at simulated 50,
60 and 144 Hz, input edges, focus and controller changes, reset, recording,
Options transitions, audio suppression and device recovery. Real SDL main-loop
sessions compare both ROMs frame by frame, including machine state, PSG writes
and source samples. Rendered indicators were inspected in both versions.

The final complete configured suite passed 109/109, with zero failures or
skips, in 337.90 seconds. A Sol 6.1 runner executed
`ctest --test-dir build-rel -j 16 --output-on-failure --no-tests=error`
and awaited process session 43954 through exit. The log is
`build-rel/m6-mute-full-suite.log`. Subsequent changes only close documentation.

The maintainer tested the revised silent-fast-forward build, said it was good,
and explicitly accepted closure, commit and push on 2026-11-11. The tested
executable's SHA-256 is
`6fa842351e4501e1794292b267444a58753a43950b6cd5b38d90de9f7c847e19`.

## Follow-up: normal-tempo audio

**Tier A. Status: completed and accepted (2026-11-11).**
The acceptance above applies to the original fast-forward delivery. The
maintainer subsequently requested investigation and implementation of music
that continues at its normal tempo during acceleration.

The optional presentation path executes the player's ROM sound routines on
a separate CPU, sound RAM and PSG. The gameplay machine continues unchanged.
On first acceleration, capture coherent sound state and oscillator phase;
retain this independent sound timeline when returning to 1x, until power
cycle. Sessions that never accelerate and recordings retain original audio.

Forward requests actually consumed by the game, in order, to the original
request handler. Advance sequence timing at the original frame period.
This deliberately separates request delivery from music timing: several
requests can arrive between music ticks. It does not promise original
request/tick interleaving during acceleration. Preserve the ROM's channel
priorities and commands rather than classifying or replacing its music.
External initialization and silence remain distinct events; pause must not
advance music. Speed changes must neither clear audio nor replay a backlog.

Independent design and implementation reviews passed. Both-ROM tests compare
audio against native sound routine execution, including constant-tempo PCM,
ordered requests, startup samples, fade provenance, transitions and unchanged
gameplay state. Private execution is bounded and falls back safely if its
verified contract is violated. The complete configured suite passed 111/111,
with no failures or skips, in 374.03 seconds. A Luna runner executed
`ctest --test-dir build-rel -j 16 --output-on-failure --no-tests=error`,
awaiting its execution session through exit using `write_stdin`.
The log is `build-rel/tempo-full-suite.log`.

The maintainer reported that the isolated play-test worked perfectly on
2026-11-11, accepting the result. The tested executable's SHA-256 is
`b98845bd0fa16e75b38101e629e8715f316c8bb21f376800cf2b6c726b8084b0`.
