# Session handoff

Last updated: 2026-11-25

## Milestone 19 (reviewed, required changes made; acceptance pending)

[Milestone 19](milestone-19-plan.md) was implemented on 2026-11-24 and 25 and
committed locally as `88aa2cc` on the branch `milestone-19` (not pushed;
`main` is at `e7fb1f5`). The independent implementation review, by an agent
of another lab, came back on 2026-11-25: **approve with required changes**.
Its text is `../m19-wt/review-codex.md`; the running log is
`../working-log.md`.

**What the review confirmed by its own runs:** both routes (the rescue, both
boss victories, the credits, the title), the fourth reference patch and the
figures 362 and 64. No game code changed in the milestone.

**The eight required changes, committed on `milestone-19` after `88aa2cc`:**

1. Chain test: every link that another continues must have written exactly
   one slot, and an edited experience must be a sum of awards the game gives.
   All 52 committed links pass unchanged. The negative cases run in the
   normal suite as `rom_<version>_playthrough_rules` (438 tests now).
2. The link test is strict whatever `PSMODERN_LOCKSTEP_STRICT` says.
3. The interrupt sweep of the two joins requires each interrupt to be
   accepted after the call and to return; the guard test has 291 states and
   separates the stack checks from the variable checks.
4. The plain runner reuses only a folder that carries its own marker file,
   and checks the final save flush.
5. [Coverage](coverage.md) tells the 4 route-only segments from the 358
   reached by neither a recording nor a route. `tools/coverage-reasons.tsv`
   has an evidence column.
6. `tools/extra-start-classes.tsv` classes each extra's start image;
   [development](development.md) states what the edit checks do and do not
   guarantee.
7. Closure disclosures: corrected below. The private repository's handoff is
   corrected, not committed.
8. The reasons file holds no location data; it is linted.

Also added, from the review's optional suggestions:
`tests/playthrough_inventory.txt` (the 145 expected links, extras and
routes; the group and the coverage generator fail on a difference) and
checks inside the chain test for the rescue, the bosses, the credits and the
title.

**Closure runs after those changes (2026-11-25, `build-rel`):**

- Complete suite, option off: 438 of 438 passed, no skip, 1,735.25 s
  (`build-rel/m19-complete-suite-2.log`; the limit is 1,984 s).
- Playthrough group, option on: 378 of 378 passed, no skip, 2,409.73 s
  (`build-rel/playthrough.log`).
- [Coverage](coverage.md) was regenerated from those runs with the inventory
  and `--require-reasons`: 362 and 64, as before the review.
- `build-rel` is configured with the option off, the new reference
  executable (`../m19-reference/mesen-publish/Mesen.exe`) and the new
  reference folder (`../m19-reference/reference/`).

## Next steps

1. The maintainer reads the closing summary (`../m19-wt/closing-summary.md`)
   and accepts the milestone or asks for changes.
2. After his acceptance: apply `../m19-wt/closure-docs.patch` (the condensed
   plan, [project status](project-status.md), the history entry, the
   roadmap) with its dates and commits filled in, commit, merge
   `milestone-19` into `main` in both repositories and push, on his word.

## To report at closure

- Ten links are under the plan's 20,000 frames: US 01 (11,137), 03
  (18,402), 06 (15,522) and 16 (15,081); Brazilian 01 (10,633), 02 (15,440),
  04 (18,710), 06 (15,412), 16 (18,310) and 25 (4,001). The maintainer
  accepted all ten on 2026-11-25: the floor was a sizing guide, and every
  link ends at a natural save.
- The route first skipped optional gear and could not beat the last two
  bosses. The maintainer chose on 2026-11-24 to record again with gear
  collected on the way; US links 09 to 25 are that second recording. Each
  boss link searches the idle frames before the door for a winning run. That
  gives one winning input sequence. It does not give a typical win rate, and
  it does not show that the first gear set could never win.
- The Brazilian chain has 27 links against 25: two links passed their frame
  budget with the Brazilian draws and were split. Its link 04 edit gives
  Alis and Myau level 15 (US 12 and 10).
- The edits are checked field by field, not as a whole save image. A raised
  level keeps the member's earlier spell counts until the game refreshes
  them: 18 records in 13 start images.
- Of the 89 extra recordings, 6 start from an unchanged chain save, 79 from
  a save edited to a state whose reachability by play was not verified, and
  4 from a state known to be artificial.
- Of the 426 reasons, 161 rest on a run (the segment runs inside another
  segment's body, which the census does not count), 10 on a reading of the
  code, 153 on inference and 102 say the trigger is unknown. The maintainer
  decides whether that is enough.
- The whole group takes 40 to 43 minutes at `-j 16`; the plan's mark is 45.
  The 52 chain links are about half of the processor time and the extras'
  start-image tests about a fifth.
- Chain rule choices made in the work, not by the maintainer: each link that
  another continues saves into exactly one slot; an edited experience must
  stay inside its level's band and be a sum of awards the game gives.
- The second recording started at US link 09, not at link 11 as the
  maintainer had chosen: the orchestrator moved it to buy gear in the towns
  links 09 and 10 pass, and told him in chat. He has not confirmed it.
- `project-status.md` still says 1,198 probe-only segments. It is updated
  when the milestone closes.
- MesenCE also lacks the rule that an interrupt accepted right after
  `LD A,I` or `LD A,R` resets P/V. No recording has shown it. The maintainer
  decided on 2026-11-24 to patch it only if one does; the reviewer agrees it
  can wait.
- A reference capture failed twice inside `tools/reference.lua` and passed
  when repeated. Heavy machine load is the suspected cause, not an
  established one. It fails loudly and writes no manifest. Not fixed.
- Three places where the driver saw no frame wait are an observation
  artefact, not slowdowns. The longest gap measured in a recording is 5,039
  frames; an agent's note of over 20,000 was not measured again.

## Pending from before

- A save near the petrified companion is still welcome
  ([player follow-up in PR #3](https://github.com/carbiecarbie/psmodern/pull/3#issuecomment-7108021206)).
  Milestone 19's playthrough records the rescue without it.
- The macOS port awaits a new ZIP built from current `main` with the dungeon
  fix, and a packaging change that preserves an existing staging folder
  ([requested changes in PR #2](https://github.com/carbiecarbie/psmodern/pull/2#pullrequestreview-6034626295)).
- `../m18-mockup/` can be deleted.
