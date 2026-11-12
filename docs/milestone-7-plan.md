# Milestone 7 plan - Dungeons and the automap

**Tier A. Status: completed and accepted (2026-11-12).** Planned in
`39f14a3` (independent Tier A plan review, 2026-11-11); implemented in
`674c4fa`; closed by `391f1f1`, which condensed this plan.
Seventh milestone of the [roadmap](roadmap.md); stage 5 of
[design](design.md) section 8.

## Goal

Dungeon walking runs in C++, verified call by call. An optional automap
opens a map of the current floor showing only what the party has explored;
with it off, and in recording mode, the game is the cartridge.

## Scope delivered

- **Dungeon walking in C++:** eleven routines, about 545 original
  instructions in 53 segments: the input handler (`01:65EE`), the pitfall
  (`01:661C`), the step forward and back (`01:6758`, `01:6792`), the turns
  (`01:67B7`, `01:67E2`), doors (`01:681B`), the exit lookup (`01:68BC`), the
  object check (`01:6963`) and the cell readers (`01:6BCA`, `01:6BE9`). See
  [dungeons](how-it-works/dungeons.md) and
  [original quirks](original-quirks.md#dungeons).
- **The automap:** dungeon tables read from the player's ROM, exploration
  kept with each saved game, the map screen, its keys and an Options row
  ([development](development.md#automap)).
- **Stays on the Z80:** the 3D view, the dungeon load, text, battle, events,
  the world-map transfer (`01:7877`) and three pitfall sequences that switch
  interrupts off around a video copy (`01:6625`, `01:665A`, `01:66B1`).

## Key decisions

1. **Seen cells** are those the 3D view drew, cut at the first wall ahead,
   plus every cell stood on; a seen cell shows its true kind at once, a
   little more than the 3D view (Maintainer decision, 2026-11-11). Confirmed.
2. **Found cells:** a pitfall shows as floor until the party has stood on
   one; a fake wall shows as wall until walked through, then as floor
   (Maintainer decision, 2026-11-11). Confirmed.
3. **Fixed fights and scripted events.** Changed: planned as shown after
   discovery; never marked (Maintainer decision, 2026-11-11).
4. **Chests.** Changed: provisionally shown only when directly ahead; shown
   as soon as their cell is seen, drawn opened once opened (Maintainer
   decision, 2026-11-11).
5. **Exits:** the marker only on cells that really lead out; the 33 that
   start a scene, an interaction or an event show as plain stairs or doors
   (ROM static). Confirmed (Maintainer decision, 2026-11-12).
6. **Screen and keys:** M on the keyboard, X on a controller; game held as
   for Options; current floor only; no floor label. Confirmed (Maintainer
   decision, 2026-11-12).
7. **Icons:** new 8x8 icons, one door drawing in three colours (plain,
   locked, magic), a named exception to "no new art"
   ([design](design.md#non-goals)). Confirmed (Maintainer decision, 2026-11-12).
8. **Option and tracking:** AUTOMAP OFF/ON, off by default, settings format
   2; tracked even with the option off, never in recording mode or with
   `--no-routines`. Confirmed (Maintainer decision, 2026-11-12).
9. **Kept with each saved game:** `<save>.automap`, one record per saved game
   keyed by the SHA-256 of its game block; stored after a save (`00:1D2D`),
   restored after Continue (`00:06B4`), cleared on a new game (`00:05F4`);
   an unusable file is kept as a backup and replaced at the next save.
   Confirmed (Maintainer decision, 2026-11-12).
10. **Where the map looks:** those points and `00:0F48` in dungeon mode;
    nothing watches memory writes; a watched instruction that becomes a
    segment entry or exit makes activation fail (ROM static, ROM run).
    Confirmed.
11. **Three new exit kinds.** Changed: added to the mechanism (Part X, own
    independent review) after the study showed about 50 instructions would
    otherwise stay native (ROM static): words held on the stack across a
    native call, a conditional final jump, and a loop exit that splits long
    scans. Earlier segments use none of them.
12. **Automap beside the rewrite.** Changed: design 5.3 no longer has the
    automap wait for the movement rewrite; both tracks ran in one milestone
    (Maintainer decision, 2026-11-11).

## Results

- **Original mode unchanged:** fifteen checkpoints (six per version, three
  new US dungeon recordings) pass the per-call verifier and the routines
  on/off comparison with no forced calls; the new recordings, from
  game-made saves, match the patched MesenCE on every frame (ROM run).
  Machines are equal every frame in recording mode, option off and option
  on but never opened; map events are identical with routines on and off.
- **Census (ROM run):** the split object scan runs in C++ on 97.22% (US
  boss), 97.17% (Brazilian boss) and 96.88% (first walk) of calls; the other
  split scans 100%. Thirteen segments are covered by state probes only:
  locked and magic doors, scene and interaction exits, cascading pits, most
  object records ([coverage](coverage.md)).
- **State probes,** both ROMs: 172,752 states for the readers, 282,432 for
  movement, 221,243 for doors, exits and objects. The view drawing
  (`01:6AED`) never changed in 400 states with every unseen cell altered.
- **Speed:** replay medians 17.41 to 17.50 s (deep dungeon), 28.06 to
  27.96 s (boss), identical manifests and sound writes.
- **Reviews:** plan and Part X reviews passed. Rewrite review: pass with one
  required change (probes assert exact bounds); no difference in state or
  cycles. Automap review: pass with one required change (tests for a map
  from another game); tracking never changes the game. All applied. The
  complete suite passed 155 tests, 0 failed, 0 skipped.
- **Play-test** (2026-11-12): the maintainer played the build (`psmodern.exe`
  SHA-256 `c892ea3b5c64ccff97fc3f1ccc3ada96db363940d66b4f49da59dbd2a4585363`),
  said everything was right and accepted the milestone as it is. His
  session's files show about nine and a half minutes with fast-forward from
  the kit's Maharu Cave save; Automap on (settings format 2); a new save in
  a third slot whose `.automap` record matches its saved block, with two
  floors (103 of 105 walkable cells seen; 71 of 107, with one pitfall
  found); three cartridge saves. The optional door save was not used.
