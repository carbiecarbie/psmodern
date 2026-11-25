# Session handoff

Last updated: 2026-11-24

## Milestone 19 (planned, not started)

The maintainer approved the [milestone 19](milestone-19-plan.md) plan
(playthrough to the ending, Tier A) on 2026-11-24, with its acceptance
figures. No code or tests changed. Agents make the recordings, and only
strength and money may be edited between links. Each cartridge gets its own
playthrough. The new recordings run only at milestone closure, under the
`playthrough` label, behind a CMake option that is off by default. The Tier A
plan review is done (Opus, read-only). Its required changes are in the plan:
a chain test, an allow-list of the fields that may be edited, values a player
could reach, the stated limits of one route, the CMake option and measurable
acceptance terms. The maintainer approved those changes the same day. He
starts the milestone explicitly.

The plan builds on an earlier stop sweep, `../sweep-2026-11-23/report.md`,
and its unapplied `proposed-tests.patch`. Both are outside the repository.
They are an agent's report that part 0 must re-verify. The figures in the
plan come from the census of 2026-11-23: 1,200 probe-only segments, so the
1,198 in `project-status.md` is two short. That gets corrected at closure.

Milestone 18 was withdrawn on 2026-11-24. Its mockup in `../m18-mockup/` can
be deleted.

## Pending

- `../psmodern-re` is pushed at `2437608`. Its own handoff still says "not
  pushed"; correct that line in its next commit.
- A save near the petrified companion is still welcome
  ([player follow-up in PR #3](https://github.com/carbiecarbie/psmodern/pull/3#issuecomment-7108021206)).
  Milestone 19's playthroughs record the rescue without it.
- The macOS port (PR #2: a universal app of 1.3.1, Apple silicon and Intel,
  macOS 11+, no gameplay change) has the
  [requested changes](https://github.com/carbiecarbie/psmodern/pull/2#pullrequestreview-6034626295):
  its branch includes current `main` with the dungeon fix,
  `tools/package-macos.sh` stages in a fresh temporary folder and leaves the
  output folder's other files alone, and the ZIP was rebuilt from `b3c4d8c`
  ([contributor reply](https://github.com/carbiecarbie/psmodern/pull/2#issuecomment-7131446820)).
  It awaits review.
