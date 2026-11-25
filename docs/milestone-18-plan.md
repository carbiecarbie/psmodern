# Milestone 18 plan - Television pixel shape

**Tier B. Status: withdrawn on 2026-11-24 (Maintainer decision), before any
production code.** The maintainer chose the subject and started the milestone
on 2026-11-23, saw the Options mockup and withdrew it the next day. Nothing in
the program changed: the picture keeps square pixels.

## What was proposed

An option to show the picture with the pixel shape a television gave it. The
console's pixels were not square: on NTSC and on Brazil's PAL-M sets each
pixel was 8/7 as wide as it was tall (Hardware documentation: SMS Power,
"Pixel Aspect Ratio"; the pixel clock is 3/2 of the colour subcarrier and the
280 x 240 signal fills a 4:3 screen). PSModern shows square pixels, so the
256 x 192 picture is about 12.5% narrower than both cartridges looked on
their consoles. With the option on, 256 x 192 would have been shown about
292.6 x 192.

The plan was an eighth Options row, off by default and saved for the
installation. Rows would keep their whole-number scale; each column would
cover 8/7 of that scale, and an output pixel straddling two columns would
blend their colours. With Widescreen on, the height would be kept and the
picture would show as many of the 44 extra columns a side as the screen fits.

## Why it was withdrawn

All: Maintainer decision, 2026-11-24, on the mockup and the points below.

- **The Options page is full.** Seven rows fill it since milestone 17. The
  mockup kept seven visible and scrolled to the eighth, which undoes the
  one-page, no-scrolling layout approved in milestone 12, and its scroll
  hints on the border crowd the neighbouring rows.
- **The picture loses sharpness at the commonest size.** At 1920 x 1080 the
  scale is 5, so a column covers 5.71 output pixels and about one output
  column in six is a blend. Only a scale that is a multiple of 7
  (2560 x 1440) is exact; 3840 x 2160 uses scale 11 and blends too
  (Inference from the sizing arithmetic, high confidence).
- **It takes columns from Widescreen.** With both on, each side shows 40 of
  the 44 extra columns at 1920 x 1080, 32 at 2560 x 1440 and 24 at
  3840 x 2160.
- **The cost outweighs the gain.** Window sizing, presentation, a new
  preference, the Options layout, the Android touch layout and five documents
  would change, with a play-test over window, maximized and fullscreen, with
  and without Widescreen, and both phone orientations, for an option that
  starts off and few players would turn on.

## If it is raised again

Do not add a row. Fold it into the Widescreen row as one choice of three
(original, television shape, wide): the page stays at seven rows, and the two
shapes can no longer be on together, which removes the lost columns. The
blending at scales that are not a multiple of 7 would remain.

The mockup was kept outside the repository and is not part of it.
