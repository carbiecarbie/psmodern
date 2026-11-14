# How it works: the world map and the towns

This chapter explains how the walking maps work: how the world map and the
towns are stored, how the picture follows the party, how the game notices that
the party has stopped on a door or a doorway, and how the optional widescreen
view reads all of it without changing the game. It builds on
[the Master System](master-system.md), which explains tiles, the name table and
scrolling.

**What runs where.** Nothing in this chapter is rewritten yet. The map code
runs on the virtual Z80 exactly as on the cartridge; the rewrite is a later
milestone. Widescreen only watches. Like the dungeon automap
([dungeons](dungeons.md#how-the-automap-reads-this)) it never changes what the
original code does.

**Evidence.** The first parts are labelled the way
[the documentation rules](../AGENTS.md#evidence-labels) ask. Most of what is
said about the code comes from a read-only study of the US Rev-A cartridge made
for milestone 10, backed by running every recording of the project (ten US and
eight Brazilian, called the census below). Addresses are the US Rev-A ones. The
two cartridges share this code (the comparison in
[design, section 5.5](../design.md#55-languages)); the census ran on both.

## How the maps are stored

**ROM static.** A map is a grid of screens, and a screen is a grid of
metatiles.
- A **metatile** is a block of 2 by 2 background tiles, 16 by 16 pixels. The
  game stores it as four name-table words (which tile, which palette, flipped
  or not), read from a table of 256 metatiles in a bank of the cartridge.
- A **screen** is 16 by 12 metatiles, 256 by 192 pixels, exactly one picture of
  the Master System. It is stored run-length coded (a byte repeated a number of
  times costs two bytes), and `01:723D` expands it into one byte per metatile.
- An **area** is a grid of 8 by 8 screens, 2048 by 1536 pixels, with a table of
  screen pointers. The table has 9 by 9 entries, and its ninth row and ninth
  column repeat the first ones. Three bytes of work RAM name the current
  area: the table's address (`$C260`), the bank holding the table and the
  screens (`$C262`) and the bank of the metatiles (`$C263`). The map load
  `00:0B6A` sets them from an area record (there are 11 areas), looked up
  from the player's location.

The tests of milestone 10 read all 11 areas of both cartridges: every screen
expands to exactly 192 metatiles and the repeated row and column are equal
(**ROM run**).

**The worlds are tori.** **ROM static.** The repeated ninth row and column mean
that walking off the east edge of a world map brings you to the west edge, and
north to south. There is no border to hit. The four overworlds (areas 0 to 3)
are worlds of this kind. The census never walked far enough to cross an edge,
so wrapping is read from the tables and the camera code, not observed.

**Towns share sheets.** **ROM static.** A town uses the same structure but
does not fill it. The seven towns-and-villages areas (4 to 10) each hold a
sheet of 8 by 8 screens with one to five towns on it. Screens that belong to no
town are not empty: each is one uniform "filler" metatile repeated over the
screen, and the filler is not the same on every sheet. That is what you see
beyond a town's edge in the original, because the game never stops the camera
(below).

**Townspeople are part of the picture.** **ROM static.** The object table that
holds the party's sprites (`01:576A`, eight slots in `$C800`-`$C8FF`) holds
only the party, or one vehicle, on a walking map. Townspeople, doors and
shops are background tiles of the map: they do not walk, they do not scroll
separately, and a "person" is a tile pattern drawn in the map like a wall. That
is why a wider view can show them without any rule of their own. Whether all
of them are tiles in every town was read from the code, not played in every
town.

## The camera and the scroll

**ROM static.** The party is fixed on the screen: the leader stands at 128
pixels from the left and 96 from the top (`01:599F`), and the map moves behind
it. The camera is the world position of the top-left pixel of the picture,
kept as a position in the screen it falls in: pixels in `$C301` (across) and
`$C305` (down), the screen number in `$C302` and `$C306`. A step on foot takes 16
frames of one pixel each, so 16 pixels, one metatile; in a vehicle a step takes
8 frames of two pixels. `01:7143` is the step controller that does this and
also checks the terrain.

**It never stops.** **ROM static.** Nothing clamps the camera. The party is
always at the same place on the screen and the map slides, so near an edge the
screen simply shows what lies beyond: the other side of a world, or the
filler of a town sheet.

**The picture is a window on a small name table.** **Hardware documentation.**
The background is a name table of 32 by 28 tiles, 256 by 224 pixels, that the
video chip scrolls with two registers (8 across, 9 down) and wraps around. The
screen shows 192 of its 224 lines.

The game keeps the two registers in shadows, `$C300` across and `$C304` down,
written to the chip once a frame in the vertical blank (`00:0235`). The
horizontal shadow is minus the camera's low byte: the chip scrolls the table
to the left when the camera moves right. **ROM run.** In the census the
registers changed only once a frame, at line 194 (after the picture), so a
picture is always drawn with one fixed pair.

**Streaming.** **ROM static.** The table holds one screen, not the whole map, so
as the camera moves the game rewrites the part that scrolls into view. A column
producer (`01:72A6`) and a row producer (`01:733A`) prepare the new column or
row in a mirror of the name table in work RAM; the vertical blank uploads that
strip to video memory (`01:73D0`). A full-screen producer (`01:744B`) draws the
whole picture when a map is loaded. For this the game keeps a window of work
RAM, 4 slots of 256 metatile numbers (`$CC00`-`$CFFF`), that `01:723D` fills
with the 2 by 2 screens around the camera.

**The blank left column.** **ROM static.** The map load sets video register 0
to 26h (`00:0C8E`). One effect is that the first 8 pixels of the picture show
the backdrop colour instead of the map. **ROM run.** The leftmost column of the
name table is always stale in the census. **Inference.** The name table has
exactly 32 columns, which is exactly one picture wide, so there is no spare
column to prepare off screen. The game blanks the leftmost 8 pixels to hide the
column that is being refilled. If this is right, the original shows 248 pixels
of map, not 256. The census supports it (the column is stale in every frame);
nothing proves the reason, which is kept as an oddity in
[original quirks](../original-quirks.md#the-original-shows-248-pixels-of-map).

**Late pattern changes.** **ROM run.** Almost nothing changes in the picture
while it is drawn on the map. The exception is a tile animator that rewrites
a few tile patterns in the first 46 lines in about one frame in 180 (0.55 per
cent of the map frames), so an animated tile is drawn with the new pattern
halfway down the picture in those frames.

## Stops, entrances and transfers

**ROM static.** The game looks at the map only when a step ends, and `01:77AC`
does it. It looks the party's metatile up among the entrance records of the
area (6 bytes each, in bank `03`, loaded into `$C2D9` by the map load, in
metatile coordinates of the world). A record gives a destination and a kind:
8 is a transfer to another map (town to world, world to town), 0Ah is a
dungeon, 0Ch is an interaction scene (a shop, a house, a conversation). On a
hit it sets the game mode (`$C202`), and the rest runs from the main loop:
`00:0B41` switches to the interaction mode, which fades the map out and
draws the first-person scene screen. Shops, houses and conversations are not
windows over the scrolling map: the game fades the map out and draws a new,
fixed screen, 256 pixels wide, and fades back in. A transfer between maps
(`01:7877`, `01:787B`) rewrites the camera and the area while the old map is
still fading, which matters below.

## How widescreen reads this

**Maintainer decision.** Widescreen is an option, off by default (see
[the controls](../player-controls.md#options-widescreen)). When it is on, the
world map, the towns and the spaceship flight are shown 344 by 192 pixels, 16:9 in square pixels: the
original 256 columns in the middle, 44 more on each side, drawn from the map
with the cartridge's own tiles and palette (in space, from the stars). Everything else keeps 256 pixels,
centred. When it is off, in recording mode, and with `--no-routines`, nothing
changes at all.

**Copies after each frame.** The virtual video chip draws the picture while the
game runs, so the extra columns are not drawn by it; PSModern copies what the
chip drew from and draws the sides itself. After each frame the frame driver
copies the video memory, the palette, the registers and a few work-RAM bytes
(the camera, the area, the window of metatiles), about 17 KiB, and pairs that
copy with the next picture. The copy is the state line 0 of that picture was
drawn with, give or take one instruction (the machine stops on an instruction
boundary). Copies only read: the machine never sees the option, and the tests
check that the machine is identical every frame with the option on and off
(**ROM run**, all 18 recordings). With the option off, nothing is copied.

**A held camera.** The camera in work RAM is not always the camera of the
picture just shown: the step controller moves it for the next frame, so it is
equal in 89 per cent of the census's map frames and one step ahead in the
rest; and a transfer rewrites it during the fade of the old map, while the
picture still shows the old one (**ROM run**). So widescreen keeps its own
camera. It takes work RAM's camera only on a consistent frame of the map or
the road, one where the registers equal their shadows and the shadow is minus
the camera's low byte, then follows only the changes of the two scroll
registers from frame to frame (across modulo 2048, down modulo 1536) and never
reads work RAM again. It lets go of the camera when a change is bigger than one
vehicle step (2 pixels), when the display is off, when register 0 is not 26h,
when the game leaves the family of map modes, or at a power cycle or ROM swap.
It then shows black bars on both sides until the next consistent frame
(**Inference**: this is a conservative rule; the tests show it matches the
real name table in every checkable census frame, except 23 frames where the
game was in the middle of refilling the window of metatiles).

**Which screens widen.** **Maintainer decision.** The frames that widen are
those of game modes 8 (map load), 9 (the map), 0Ah and 0Bh (dungeon load, while
the map is still fading), 0Ch (interaction load), 0Eh and 0Fh (the road), with
register 0 at 26h, the display on and a held camera. The spaceship flight
(modes 4 and 5) widens by its own rule, described in [the spaceship flight](#the-spaceship-flight) below
(maintainer decision, 2026-11-14, after his play-test; it was first left out).
Battles, dialogues, scenes and dungeons do not widen: they are drawn at 256
pixels. The frames that are not widened show the original 256
pixels, centred, between black bars. Fades are widened too: the palette is
current, so the bands fade with the map.

**The bands.** The pictures of the two sides, and the leftmost 8 pixels of the
middle, are drawn from the map with their own small background decoder: world
position, then metatile, name word, tile pattern, palette colour. It is a copy
of the chip's background rules (the tests prove it equal to the chip's own
picture), kept apart so the verified chip stays untouched. Bands never draw
sprites: the party or the vehicle is always near the middle (**Inference**,
from the study). A cell that the game has edited, such as ice the digger has
broken, lives in the window of metatiles in work RAM rather than the cartridge;
the renderer prefers the window when it covers the cell and its page agrees with
the held camera.

**Past a town's edge.** **Maintainer decision.** The bands show what the
cartridge has: past the edge of a town, the filler screens of the sheet, and
past the edge of a world, the other side. There is no clamp and no black. The
wider view therefore also shows nearby towns on the same sheet when they are
close enough, because they share a sheet.

**The left strip.** **Maintainer decision.** The leftmost 8 pixels, which the
original blanks, are drawn from the map in widescreen, so the picture has no
black stripe inside it; with the option off they stay blank as on the cartridge.

**The window.** With the option on, the whole game, not only the map, uses the
344-pixel window, so it does not change size while you play; the start screen
and the screens that are not widened show 256 pixels in the middle. The
Options window, the Controls and Language screens, the dungeon map and the
speed mark stay 256 pixels wide in the middle, over bands that stay still.

## The spaceship flight

**ROM static.** Flying between planets is a scene, not a place on the map. A
loader (`00:0A5C`, game mode 4) prepares it and a second routine
(`00:086F`, mode 5) plays it; the game gets there only when you leave an
interaction scene such as a spaceport, never by walking on a map. The scene
ends by loading a map again (mode 8) at the destination.

**ROM run.** The study replayed eight routes on both cartridges (not played
by hand). The scene has four parts: take-off, drawn over the map you left;
space; descent, drawn over the map of the destination; and then the fade of
the map load, which keeps the camera of the descent. Counting each route,
116 frames could not be widened, 606 show a map and 860 show space.

**How each part is drawn.** **Inference**, checked by the tests below. In
take-off and descent the screen is the map, moved by the scene itself, so
PSModern works out the camera from work RAM and uses it only when the picture's
name table matches the map there. In space the picture is a field of stars on the name table,
scrolled up and down only. Widescreen draws the sides as the video chip
itself would draw that table past 256 pixels: it repeats around, so the stars
at the right of the middle are the same as those at the left of it. That is
the chip's rule, not new art. A frame that fits neither case stays 256 pixels
between black bars.

**Tests.** `rom_us_wide_view_ship`, `rom_br_wide_view_ship`, the eight-route
`_ship_routes` and the unit test `wrapped_against_decoder`, which proves the
wrapped drawing equals the decoder inside the picture. The flight has been
checked on replayed routes only; how it looks is for the maintainer's play-test.
