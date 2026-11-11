# PSModern - Project History

One short entry per completed milestone. Details are in each condensed plan
and in Git.

## Milestone 1 - Foundation (2026-11-04 to 2026-11-08)

Phantasy Star runs from the player's ROM on a virtual Master System, in
both versions, with picture, sound, controls and saves. Twelve recordings
match a patched MesenCE reference on every frame. An independent review
led to fixes and to six maintainer decisions. The maintainer accepted the
milestone after playing the US version.
[Plan](milestone-1-plan.md). Last implementation commit: `4585634`; closed
by `408077f`.

## Milestone 2 - Verification (2026-11-09)

Controller polling is the first C++ replacement, with the same controls and
timing as the original. Per-call and frame comparisons pass all twelve
checkpoints; all 93,555 observed calls run in C++. Speed meets the limit,
independent Tier A reviews passed, and the maintainer's US play-test passed.
The Brazilian recordings pass automatically. The maintainer accepted the
milestone after playing the US version.
[Plan](milestone-2-plan.md). Last implementation commit: `70c3454`; closed
by `f6d0fc7`.

## Milestone 3 - Study base (2026-11-09)

A private, data-free disassembly rebuilds the player's ROM exactly, with a
catalog of routines and RAM fields that every public citation is checked
against. The encounter checks, the experience award and the sound driver
are found and explained in three new chapters. The design was corrected
from the study and the roadmap written. The maintainer accepted the
milestone after reading the chapters and the roadmap.
[Plan](milestone-3-plan.md). Last implementation commit: `24410ee`; closed
by `1d4c6a1`.

## Milestone 4 - First win (2026-11-10 to 2026-11-11)

The random-number routine, the random-encounter checks and the experience
award now run in C++, checked call by call against the original, on all
twelve recordings and by state probes over every input. Two options, off by
default, set the encounter rate (0, 50, 100 or 200%) and the experience
gained (100, 200 or 400%); with them off the game is identical to the
original. Speed meets the limit and three independent reviews passed. The
maintainer accepted the milestone on 2026-11-11 after playing the US version
with the launchers and finding everything correct. He chose fixed values for
the options instead of ranges, with encounters 50 and experience 400 as the
Switch-like setting.
[Plan](milestone-4-plan.md). Last implementation commit: `3609f6b`; closed by
`4066cb2`.
