# Milestone 10 plan - Widescreen on the world map, in towns and in the spaceship flight

**Tier A. Status: completed and accepted (2026-11-14).** Implemented in
`581b4f3` and closed by `6101dd9`. Tenth milestone of the
[roadmap](roadmap.md); stage 8 of [design](design.md) section 8.

## Scope delivered

- An option, off by default, shows the world map, the towns and the
  spaceship flight between planets 16:9: 344 x 192 square pixels, the
  original 256 in the centre and 44 more per side, drawn from the map (in
  space, from the starfield) with the cartridge's tiles and palette, the
  blank left column included. Other screens (battles, dialogue, shops,
  houses, dungeons, scenes) keep 256 pixels, centred between black bars.
- WIDESCREEN OFF/ON is the fourth Options row, per saved profile beside
  AUTOMAP, applied at once and read-only in recording mode and with
  `--no-routines`. With the option off, game and picture are those of
  release 0.4.0; with it on, the machine is still the cartridge every frame.
- Nothing was rewritten and no native instruction is watched: the milestone
  reads state the original keeps, which is why it comes before the map
  rewrite (milestone 11). Out of scope: widescreen in battles, dungeons or
  scenes (design section 2) and centring on the leader.

## Key decisions

1. **Read-only presentation layer** (Maintainer decision, 2026-11-14). After
   each frame the driver copies VRAM, CRAM, registers and a few work-RAM
   bytes; the virtual VDP and the machine never see the option.
2. **344 x 192, square pixels**, the original 256 at x 44-299.
3. **Town edges show what the cartridge has**: the town sheet's filler
   screens, no clamp, no black; world maps wrap (ROM static).
4. **The blank left column is drawn from the map.** The map load `00:0B6A`
   blanks x 0-7 of the picture (ROM static); the widescreen centre shows
   the map there.
5. **Which frames widen:** game mode `C202` 8 to 0Ch, 0Eh and 0Fh (map,
   dungeon load while the map fades, interaction load, road) with register
   0 = 26h, the display on and an anchored camera; modes 4 and 5 (the
   flight) by their own rule. Other frames show the centre between black
   bars.
6. **Held camera.** The camera is anchored only on a consistent frame of
   mode 9 or 0Fh, then advances by the deltas of the latched scroll
   registers and never reads work RAM again; an inconsistent frame, display
   off, leaving the family, power cycle or a ROM swap drops it
   (pillarbox until the next consistent frame) (ROM run).
7. **`game::MapScreens`** reads the screen table, run-length coded screens
   (`01:723D`) and metatiles (`01:744B`) from the player's ROM, bounded,
   refreshing on a ROM swap; `CC00` overrides the ROM for edited cells (ice)
   only when its page matches and is stable (Inference).
8. **Band renderer** with its own small mode-4 background decoder, proven
   bit-equal to the VDP by tests, so the verified VDP stays untouched. Bands
   need no sprites.
9. **Presentation** (Maintainer decision, at the play-test): with the option
   on every game screen uses the 344 canvas so the window never jumps; the
   start screen stays 256, overlays stay 256 in the centre. At 1366 x 768
   the scale drops from 4 to 3, accepted. Applying the option resizes a
   normal window; maximized and fullscreen change only the viewport.
10. **Spaceship** (Maintainer decision, after the first play-test): modes 4
    (`00:0A5C`) and 5 (`00:086F`), entered from an interaction exit:
    take-off over the origin map, space, descent over the destination map.
    In space the bands are the name table wrapped as the VDP would draw it,
    so stars at the right repeat those at the left. `WideView::ship` derives
    the map camera from work RAM and matches the name table.
11. **Settings format 3** adds a widescreen token; formats 1 and 2 read as
    off, and format 2 is still written while no record has widescreen on,
    so 0.4.0 keeps reading the file.

## Results and acceptance

- Independent reviews of the plan (two rounds), the implementation and the
  spaceship addition approved the delivery after fixes.
- Tests on both ROMs: decoder against the VDP, map reader, centre
  agreement, scroll consistency, a gating oracle, state probes (both wraps,
  a vehicle, the road, the ship and its eight routes, power cycle, ROM swap,
  option on mid-stay and during a transfer fade), passivity on all 18
  recordings (machine equal to the option-off run every frame, option-off
  picture equal to 0.4.0's), settings formats and the Options window.
- Complete suite: **206 passed, 0 failed, 0 skipped**, with both ROMs.
- The maintainer play-tested on 2026-11-14 the world map, towns, transfers,
  fades, battles, Options, fast-forward, pause and fullscreen, and agreed
  with every provisional decision. Vehicles were not play-tested because no
  save reaches one. After the spaceship flight was added at his request he
  play-tested it and accepted the milestone the same day.

## Known limits

- **Late pattern writes** (0.55% of map frames): bands may be a frame behind
  a tile animation in lines 0-45.
- **Unrecorded paths** (vehicles, ice, the road, both wraps) are covered by
  state probes only; milestone 11's map rewrite will be guarded by them.
- **Spaceship:** repeated stars at the right of the centre are the VDP's own
  wrap, not an error; on some routes the last take-off frames draw map bands
  while a partial top row of the centre already shows stars (at most 7
  lines, 3 frames; Inference).
