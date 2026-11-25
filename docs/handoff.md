# Session handoff

Last updated: 2026-11-25

## Milestone 19 (implemented; review and acceptance pending)

[Milestone 19](milestone-19-plan.md) was implemented on 2026-11-24 and 25 by
Opus and Sonnet agents under one orchestrating session. The maintainer gives
the independent implementation review to an agent of another lab. The
running log is `../working-log.md`.

**What the branch `milestone-19` holds (on `e7fb1f5`, local, not pushed):**

- The harness: the stricter static walk, `rom_<version>_stop_guards` and
  `rom_<version>_stop_control` (436 tests in the complete suite, 432 before),
  the interrupt sweep of the two item-place calls, the option
  `PSMODERN_PLAYTHROUGH_TESTS` with the label `playthrough`, the chain test,
  the extra-recording tests and their tools, the census categories.
- The fourth reference patch (Maintainer decision, 2026-11-24): MesenCE left
  bit 3 of F stale after `LD A,I` and `LD A,R`; one playthrough frame showed
  it. All 27 checkpoint manifests were regenerated with identical data rows.
- Both chains: 25 US links (673,967 frames) and 27 Brazilian links (685,128
  frames), from power-on with empty cartridge RAM to the title after the
  credits, through the petrified companion's rescue. No game stop on either
  cartridge. 89 extra recordings (44 US, 45 Brazilian) for rare branches.
- [Coverage](coverage.md), regenerated from the closure runs: 362 segments
  reached by no recording (target 400) and 64 reached by US recordings only
  (target 100). Each has its reason in `tools/coverage-reasons.tsv`.
- Two entries in [original quirks](original-quirks.md) about loop passes that
  outlast the frame-end sample.

**Closure runs on `build-rel` (2026-11-25):**

- Complete suite, option off: 436 of 436 passed, no skip, 1,917.50 s
  (`build-rel/m19-complete-suite.log`; the limit was 1,984 s).
- Playthrough group, option on, first run: 373 of 377 in 2,564.67 s. The four
  prepared-route tests failed because the test program did not create its
  census folder. That was fixed in `tests/stop_sweep_tests.cpp`, and the four
  pass (`build-rel/playthrough-first-run.log`).
- Playthrough group, second run after that fix: 377 of 377 passed, no skip,
  2,535.52 s (`build-rel/playthrough.log`). [Coverage](coverage.md) is
  unchanged by it.
- `build-rel` is configured with the option off again, the new reference
  executable (`../m19-reference/mesen-publish/Mesen.exe`) and the new
  reference folder (`../m19-reference/reference/`). The old folder and
  executable are untouched.

**Outside the repository:** `../m19-playthrough/` and
`../m19-playthrough-br/` (driver, route, link scripts, notes),
`../m19-triage/` (part 3's scripts and results), `../m19-wt/` (patches,
`interface.md`, the worktree `part0`, which can be removed).
`../psmodern-re` has an allow rule for the equipment boost table `00:183A`
and a corrected handoff.

## Next steps

1. The independent implementation review, by the maintainer's choice of
   reviewer, of the local branch `milestone-19` in both repositories (not
   pushed; Maintainer decision, 2026-11-24).
2. After the review and the maintainer's acceptance: the closing summary,
   [project status](project-status.md), the history entry, the condensed
   plan, and the merge into `main`.

## To report at closure

- Link 01 has 11,137 frames, under the plan's 20,000. The maintainer
  accepted it on 2026-11-24.
- The route first skipped optional gear and could not beat the last two
  bosses. The maintainer chose on 2026-11-24 to record again with gear
  collected on the way; US links 09 to 25 are that second recording. Each
  boss link searches the idle frames before the door for a winning run. Both
  chains won both bosses with all four members alive.
- The Brazilian chain has 27 links against 25: two links passed their frame
  budget with the Brazilian draws and were split. Its link 04 edit gives
  Alis and Myau level 15 (US 12 and 10).
- Of the 362 segments no recording reaches, 145 run inside another segment's
  body, which the census does not count. Of the 64 US-only, 56 are reached
  only by checkpoints that exist for the US cartridge.
- The whole group takes about 42 minutes at `-j 16`; the plan's mark is 45. Most
  of it is the extras' `_start` tests, which each replay a chain link.
- Chain rule choices made in part 0: each link saves into exactly one slot;
  edited experience must stay inside its level's band.
- `project-status.md` still says 1,198 probe-only segments. It is updated
  when the milestone closes.
- MesenCE also lacks the rule that an interrupt accepted right after
  `LD A,I` or `LD A,R` resets P/V. No recording has shown it. The maintainer
  decided on 2026-11-24 to patch it only if one does.
- A reference capture failed twice inside `tools/reference.lua` under heavy
  machine load and passed when repeated. It fails loudly and writes no
  manifest. Not fixed.
- The first US route notes called three places "lags". They are not: the
  game runs one loop pass a frame there, and only a tool that looks for the
  frame wait at frame ends is affected.

## Pending from before

- A save near the petrified companion is still welcome
  ([player follow-up in PR #3](https://github.com/carbiecarbie/psmodern/pull/3#issuecomment-7108021206)).
  Milestone 19's playthrough records the rescue without it.
- The macOS port awaits a new ZIP built from current `main` with the dungeon
  fix, and a packaging change that preserves an existing staging folder
  ([requested changes in PR #2](https://github.com/carbiecarbie/psmodern/pull/2#pullrequestreview-6034626295)).
- `../m18-mockup/` can be deleted.
