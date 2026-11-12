# Milestone 7 plan - Dungeons and the automap

**Tier A. Status: planned (2026-11-11).** Seventh milestone of the
[roadmap](roadmap.md); stage 5 of [design](design.md) section 8.

## Goal

Walking in the dungeons runs in C++, verified against the original call by
call: steps, turns, the step back, stairs, doors, fake walls, pitfalls, the
check for chests, fixed fights and events, and the exits to the world map.
Beside it, an automap: a button opens a map of the current floor that shows
only what the party has explored. The map is an option, off by default; with
it off, and in recording mode, the game is the cartridge, US and Brazilian.

## Scope

Two tracks share one study, one integration, one implementation review and
one play-test; the automap reads state the original keeps (design 5.3).

- **Rewrite:** eleven routines, about 545 original instructions in 53
  segments: the dungeon input handler (`01:65EE`), the pitfall (`01:661C`),
  the forward step (`01:6758`), the step back (`01:6792`), the turns
  (`01:67B7`, `01:67E2`), opening a door (`01:681B`), the exit lookup
  (`01:68BC`), the object check (`01:6963`) and the cell readers (`01:6BCA`,
  `01:6BE9`).
- **Map:** dungeon tables read from the player's ROM, an exploration model
  and its companion file, the map screen, its keys and an Options row.
- **Stays on the Z80:** the 3D view and its animations, the dungeon load,
  fades, text, battle, the field menu, events and the world-map transfer
  (`01:7877`), each its own system; three short pitfall sequences that switch
  interrupts off around a video copy (`01:6625`, `01:665A`, `01:66B1`), since
  C++ would miss the interrupt the copy leaves pending (ROM static); three
  helpers that call rewritten entries (`01:684A`, `01:687A`, `01:688C`).
- **Out of scope:** walking speed, widescreen, a floor label, any change to
  the cartridge save format.

## Design decisions

