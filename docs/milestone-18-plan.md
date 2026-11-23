# Milestone 18 plan - Television pixel shape

**Tier B. Status: started on 2026-11-23. The Options mockup awaits approval.**
The maintainer chose the subject and approved the four decisions below on
2026-11-23 and started the milestone that day. Only the host's
presentation changes: no game routine, no virtual hardware and no test
referee is touched.

## Goal

An option shows the picture with the pixel shape a television gave it. The
console's pixels were not square: on NTSC and on Brazil's PAL-M sets each
pixel was 8/7 as wide as it was tall (Hardware documentation: SMS Power,
"Pixel Aspect Ratio"; the pixel clock is 3/2 of the colour subcarrier and the
280 x 240 signal fills a 4:3 screen). PSModern shows square pixels, so the
256 x 192 picture is about 12.5% narrower than both cartridges looked on
their consoles. With the option on, 256 x 192 is shown about 292.6 x 192.

Out of scope: the border the console drew around the picture, overscan,
scanlines and any other television effect; European PAL's different shape
(neither supported cartridge is European); non-integer vertical scaling.

## Key decisions

All four: Maintainer decision, 2026-11-23.

1. **Off by default.** Square pixels stay the default, as every option starts
   off and release 1.3.0 looks today.
2. **An eighth Options row,** saved for the installation as Sound and
   Language are, applied at once. The Options list is full at seven rows
   (milestone 17), so the page needs a new layout: a mockup comes first, as
   in milestones 12 and 17, with a scrolling list (as Controls has) or a
   two-line explanation window.
3. **With Widescreen on, the height is kept.** 344 television-shaped columns
   are about 2.05:1, wider than a 16:9 screen, so the picture shows as many
   of the 44 extra columns a side as the screen fits (40 at 1920 x 1080, 32
   at 2560 x 1440, 24 at 3840 x 2160, all 44 on a phone) instead of dropping
   a scale step.
4. **Columns are blended, rows are not.** Rows keep today's whole-number
   scale and stay sharp. Each source column covers 8/7 of that scale in
   output pixels; an output pixel that straddles two columns takes their
   colours in proportion to the cover. Where the scale is a multiple of 7
   (2560 x 1440 and above) nothing is blended. Unblended columns of uneven
   width were set aside because moving scenery would shimmer.

## Tasks

- [ ] Mockup of the Options page with the eighth row in both languages
      (cartridge font, uppercase, no accents), outside the repository. The
      maintainer approves the layout and the labels before any production
      code.
- [ ] A pure sizing function beside `viewport` (`src/app/host.h`): output
      size, canvas, Widescreen and pixel shape in; scale, picture rectangle
      and shown columns out. With the option off it returns what `viewport`
      returns today.
- [ ] A pure column-stretch function (decision 4) and its use in `present`
      (`src/app/sdl_host.cpp`). The 256-column overlays (Options, Controls,
      the automap, the start screen) stretch with the picture, as on a
      television. If the stretched texture cannot be created, the picture
      falls back to square pixels with a console message.
- [ ] Window sizing: applying the option resizes a normal window to the new
      shape at its current scale, lowered to fit the display; maximized and
      fullscreen windows change only the picture (`apply_canvas`).
- [ ] The preference: read, saved at once and reported on failure as the
      Sound row is; a missing or damaged file means square pixels.
- [ ] The Options row, its explanation and state lines in English and
      Portuguese, per the approved mockup.
- [ ] Android: `touch_layout` places the reshaped picture in both
      orientations; the cross and the buttons keep their size, as they do
      with Widescreen (milestone 16).
- [ ] Documentation in the same change: a pixel-shape section in
      [the console chapter](how-it-works/master-system.md),
      [player controls](player-controls.md), [development](development.md)
      and both READMEs if they list the options.

## Acceptance criteria

- With the option off, every picture rectangle and every presented frame
  equals release 1.3.0's; a test proves it over the sizes the existing
  viewport tests use, narrow and wide.
- With it on, tests fix the rectangle and shown columns at 1366 x 768,
  1920 x 1080, 2560 x 1440, 3840 x 2160 and the phone's two orientations,
  narrow and wide; every source row fills whole output rows; at scale 7 every
  column is exactly 8 output pixels; the stretch never changes a colour inside
  a column of flat colour.
- The game never sees the option: recordings, lockstep runs and saves are
  unchanged, and the complete suite passes once after the final production
  build ([protocol](../tools/AGENTS.md)).
- One maintainer play-test ([preparation](playtests.md)), only for what tests
  cannot judge: how the world map looks while scrolling in a window,
  maximized and fullscreen, with Widescreen off and on, and the picture and
  touch controls on the phone in both orientations.
- At closure, with the maintainer's approval: [design](design.md) (the
  options list and the widescreen width in section 5.4), the root `AGENTS.md`
  options list, [project status](project-status.md) (the known gap goes),
  [roadmap](roadmap.md) and [project history](project-history.md).
