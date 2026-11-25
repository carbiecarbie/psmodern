# Milestone 19 plan - Playthrough to the ending

**Tier A. Status: completed and accepted on 2026-11-25; commits
`88aa2cc` (implementation), `d8cc530` (review changes) and `3b6e970`
(closure); study: `psmodern-re` `21cfd16` and `67299de`.** The maintainer
approved the plan on 2026-11-24 and accepted the milestone after reading the
closing summary; nothing playable changed, so there was no play-test. Number
18 belongs to the withdrawn [television pixel shape](milestone-18-plan.md)
and is not reused.

## Scope

Show, on the US Rev-A and on the Brazilian cartridge, one route from a new
game to the title after the credits that never stops at cartridge code with
no C++ version, and make the part of the game that only state probes reach
much smaller. A stop found would have been fixed here.

## Key decisions

- Agents make the recordings, driving the C++ game headless; the driver never
  presses Reset (Maintainer decision, 2026-11-24).
- A playthrough is a chain of links, one chain per cartridge, from power-on
  with empty cartridge RAM. Each link continues from the previous link's save;
  the committed chain test checks every step. Each link that another
  continues writes exactly one save slot (chosen in the work; extended to every
  link after the review).
- Only strength and money may be edited, as an allow-list of fields, each
  value one a player could reach (Maintainer decision, 2026-11-24). After the
  review an edited experience gain must be a sum of awards the game gives
  (ROM static); the first rule, a band per level, admitted 1 point.
- The Brazilian cartridge gets a chain of its own (Maintainer decision,
  2026-11-24).
- The `playthrough` group (CMake option `PSMODERN_PLAYTHROUGH_TESTS`, off by
  default) runs only at milestone closure (Maintainer decision, 2026-11-24).
- The census tells four kinds of evidence apart: checkpoints, the group,
  prepared-state routes and probes only. Each segment left to probes, or
  reached by US recordings only, has a reason in `tools/coverage-reasons.tsv`.
- Adopt a fourth MesenCE patch and install the .NET 8 SDK to build it
  (Maintainer decision, 2026-11-24).
- Record again with gear collected on the way after the first route could
  not beat the last two bosses (Maintainer decision, 2026-11-24, to start
  from link 11). The start moved to link 09 to buy gear in the towns on the
  way, the smallest gear set that won was used, and each boss link searches
  the idle frames before the door for a winning run (the orchestrator's
  choices, confirmed by the maintainer on 2026-11-25).
- Accept that 102 reasons say the trigger is unknown; narrowing them is a
  candidate for later (Maintainer decision, 2026-11-25).
- The P/V difference after an interrupt (below) is patched only if a
  recording shows it (Maintainer decision, 2026-11-24).
- Commit locally before the review, without pushing (Maintainer decision,
  2026-11-24).
- Accept the ten links under 20,000 frames: the floor was a sizing guide, and
  each link ends at a natural save (Maintainer decision, 2026-11-25).

## What the work found

- **No stop.** Both chains, the 89 extra recordings and the four routes ran
  without meeting cartridge code that has no C++ version. No game code
  changed (`src/` untouched), so no release is needed.
- **A reference defect.** MesenCE left bit 3 of F unchanged after
  `LD A,I` and `LD A,R`; one frame of US link 07 differed. The fourth patch
  fixes it; all 27 checkpoint manifests were regenerated with identical data
  rows (Reference emulator). MesenCE also lacks the rule that an interrupt
  accepted right after those instructions resets P/V; no recording has shown
  it, and it is documented, not patched.
- **The gear lesson.** The first route treated optional gear as optional and
  lost to the last two bosses. Gear bought and found on the way fixed it;
  each boss link uses one winning run found by waiting a chosen number of
  frames before the door.
- Two places where a tool sampling frame ends never sees the frame wait are
  an observation effect, not a slowdown
  ([original quirks](original-quirks.md#a-finished-object-cell-lengthens-every-pass-of-the-dungeon-loop)).

## Results

1. **US chain:** 25 links, 673,967 frames, through the petrified companion's
   rescue (`01:5538`), both last bosses, the credits and the title. The chain
   test passes; every link matches the reference on all seven components at
   every frame, runs under strict lockstep with no refused call, and runs
   plainly with no stop. Exception: links 01, 03, 06 and 16 are under 20,000
   frames (accepted).
2. **Brazilian chain:** 27 links, 685,128 frames, the same checks. Two links
   were split for the Brazilian draws. Exception: links 01, 02, 04, 06, 16 and
   25 are under 20,000 frames (accepted).
3. **Probes only:** 362 of 3,156 segments are reached by no recording (was
   1,200; target 400); 4 of them are dispatched by a prepared-state route.
4. **Brazilian gap:** 64 segments are reached by US recordings only (was 587;
   target 100).
   The 426 reasons rest on: ROM run 161 (the segment runs inside another
   segment's body, which the census does not count), ROM static 10,
   Inference 153 and Unknown 102 (the trigger is not known).
5. The sweep tests pass on both cartridges. The complete suite passed 438 of
   438 with no skip in 1,735.25 s (limit 1,984 s). The group passed
   378 of 378 with no skip in 2,409.73 s.
6. The independent implementation review approved with eight required
   changes; all were made, with its two optional suggestions.

## Limits of the route

One route per cartridge. Unsaved work RAM restarts at every link. Strength
edits bypass ordinary progression and are checked field by field, not as a
whole save image. A boss link shows one winning input sequence, not a win
rate. Healing places, other routes and optional places are covered only by
the extra recordings, most of them from edited saves. Between closures a
maintenance fix runs only the complete suite.