1. **Maintainer decisions (2026-11-11).** The map shows explored cells only.
   Pitfalls and fake walls appear only once found; fixed fights (the bosses
   and guardians waiting on a fixed cell) are never marked. A seen cell shows
   its true kind as soon as it enters the view, side doors, stairs up or
   down, locked or magic doors and chests included, which is slightly more
   than the 3D view shows. A fake wall walked through is drawn as floor. A
   button opens the map of the current floor with the game held (music
   continues, no encounter); it is never shown permanently. Exploration is
   recorded even while the option is off. The map uses new simple 8x8
   icons, the doors one drawing in three colours (plain, locked, magic), a
   named exception to "no new art" ([design](design.md#non-goals)); the
   coloured mockup is the visual base. The milestone may close with the map
   and the rewritten units done if the rewrite slips.
2. **Segments, as in milestone 4.** Each routine is cut at every call or jump
   into code that stays native; each segment renders the original
   instructions from its entry for any state and ends with one instruction
   run by the core. A jump into the object check or the exit lookup is run by
   the core, so the two rewrite parts share no code (ROM static).
3. **Three new exit kinds** (Part X). (a) Words held on the stack across a
   native call and popped by the next segment (ten places). (b) A declared
   conditional jump as the final instruction, either outcome checked
   (`01:6712`, `01:672E` to the exit lookup; `01:68F4` to the world map).
   (c) A loop exit taken at its first arrival after a declared number of
   cycles, to split a long scan. Without them about 50 instructions stay
   native (ROM static). Every segment registered today uses none of them and
   runs exactly as before.
4. **Long scans are split** at the step to the next record (`01:6618`,
   `01:6988`, `01:68E1`), with bounds computed from the machine's state.
   Unsplit, the scan under a party standing on an object cell (up to about
   12,000 cycles, every frame) would fall back on 76% of calls (ROM run). Split
   every 128 cycles it is estimated at 97% in C++, above the 90% coverage rule
   (Inference from the measured frame positions).
5. **Where the map looks** (provisional, confirmed at the play-test). Only at
   the native call `00:0F48` in dungeon mode `$0B`; the credits walk
   (`01:4661`) never passes there. Nothing watches memory writes. At that
   point position, facing and floor are never in between two values, except
   inside a fake wall, which decision 6 handles (ROM static, ROM run).
6. **Seen and found** (provisional except where decision 1 settles it). Seen:
   the cells the 3D view drew, cut at the first wall ahead, plus every cell
   stood on (ROM static on `01:6AED`; a test runs the original drawing to
   prove an unseen cell never changes the picture). A fake wall shows as wall
   until walked through; a pitfall shows as floor until stood on. A chest
   shows once seen, though the game announces it only directly ahead
   (Maintainer decision, 2026-11-11; ROM static); opened chests show as
   opened; events get no marker. Dead and shadowed records never show (the
   game's first-match rule). The exit marker appears on a seen exit cell
   that leads to the world map; of the 80 exit-bit cells, the 33 whose
   record starts a scene or interaction show as plain stairs or doors (ROM
   static). Opened doors are not marked: the game closes them on reload.
7. **Kept with each saved game** (provisional). `<save>.automap` holds one
   record per saved game, keyed by the SHA-256 of the 1 KiB game block
   (`$C300-$C6FF`). Stored after the save completes (`00:1D2D`), restored
   after Continue (`00:06B4`), cleared on a new game (`00:05F4`), all native
   points (ROM static, ROM run). The first cartridge write after a save comes
   after the map's write; a failed map write is retried and never blocks the
   save; an unknown game or an unusable file gives an empty map, never a
   wrong one; the next save keeps an unusable file as `.automap.bak` and
   replaces it (Maintainer decision, 2026-11-11); no file it writes is refused.
8. **Keys and option** (provisional). M on the keyboard, X on a controller,
   at the dungeon's walking wait; M, X, Esc or cancel close. The game is held
   as for Options: opening and cancelling the game's own field menu, a legal
   original action that clears two bytes the field menu always clears
   (ROM static, ROM run). The Options window gains AUTOMAP OFF/ON, default
   off; settings become format 2, and format 1 reads as off. Off hides the
   map and ignores the keys; tracking continues. Recording mode and
   `--no-routines` neither track nor show a map.
9. **Observation points are guarded.** The host watches native instructions;
   one that becomes a segment entry or exit stops being visible, so
   activating the routines fails at once in that case. Code rendered inside a
   segment is caught by a test comparing the map's events with routines on
   and off.
10. **New recordings** of dungeon walking (US) may start from an edited save
    image, the edits recorded outside the repository (provisional; the game
    accepts an edited save: ROM run).

## Parts

Each part ends with the complete suite green; parallel parts share no file.

- **X** mechanism (Tier A, own independent review) and **B** base, cell
  readers and every stub and build target, in parallel; **T** recordings in
  parallel with everything up to I.
- After X and B: **R2** handler, pitfall, steps and turns; **R3** door, exit
  lookup and object check; **M1** exploration model and companion file. Then
  **M2** host: screen, keys, Options row, saving.
- **I** integration, **D** chapter and quirks, **V** implementation review,
  **Q** play-test kit, **C** closure.
- **If the rewrite slips:** unfinished routine parts are left unregistered;
  I runs the map tests and the checkpoints with the segments that are done;
  D documents only those routines; V reviews what ships. The rest moves to a
  later milestone.

## Risks

- State probes alone cover locked and magic doors, scene and interaction
  exits, cascading pitfalls, most object records and the scripted callers.
- The new exit kinds change the verifier; Part X is reviewed before use.
- The scan split rests on an estimate; if a recording misses the 90% rule,
  a census rule for loop continuations goes to the maintainer.
- A future rewrite of the save, load or new-game copies would hide the map's
  observation points; the guard and the event comparison catch it.
- A format 2 settings file is refused by release 0.1.0, which keeps the
  original settings until the player applies new ones.
- Suite time: each new test stays under about 60 seconds.

## Acceptance

- **Tests.** ROM-free tests of the three exit kinds; every earlier mechanism
  fault keeps its message. State probes of every segment on both ROMs: all
  256 cell values in every facing, every stairs, fake wall, pitfall and
  object cell, all object and exit records. ROM-free tests of the model and
  the companion file, including the largest file. On the ROM: table counts,
  the original drawing unchanged by any unseen cell, save, Continue and
  power cycle with real files.
- **Lockstep.** The six checkpoints of each version and two or more new US
  recordings pass the per-call verifier and the routines-off comparison under
  the coverage rules. Tracking changes nothing: per-frame equality with the
  option off, on, and on but never opened; the map's events are identical
  with routines on and off.
- **Play-test** (one session, US). Only what needs a person: how the map
  looks and reads; whether the found rules feel right in play; how M and X
  feel with the game held; the Options row; one smoke save and Continue in the
  real settings folder (the suite settles save, Continue and power cycle with
  real files). The provisional decisions are confirmed or changed there.
