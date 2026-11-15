# Milestone 11 plan - World map and towns

**Tier B. Status: completed and accepted (2026-11-15).** Implemented in
`78254af` and closed by `f5cc348`. Eleventh milestone of the
[roadmap](roadmap.md); stage 8 of [design](design.md) section 8.

## Scope delivered

- Walking on the world map and in towns runs in C++ in both versions, as 153
  short pieces ("segments") in six groups, each verified on every call
  against the original: the picture's update in the frame interrupt
  (`00:0235`, `00:029F`, `01:588B`, `01:61F5`, `01:62BC`, `01:73D0`,
  `01:7C18`); steps, the camera and the screens kept in memory (`01:7143`,
  `01:723D`, `01:72A6`, `01:733A`, `01:744B`); sprites and the object list
  (`01:576A`, `01:5818`, `01:5853`, the map's object types from `01:599F`);
  what the ground allows, vehicles and ice included (`01:7764`, `01:74E4`,
  `01:756C`, `01:7602`, vehicle placement); entrances, transfers and special
  ground (`01:77AC`, `01:7877`, `01:792A`); and map modes, loading and the
  road (`00:0B08`, `00:0B6A`, `00:0CA6`, `00:0E8B`, `00:0ED7`).
- Three new US recordings, `road`, `vehicles` and `ice`, equal to the patched
  MesenCE on every frame. The road starts from an unedited game-made save;
  vehicles and ice start from a game-made save with documented edits of saved
  fields (the vehicles in the item list, the position, the transport).
- A widescreen defect the recordings exposed, fixed: the broken-ice trail
  was lost in the sides at a screen change.
- The [world map chapter](how-it-works/world-map.md), the original quirks
  found, development notes and the regenerated coverage census.
- With every option off the game stays the cartridge. Out of scope: the
  palette fades and the tile animators that are not the map's (milestones 12
  and 13, Maintainer decision, 2026-11-15), battle object types and the
  spaceship scene (milestone 13).

## Key decisions

1. **No change to the registered-routine mechanism** (hence Tier B): the
   frame interrupt already ran C++, and each video write keeps its original
   cycle under the per-call verifier (ROM run). A needed change would have
   been its own Tier A part with mandatory reviews; none was.
2. **Stays on the Z80:** the four short stretches with interrupts off (three
   in the map load `00:0B6A`, one in the ice breaking `01:756C`), interrupt
   entry and exit, the jump-table dispatchers, the graphics decompressor,
   palette fades, battle sprites and object types, the sound driver and the
   spaceship (milestone 13).
3. **The map's frame wait stays native:** Options and Language open at
   `00:0B14`; no piece contains, starts or ends at it, and a test compares the
   instructions the host watches with routines on and off.
4. **Long loops are cut into pieces** (milestone 7's loop exits): whole,
   `01:723D`, `01:72A6`, `01:733A` and `01:744B` crossed a frame boundary on
   74-100% of calls (ROM run), so each is split to run in C++ on at least 90%
   of its calls.
5. **Long copies stay native.** A copy of about 255 bytes (about 5,400
   cycles) crosses a frame boundary too often to live in a piece: the object
   slot clearing (`00:0CAC`-`00:0CB1`) and the clearing at the end of a
   transfer (`01:7921`) stay on the Z80, and the pieces end just before them.
   `00:0CA6` is split at `00:0CB3` for the same reason. No call is forced
   past an interrupt or frame boundary: the allow-list of such calls is empty.
6. **`01:591E` is not an entry of its own.** The 64-byte sprite copy runs in
   C++ inside the pieces that jump to it but cannot end a piece (a repeated
   port-write instruction needs a mechanism change); it stays native on its 3
   direct entries in the recordings.
7. **New US recordings only,** matched against the patched MesenCE (Maintainer
   decision, 2026-11-15): the map code is the same in
   both versions (design 5.5), so no Brazilian recordings were made and the
   probes run on both ROMs.
8. **Widescreen learns broken ice from the name table:** the viewer reads the
   edited cells from the name table of the screen and holds them until the
   name table shows them whole, so the game is never touched.

## Results

- Tests on both ROMs: the new kit and each part's tables, state and scenario
  probes of every piece, the host-observed instructions on routines on and
  off, the eighteen checkpoints with their `_options` runs, and widescreen
  passivity on all 21 recordings (13 US, 8 Brazilian).
- Coverage: every entry reached 20 or more times runs in C++ on at least 90%
  of its calls on the eighteen checkpoints, with no forced calls.
- Complete suite: **237 passed, 0 failed, 0 skipped**, with both ROMs, in
  about 17 minutes of real time at `-j 16` (about 4 minutes before this
  milestone: the map probes and the three recordings are heavy).
- The citation check passes. An independent implementation review approved the work with no required
  changes; its test and documentation recommendations were applied (the
  ROM-stack probes at equal cycles, bounded widescreen skips, the documented
  limit below).
- The maintainer play-tested in one US session with the kit (normal play, his
  own save, and test saves in the Land Rover, the hovercraft and the Ice Digger
  on Dezoris, the road and a cave, with widescreen on for vehicles, ice and
  the road): everything behaved like the original, nothing to fix. He accepted
  the milestone on 2026-11-15.

## Known limits

- **Native stretches** (decisions 2, 3, 5 and 6) remain until milestone 13.
- **Port-write order inside a piece** rests on reading the code and the kit's
  timing tests; the per-call check covers state and total cycles, and an
  access log is not planned.
- **Probe-only paths:** a few map pieces are reached only by state probes, and
  the road, the vehicles and the ice have one US recording each.
- **Slow probes:** terrain and modes take about 90 and 250 seconds each.
- **Widescreen** can still draw a frame behind (late pattern writes, 0.55% of
  map frames) and shows a broken-ice trail only once the cartridge's name
  table has it whole.
