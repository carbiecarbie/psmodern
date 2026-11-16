# How it works: the world map and the towns

This chapter explains how the walking maps work: how the world map and the
towns are stored, how the picture follows the party, how the game notices that
the party has stopped on a door or a doorway, and how the optional widescreen
view reads all of it without changing the game. It builds on
[the Master System](master-system.md), which explains tiles, the name table and
scrolling.

**What runs where.** The walking maps run in C++ since milestone 11, in six
groups of short pieces ("segments", each doing the work between two calls into
other code): the picture's update in the frame interrupt, the party's step and
the camera, the sprites and the object list, what the ground allows (vehicles
and ice included), entrances, transfers and special ground, and the map's
loading and the road. Each section below says which routines belong to its
group. Every piece is checked against the original on every call. A few
stretches stay on the virtual Z80: the four short stretches that run with
interrupts off (three in the map load, one in the ice breaking), the interrupt
entry and exit, the jump-table dispatchers, the graphics decompressor, the
palette fades, the battle sprites, the sound driver, the frame wait at `00:0B14`
where the host opens Options and Language, and the spaceship flight
([the flight](#the-spaceship-flight)). The widescreen view only watches. Like the
dungeon automap ([dungeons](dungeons.md#how-the-automap-reads-this)) it never
changes what the original code does.

**Evidence.** The first parts are labelled the way
[the documentation rules](../AGENTS.md#evidence-labels) ask. Most of what is
said about the code comes from a read-only study of the US Rev-A cartridge made
for milestone 10, backed by running every recording of the project (ten US and
eight Brazilian, called the census below). Milestone 11 added three US
recordings made for the map (`road`, `vehicles` and `ice`) and the sections
below say where they changed what is known. Addresses are the US Rev-A ones.
The two cartridges share this code (the comparison in
[design, section 5.5](../design.md#55-languages)); the census ran on both.

## Map display transfers

**ROM run.** Loading a map masks interrupts around the display setup and
the video address writes. The breakable ice update (`01:75CC`) selects the
video address, writes four tile bytes, advances the row address by 64 and
writes the next four. A two-ROM test checks both rows in video memory and
the machine state after interrupts are enabled again.

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
are worlds of this kind. The census never walked far enough to cross an edge;
the milestone 11 probes did, in all four directions
([walking and the camera](#walking-and-the-camera), **ROM run**).

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

## Loading a map and the road

**ROM static.** Three game modes (the byte at `$C202`) belong here. Mode 9 is
walking, run by `00:0B08` once per frame. Mode 8 loads a map, run by `00:0B6A`;
the same loader is what turns the mode into 9 when it starts, because it adds
one to whatever mode called it. The road between two towns is a pair of modes of
the same kind: 0Eh loads a leg of it (`00:0ED7`, whose call of the loader turns
0Eh into 0Fh) and 0Fh plays it (`00:0E8B`).

**What runs where.** **ROM run.** These routines run in C++, cut into short
pieces (23 entries, in `src/game/map_modes.cpp`) between the calls into code
that is still on the virtual Z80. Three short stretches of the loader and the
call that lets the host open Options and Language (`00:0B14`, the call of the
object engine in the walking loop) stay on the Z80 on purpose; the end of this
section says which and why.

**The walking loop.** **ROM static.** Each frame `00:0B08` does these things, in
order. It waits out a pause (`$C212` nonzero). It waits for the next frame. It
runs the object engine (`01:576A`) and then the check of the tile the party
stands on (`01:77AC`). If no step is in progress (`$C265` is zero) and a random
fight is pending (`$C2D2` nonzero), it clears the pending byte and rolls the
fight (`01:5F63`). A fight that starts, or button 1 or 2 *held* while no step is
in progress, ends the walking mode: the loop stores FFh (fight) or 0 (button) in
`$C29D`, sets mode 0Ch, copies the leader's object type (`$C810`) to `$C2D7` and
clears the animator block (`$C26F`, 24 bytes) and the eight object slots
(`$C800`-`$C8FF`). Anything else returns, and the main loop calls it again next
frame.

If a frame ends inside that check, the game can resume after the button
test. It reads the movement flag again before allowing the menu to open.
Both clearing loops can pause between bytes and continue with the remaining
count. **ROM static:** `00:0B3B`, `00:0B47`, `00:0B5A`, `00:0B5C` and
`00:0B67`. **ROM run:** both releases exercise a moving party and a
stationary party at four distances from a frame boundary.

**Loading a map, step by step.** **ROM static.** `00:0B6A` does this on every
path:
1. Fades the picture out and switches the display off (`00:003E`).
2. Adds one to the mode, clears two flags (`$C2D6`, `$C315`) and sets `$C2D5`
   to 1.
3. Chooses the tile sheet by the area number (`$C308`): areas 0 to 3 (the four
   worlds) come from bank 1Dh, the others from bank 16h, and go to video memory
   through the tile decompressor (still on the Z80).
4. Builds the object slots (`00:0CA6`, below), runs the object engine once, then
   waits four frames, uploading the party's frame tiles (`01:61F5`) with
   interrupts off after each wait.
5. Sets the scroll shadows from the camera: `$C300` is minus the low byte of the
   camera across (`$C301`) and `$C304` is the low byte of the camera down
   (`$C305`).
6. Looks the location (`$C309`) up in a table of 24 bytes in bank 0, one for each
   location of the game, to get the area number, and stores it in `$C308`.
7. Reads that area's record from a table of 14-byte records (11 areas) and
   copies it out: the pointer and banks of the area's screens
   (`$C260`-`$C263`), six bytes into the animator block (each next to a fixed
   byte, `$C26F`-`$C286`), 17 bytes through a pointer in the record to `$C240`,
   then 15 more there (on foot: 13 from a fixed table and 2 from the record; in
   a vehicle or the ship: 15 from another fixed table), and the pointer to the
   area's entrance records (`$C2D9`).
8. Builds the screen window (`01:723D`) and the whole picture (`01:744B`), then
   asks `01:7764` for the attribute of the tile the party stands on; its upper
   nibble goes to `$C29E`, the "interaction type" the later checks read.
9. Asks for the area's tune, writes video register 0 (`00:0C8E`, see the blank
   left column above), clears the sprite tables (`00:02FD`), waits a frame and
   fades the picture in.

**Which tune.** **ROM static.** `00:0C97` is the game's "play this tune unless it
is already playing": it compares the request with the last one kept in `$C2F4`,
and only a different request is stored in the sound request byte (`$C004`) and
in `$C2F4`. The loader asks for B8h when the transport byte (`$C30E`) is 10h or
more (the ship), 8Fh for any other vehicle, and otherwise for the byte a table of
24 gives for the location. A load that lands on the tune already playing does
not restart it. Other code requests tunes through the same routine.

**ROM static; ROM run.** Two map paths clear a row of object records by
writing a zero and then copying it one position ahead, repeatedly. The
source and destination overlap on purpose. The copies at `00:0CB1` and
`01:7926` now run in short sections with the same register state, work RAM
and timing as the original at each return to the machine.

**The party on the map.** **ROM static.** `00:0CA6` clears the eight object slots
and then fills them. In a vehicle (`$C30E` nonzero) it makes one object of type 9
in slot 0. On foot it makes one object for each of four character records
(`$C400`, `$C430`, `$C420` and `$C410`, in that order) whose first byte has bit 0
set, of type 1, 3, 5 and 7, one slot (32 bytes) apart. The records are tested in
that fixed order, so the order of the party on the map follows it.

**The road.** **ROM static.** A leg of the road is an 8-byte record in a table of
six in bank 0, right after the loader's routines: the buttons to hold (one byte), how long to walk (one byte) and
two 3-byte transfer records, one applied when the leg starts and one when it
ends. `00:0ED7` copies the first two bytes to `$C2EA` and `$C2EB`, pushes the
address `00:0EF5` as a return target, and applies the starting record with
`01:787B`. That routine drops the return address of its own call and goes back to
the pushed target, so the loader (`00:0B6A`) runs next, then a clean-up of the
animator block. `00:0E8B` then runs each frame like the walking loop: it handles
the pause, waits a frame, replaces the held buttons by `$C2EA` (the party walks
by itself), runs the object engine, and counts `$C2EB` down in every frame in
which no step is in progress. At zero it sets mode 8, clears `$C265` and the
three road bytes, and applies the ending record. **A press of button 1 or 2
skips all that**: the road looks at new presses (the walking loop looks at held
buttons), and a press goes straight to the ending, before the object engine
runs.

**Evidence.** **ROM run.** The rewritten routines ran against the original in
lockstep on the US recordings opening, first-battle, later-town, road, vehicles
and ice (the road only in `road`): no difference in any frame, picture, memory
or cycle. Map loading also ran at all 24 locations on both cartridges, on foot,
and in each vehicle and the ship at one of them, and the six legs of the road
ran on both cartridges from a town save, with the routines on and off compared
every frame. Every path of every entry, with all four characters present and
absent, was run from a bare machine against the original with varied banks,
refresh counter and stack.

**What stays on the Z80.** **ROM static.** The loader switches the display off
and on with interrupts disabled in three places: around `00:003E`
(`00:0B6D`-`00:0B71`), around the party frame upload (`00:0BB4`-`00:0BB8`, four
times) and around the write to video register 0 (`00:0C91`-`00:0C93`). Those
stretches, the instruction after each switch-on (it runs before an interrupt can
be taken) and the call at `00:0B14` stay on the Z80. The sprite-table clearing
now also has an entry at `00:02FD`: it runs the setup and first fill when the
interrupt window permits, and otherwise falls back to the Z80. The second fill
(191 bytes) uses bounded copy slices at `00:030E`. Earlier recordings found the interrupt due in 11 of
18 road arrivals and 9 of 20 first-battle arrivals (**ROM run**).

The 255-byte clearing of the object slots (`00:0CAC`-`00:0CB1`, about 5,400
cycles) stays on the Z80 for the same reason: inside a piece it made 9 of 71
calls fall back (**ROM run**, census of the 18 recordings).

## How the picture is updated each frame

**ROM static.** Once a frame, when the video chip has finished drawing the
picture, it interrupts the game, and the game uses that pause to send the
picture's changes to the chip. On a walking map this work is `00:0235`. It
does, in this order:

1. **The scroll.** It writes the two scroll shadows (`$C300` across, `$C304`
   down) to the chip's scroll registers 8 and 9.
2. **The sprites** (`01:588B`). It copies the sprite table kept in work RAM to
   the chip: 64 heights from `$C900`, then 128 bytes of positions and tile
   numbers from `$C980`. The copy is a long straight run of single-byte
   transfers with no loop, so it always takes the same time (3,258 cycles,
   about a twentieth of a frame). The same run, entered further in, is used
   elsewhere to send 128, 64 or 32 bytes (`01:589E`, `01:591E`, `01:595E`).
   The 64-byte entry `01:591E` is rendered inside the pieces that jump to it,
   but not as an entry of its own: when a piece that included it had to run on
   the Z80 (a frame boundary inside), the 64 transfers that follow always held
   the boundary too, and the machine cannot end a piece in the middle of such a
   run, so that stretch stays on the Z80 (**ROM run**, 3 calls in the recordings).
3. **A vehicle's or the ship's colours** (`01:7C18`). On foot nothing
   happens. On the ship (transport `$C30E` = 10h or 11h) the 32 colours of
   `$C220` are sent every frame. In a vehicle (4, 8 or 0Ch) three colours
   rotate every fourth frame (`$C310` counts down, `$C30F` is the step), each
   step asking the sound driver for a sound (`$C004`); the vehicle numbered 8
   does it always, the other two only while `$C265` is set.
4. **The party's poses** (`01:61F5`). Each of the first four objects (the
   party) has a wanted pose and the pose last sent. For the first one whose
   two differ, the new pose's tile pictures are copied from the cartridge
   into the chip, by one of five copy routines chosen by the object's kind
   (`01:6229`, `01:6246`, `01:6263` for 192 bytes, `01:6280` for 128 bytes,
   `01:6293` for 512 bytes from another bank, the last only in game modes 5
   and 9). Only one object is served per frame (see below).
5. **The new strip of map** (`01:73D0`). When a step has prepared a new
   column or row of the name table in the mirror at `$D000` (`$C264` says
   which), it is sent now: a column is 28 separate two-byte writes, a row one
   run of 64 bytes.
6. **The animated tiles** (`01:62BC`). Six animators each keep a 4-byte
   record at `$C26F`-`$C286`: on or off, a delay, a countdown and a step from 0 to 7. When an animator's countdown runs out it
   moves one step (backwards when `$C2E9` is 4 or more) and copies that step's
   tile pictures, from 3 to 16 rows of 32 bytes, into the chip (`01:630A`).

Then the controller is read and the sound driver ticks. While a map loads,
the interrupt runs `00:029F` instead: the scroll, the sprites, the 32 colours,
and the whole name-table mirror (1,792 bytes) at once.

**One pose and one animation per frame.** **ROM static.** `01:61F5` leaves as
soon as it has sent one object's pose, and `01:630A` leaves `01:62BC`
altogether when an animator has fired, so the animators after it wait for the
next frame. **ROM run.** A probe with two animators due in the same frame shows
the second one untouched (`tests/map_video_tests.cpp`). This keeps each frame's
work short, but not always short enough: see the next paragraph.

**The work can run past the frame.** **ROM run.** The longest animations (16
rows) and the loading copy of `00:029F` (about 46,600 cycles) are still
running when the next frame begins, which is why an animated tile can change
partway down the picture (see "Late pattern changes" above).

**What runs where.** **ROM run.** Since milestone 11 all of this runs in C++:
every routine above, checked call by call against the original on every
recording. The two long loops (the loading copy and the animation copy) are
cut into short pieces so that only a few of them, the ones that cross the
frame's end, run on the original code.

**How far the check reaches.** **ROM static, ROM run.** The order of the
writes to the video chip inside each piece, and the cycle each one is made at,
were set by reading the C++ against the original code, and are backed by the
tests of the helper code that sends the bytes to the chip, which check its
timing against the real Z80 instructions. The call-by-call check compares what
is left afterwards: the chip's memory, registers and colours, the sound chip's
writes with their cycles, all other memory, and the total cycles the call took.
It does not keep a list of every access to the chip's ports. If two writes were
swapped inside the pause after the picture (the vertical blank), and the total
time stayed the same, the check would not notice. The chip cannot show the
difference there, because it is not drawing, but it is a limit of the evidence,
not something the tests proved.

## Walking and the camera

**What runs where.** Since milestone 11 the step (`01:7143`), the window of
metatiles (`01:723D`), the column and row strips (`01:72A6`, `01:733A`) and the
whole-screen drawing (`01:744B`, after its first instruction) run in C++. Each
is cut into short pieces at the places the original calls other code or loops,
and every piece is checked against the original as it runs. The terrain
check `01:74E4` they call belongs to the next section.

**One frame of a step.** **ROM static.** The party's walk handler calls
`01:7143` once a frame for the leader (`01:5A9A`; a vehicle's handler does
the same at `01:5A76`). It keeps a frame counter (`$C265`). While the counter has frames
left, the step goes on in the direction it started in (`$C264`: 1 up, 2 down,
4 left, 8 right). When it runs out, the step reads the pad (`$C204`) and starts
a new one: 16 frames on foot, 8 in a vehicle (`$C30E` not zero). Each frame
moves the camera one pixel on foot and two in a vehicle, so a step is always
16 pixels, one metatile.

**Choosing a direction.** **ROM static.** A new step tries the held
directions in a fixed order: up, down, left, right. Each one asks the terrain
check whether the step is allowed. A blocked direction passes on to the next
held one. If none is free, or nothing is held, the party stands: the counter
and the pending strip are cleared and the sound request byte (`$C004`) gets
D6h (`01:722F`). This happens every frame the party stands still; what the
sound driver does with D6h was not studied (**Unknown**). A step already under
way never asks the terrain again.

**Moving the camera.** **ROM static.** Going up or down, the step moves two
values together: the camera's Y inside its screen (`$C305`, 0 to 191, with the
screen row in `$C306`) and the vertical scroll shadow (`$C304`), which stays
inside the 224 lines of the name table. Going left or right it moves the
camera's X (`$C301`, with the screen column in `$C302`) and sets the
horizontal shadow (`$C300`) to minus its low byte. The screen numbers keep
only their low three bits, so screen 7 is followed by screen 0: this is how a
world wraps around. Every move also records the direction as the pending
strip and sets `$C2D2` to FFh. The map loop's encounter check wants that byte
set and the counter back at zero, which happens once, after the last frame of
a step; a standing frame clears the byte again. So each finished step gets
one encounter check (see
[random numbers](random-numbers.md#the-encounter-check-on-the-map)).

**ROM run.** The map probes of milestone 11 walked across the wrap in all four
directions, on foot and in a vehicle, from a real town frame with the camera
moved next to it. The camera went from screen 7 to screen 0 and back, and
every frame was identical with the rewritten code on and off.

**The window of metatiles.** **ROM static.** When a move changes the screen
number on its axis, the step refills the window at once (`01:723D`). The
window holds the four screens with the camera's screen at the top left: its
own, the one to the right, the one below, and the one below and right, in
`$CC00`, `$CD00`, `$CE00` and `$CF00`. Each screen is found in the area's
table (`$C260`, nine entries of two bytes per row of screens, in the bank
`$C262`) and decoded by `01:728A`. The code is simple: a byte 0 ends the
screen, a byte from 1 to 127 repeats the next byte that many times, and a
byte from 128 up copies the following (byte minus 128) bytes as they are.

**Strips.** **ROM static.** The name-table mirror at `$D000` is 28 rows of
32 tile words, a copy of the name table in video memory. After a move the
step jumps to one strip producer. A vertical move builds a row (`01:733A`):
16 metatiles across, at the top of the picture going up, or at the line 192
pixels lower (inside the 224 lines) going down. A horizontal move builds a
column (`01:72A6`): 14 metatiles down, at the column under the camera's left
edge, taken from the screen to the right when moving right, and one tile
further right when moving left between two tiles. A metatile's four tile
words come from the metatile sheet (bank `$C263`, eight bytes per metatile
from 8000h). The frame interrupt then copies that one row or column to video
memory (`01:73D0`). After a map load, `01:744B` builds all 12 rows of 16
metatiles the same way, then waits for the next frame.

**Pieces and timing.** **ROM run.** These routines often run when a frame
ends: a measurement on three recordings found the frame boundary inside
three column or row calls out of four. The C++ pieces are therefore short,
one metatile per piece for the strips and one run of the decoder per piece
for the window, and the column's preparation is cut in three, so that a
piece rarely contains the boundary (a piece that would contain it runs on
the original instead). In every recording, at least 91 per cent of the calls
of each piece called 20 times or more run in C++.

## Sprites and the object list

**ROM static.** Everything that moves on a walking map is an **object**, and
the objects live in eight **slots** of 20h bytes each, `$C800`-`$C8FF`. The
object engine `01:576A` runs once per frame, right after the frame wait of the
map loop; it is also called by the battle and the scenes, which keep their own
objects in the same slots. A slot's first byte is its **type** (the low seven
bits; zero means the slot is empty). Byte 1 says which sprite layout it shows,
byte 2 is its vertical position on the screen and byte 4 its horizontal one;
the other bytes are the object's own business.

**What the engine does each frame.** **ROM static.** It works in three passes.
1. It empties its list (eight places for a pair of numbers: a sort key and a
   slot number) and puts the two sprite cursors back at the start of the
   sprite tables, `$C900` (vertical) and `$C980` (horizontal and tile).
2. It goes through the slots in order. For each non-empty slot it calls the
   **handler of the slot's type**, found by a jump table in the same bank. The
   handler does whatever the object does that frame and answers in A: zero
   means "do not draw me", anything else means "draw me", and the engine
   writes the slot's vertical position (the key) and the slot's number into
   the next place of the list.
3. It sorts the list, then draws the listed slots in list order with
   `01:5853` and closes the vertical table with the end marker D0h, which tells
   the video chip that no more sprites follow.

**Drawing one object.** **ROM static.** `01:5853` takes the slot's layout
number, looks the layout up in a table of layouts, and copies it into the two
sprite tables: one vertical position per sprite (the layout's offset plus the
slot's vertical position), then one horizontal position and one tile number
per sprite (the layout's offset plus the slot's horizontal position). A layout
starts with its number of sprites. The tables are in the cartridge: slots 0-3
use the table in bank 3, slots 4-7 the one in bank 21, so the engine switches
the third memory window to the right bank first. The upload to the video
memory is a separate job done in the frame interrupt (`01:588B`).

**The party's objects.** **ROM static.** On a walking map the slots hold the
party and, when there is one, a vehicle; townspeople are not objects (see
above). Each party member has two types: a **setup** type that runs once and a
**walking** type that runs every frame afterwards.
- The setup types (`01:599F`, `01:59E0`, `01:59F1`, `01:5A02`, one per member)
  clear the slot with `01:5818` (it clears the 31 bytes after the type byte),
  turn the type into its walking twin by adding one, put the object at 60h
  from the top and at 80h or 84h from the left (84h when the two low bits of
  `$C2EA` are not zero), store the member's sprite layout and give the leader
  (slot 0) two flag bits the followers do not get. They answer "draw me".
- The walking types (`01:59D4`, `01:59E5`, `01:59F6`, `01:5A07`) give the
  shared walker `01:5A98` two small tables (which frame to show for each
  direction) and answer "draw me". The walker for the leader first runs the
  party's step controller `01:7143` (the camera moves there, see "Walking and
  the camera"). It then picks the direction from the move bits `$C264` and the
  step counter `$C265`: while the party walks, the frame changes every eighth
  frame, cycling through four poses; when it stops, the object shows the
  standing pose of its last direction. A follower works from the slot before
  it: it takes that slot's previous direction as its own. When bit 0 of its
  flag byte is set, `01:5B5C` moves it one pixel along its own direction and
  then, through the same helper `01:5B6B`, one pixel in the opposite of the
  leader's direction. **Inference.** The second move is what makes a follower
  stay in place on the screen while the map slides under it, because the
  leader itself never moves on the screen. On the road (`$C2E9` not zero) the
  animation keeps running without input.
- The vehicle has the same shape: a setup type `01:5A4F` (position 60h, 80h,
  layout 5, frame from `$C30E` minus 4) and a walking type `01:5A70`, which
  runs the step controller and then chooses the frame from the lowest set bit
  of `$C264` and the vehicle kind `$C30E` (`01:5A76`).

**What runs where.** **ROM run.** All of this runs in C++ on a walking map,
and also whenever a battle or a scene calls the engine; every call is checked
against the original (milestone 11). The engine is cut into short pieces at its
natural seams: the handler call of each slot, the end of each pass, and every
128 cycles of a long sprite layout. A piece only runs in C++ when the frame
boundary does not fall inside it; in the recordings every piece that runs
at least twenty times ran in C++ on at least 92 per cent of its calls, and the
others ran on the Z80 and gave the same result.
The handler dispatcher, the step controller and the battle object
types stay on the Z80 in this milestone and are called exactly as before.

## What the ground allows

**What runs where.** Everything in this section is rewritten in C++
except one stretch of the ice breaking that stays on
the virtual Z80 because it works with interrupts off (`01:75CC`-`01:75E4`).
The C++ is a set of 28 short pieces ("segments"), each doing the work between
two calls into other code; the tests ran every piece against the original on
every call. The routines are `01:7764`, `01:74E4`, `01:756C`, `01:7602`,
`01:761F`, `01:7656`, `01:76C1` and `01:7732`.

**Evidence.** Labels are those of the rest of the chapter. "ROM run" here is
the US recordings `vehicles` and `ice` (made for this milestone) and, for
what no recording reaches, the original and the C++ run side by side on
thousands of states built by the tests, on both cartridges. Those states
reach every instruction of this code.

**Reading the ground at a point.** **ROM static.** The game never asks what is
at a pixel. It asks `01:7764` for one of about twenty "probe points". A table
(`01:7A8D`, two bytes per point) gives a vertical and a horizontal offset from
the camera, so a point is a place around the party measured in pixels. The
lookup adds the vertical offset to the camera's vertical pixel byte; past 192
(C0h) it moves on to the next pair of screens of the window of decoded
metatiles (`$CC00`-`$CFFF`, which `01:723D` fills; see
[the camera and the scroll](#the-camera-and-the-scroll)). It adds the
horizontal offset to the camera's horizontal pixel byte, takes the metatile
number found there, keeps it in `$C2E5`, and reads that metatile's attribute
byte from a table in bank `03` (a second table, 100h bytes on, serves areas 4
and up). Bit 0 of the attribute blocks walking; bits 1 and 3 are what the
hovercraft cannot enter (bit 3 also marks an entrance, see
[Stops, entrances and transfers](#stops-entrances-and-transfers), which
reads the same attribute again, its bits 4-7 giving the kind of stop).
**ROM run.** The lookup leaves bank `03` switched into the upper slot of the
cartridge, which the C++ reproduces (see the quirks).

**Is a step blocked?** **ROM static, ROM run.** `01:74E4` is called when a step
is about to start. What it tests depends on how the party travels (`$C30E`):

- On the road (`$C2E9` not zero): nothing blocks.
- On foot: one point ahead; bit 0 decides.
- In the Land Rover (4): two points ahead; bit 0 of either blocks.
- In the Hovercraft (8): two points ahead; bits 1 and 3 of either block.
- Any other kind (the Ice Digger, 0Ch, and the ship, 10h and 11h): two points
  read with the ice probe (`01:756C`), their bit 0 results combined. When
  either probe broke ice the game asks for the ice-breaking sound (B7h into
  the sound request cell `$C004`).

**Ice.** **ROM static, ROM run.** A metatile numbered D8h to DFh is ice. When the
ice probe reads one it puts D7h (broken ice) in its place in the window,
rewrites the four tiles of the picture in the name table (video writes with
interrupts off; this stretch stays on the Z80), and then reads the attribute
of D7h, not of the ice. On areas below 4 the attribute of the ice has bit 0
set and that of the broken ice has it clear, so the step that breaks the ice
is already a step onto open ground. In the `ice` recording the probe broke
ice in 96 of its 193 reads. **ROM static.** The picture update takes the row
from the vertical scroll register's shadow (`$C304`) and wraps it at 224
lines, the 28 rows of the name table, and the column from the camera.

**The ice preset.** **ROM static, ROM run.** `01:7602` runs when a whole screen
is drawn (`01:744B`). Only on area 2 with the Ice Digger it reads four fixed
points around the party and turns any ice there into broken ice in the
window (not in the picture), so the first picture already shows the ground
around the vehicle cleared. `01:761F` is the one-point step it repeats.

**Getting in and out of a vehicle.** **ROM static.** The item-use code in bank
0 calls these when a vehicle item is used. Each tries a list of "tries", each a
small group of probe points, and takes the first that is clear; when none is,
it returns A = FFh with the zero flag clear and the caller reads "no room
here". Otherwise it moves the camera, and the two copies of it that the stop
check keeps (`$C311`, `$C313`), and returns zero.

- `01:7656` (the Land Rover and the Ice Digger, in and out): four tries of four
  points, attribute mask 0Dh. Try 0 stays where it is; try 1 moves one
  metatile (16 pixels) to the right; try 2 one metatile down; try 3 both,
  because the code adds the horizontal 16 unless the try is exactly 8 and the
  vertical 16 unless it is exactly 4 (the vertical move wraps at C0h).
- `01:76C1` (getting into the Hovercraft): eight tries of four points, mask
  0Ah; the offset of the winning try comes from a table of signed pixel
  steps (`01:7A49`). The move itself is a shared tail, `01:76FD`.
- `01:7732` (getting out of the Hovercraft): eight tries of one point each,
  starting from the party's facing (the low two bits of `$C2D7`, a copy of
  the leader's frame byte saved when a map or a scene is entered), then the
  same tail `01:76FD`.

**Why so many pieces.** The original does these jobs with calls between small
routines, and the C++ hands back to the machine at every such call. One test of
a step is therefore several pieces in a row. Each piece ends where the
original would call or return and takes the cycles the original takes, which
the tests compare on every call.

## Entrances, transfers and special ground

**Rewritten in C++ (milestone 11).** `01:77AC`, `01:7877`, `01:787B`,
`01:7908`, `01:792A` and the damage step `01:7A0C` run as C++ in pieces (22
entry points) that the machine checks against the original Z80 code on every
call; the rest of the map loop is described in the other sections.

**When the check happens.** **ROM static.** The map loop calls `01:77AC`
once per frame, after the object engine. It does nothing while the busy byte
`$C265` is non-zero (it only clears the "already checked" guard `$C2D5`). With
`$C265` zero it checks only when the guard is clear, sets the guard, and so
looks at the ground once per stop, not once per frame. It asks the terrain
code (`01:7764`) for the attribute of the point ahead of the party and keeps
the attribute's upper four bits in `$C29E` (the interaction type). Attribute
bit 3 means "an entrance may be here", bit 2 means "special ground". With a
vehicle (`$C30E` non-zero) up to three more points around the vehicle are asked
for the entrance bit as well. A stop that finds neither only copies the camera
into `$C311` and `$C313`, so that a later return (from a dungeon or a scene)
can put the same view back.

**Entrance records.** **ROM static.** The area's records (6 bytes each, in
bank `03`, ended by `FFh`, pointer `$C2D9`) say where the party is, as a row
and a column of 16-pixel cells, then a kind and three bytes of destination. The
check turns the probed point into the same row and column and walks the table.
A record matches the party's row when it is the record's own row or the row
just after it (after a page's last row, the first row of the next page), and
the party's column when it is the record's own column or the column just
after it, so each record covers a spot two cells wide and two rows tall. Kind 8 sends the party to another map, kind `0Ah` into a dungeon,
kind `0Ch` into an interaction scene (a house, a shop, a conversation); any
other kind is ignored. The scan also switches bank `03` into the third memory
slot (`$FFFF`) and leaves it there. **ROM run.** The C++ version cuts the walk
into pieces of about 512 cycles at the top of the loop (01:7844), so that a
long table never keeps the frame interrupt waiting; the original runs the walk
in one go and the two end in the same state.

**Transfers.** **ROM static.** A kind-8 record (or `01:7877` from a dungeon
exit or from a staircase tile) is applied by `01:787B`: area and location are
both set from the record's first byte, the camera row and column come from the
next two bytes (the row is moved by `-60h` and wrapped to the page layout, the
column by `-8`, then both scaled to pixels), and the party is put on foot
(`$C30E` = 0). A dungeon record sets mode `0Ah`, the dungeon position and
facing from the record and puts the saved camera back. A scene record sets
mode `0Ch`, the scene number and the saved camera. Every kind then jumps to
`01:7908`, which keeps the first object slot's byte at `$C810` in `$C2D7`,
clears the six tile animators (24 bytes at `$C26F`) and the whole object table
(`$C800`-`$C8FF`),
and finishes with `pop hl; ret`: it discards the return address of whoever
called the check and returns to that caller's caller. The map loop therefore
never continues after a transfer; the next frame of the game runs the new mode.
The C++ code renders `01:7908` inside every path that reaches it up to the
second clearing, which stays on the Z80: that copy of 255 bytes takes about
5,400 cycles, and a piece that included it fell back to the Z80 on every call
the recordings made (**ROM run**, census of the 18 recordings). The pieces end
at `01:7921`, the Z80 copies, and a short entry at `01:7928` does the
`pop hl; ret`, consuming the two bytes the same way.

**Special ground.** **ROM static.** When bit 2 was set, `01:792A` looks at the
last probed tile id `$C2E5`:

| Tile id | What the code does |
| --- | --- |
| `4Ch` | Only in location 5 and only when the flag byte `$C506` is non-zero: enter the dungeon at a fixed position and facing, camera back. Otherwise nothing. |
| `5Eh` | Only on foot, and only when the party does not already carry item `35h`: scene `00A0h`. |
| `5Fh`-`63h` | Unless the transport value is 8, a short palette flash, then 2 hit points off every living character. |
| `ADh` | A map change; the destination is the row of a small table selected by the location number (location - 4). Nothing for locations below 4. |
| `C3h`-`C6h` | Only on foot: an interaction scene, after storing 19h in `$C2E6` and FFh in `$C29D` (**Inference**: a fixed fight; no recording reaches it). |
| `C7h`-`D1h` | Unless the party carries item `3Bh`: the flash, then 30 hit points off every living character. |
| anything else | Nothing. |

After a damage tile, a character whose hit points reached zero has its bit set
in `$C2F0`; if any did, scene `00AEh` starts and the map is reset like after
any transfer. The damage of one character is `01:7A0C`: for a living
character it subtracts the damage from the hit points (never below zero) and,
when the result is zero, also clears the character's first status byte and
reports it.

**Evidence.** **ROM run.** The C++ pieces were run against the original on
every checkpoint recording on both cartridges (real entrances, scenes,
the road, vehicles and the ice field) and on constructed states for the paths
no recording reaches: every special tile, every record kind and match rule,
the damage step with all its outcomes, and each real record of the area tables
of four recordings. **Inference.** The vehicle with transport value 8 is the
hovercraft (the item menu stores 8 when it is used); the items `35h` and `3Bh`
are story items whose names this chapter does not establish.

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
(**ROM run**, all 21 recordings). With the option off, nothing is copied.

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

**The road.** **ROM run** (recording `road`). The road is a walk on the world
map along the tube between a town and a spaceport: mode 0Eh, which loads it,
keeps register 0 at 06h, so its frames stay 256 pixels wide; mode 0Fh
walks it like the map, with register 0 at 26h, and widens like the map. The
bands show the tube and the land beside it, and the fade into the map load at
the end of the road keeps the road's camera.

**The bands.** The pictures of the two sides, and the leftmost 8 pixels of the
middle, are drawn from the map with their own small background decoder: world
position, then metatile, name word, tile pattern, palette colour. It is a copy
of the chip's background rules (the tests prove it equal to the chip's own
picture), kept apart so the verified chip stays untouched. Bands never draw
sprites: the party or the vehicle is always near the middle (**Inference**,
from the study; **ROM run**: the Land Rover, the hovercraft on water and the ice
digger stay inside the original picture in the recordings `vehicles` and
`ice`).

**Edited cells.** A cell that the game has edited, ice the digger has broken,
lives in the window of metatiles in work RAM rather than the cartridge; the
renderer prefers the window when it covers the cell and its page agrees with
the held camera. The game forgets such a cell when the window is filled again
at a screen change, while the picture keeps showing it (see
[original quirks](../original-quirks.md#broken-ice-is-forgotten-at-a-screen-change)).
So widescreen holds each edited cell it has seen, in the window or, for a
cell broken in the same step that changed screen, in the four names the
picture shows for it, for as long as the cell stays in the wide picture and
the name table, where it shows the cell, still shows the edit. The broken
trail therefore continues into the bands instead of stopping at the edge of the
original picture (**ROM run**, recording `ice`). A held cell that the game
draws again from the window, as whole ice, is dropped, so the bands follow the
picture when the original lets the ice come back.

**Late changes.** The middle of the wide picture is the chip's own picture,
so whatever the game changes while a picture is drawn shows exactly as on the
cartridge: the tile animator's patterns, a broken ice cell's names, and on the
first frame of a Land Rover step to the right the new column, whose names are
written while the top 10 lines are drawn (**ROM run**, recording `vehicles`;
see [original quirks](../original-quirks.md#a-vehicles-new-column-shows-late)).
The bands are drawn from the state at the start of the picture and do not
copy those one-frame changes.

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
wrapped drawing equals the decoder inside the picture. The recording `road`
also flies from Motavia to Palma the way a player does (a spaceport guard,
not a written route): `rom_us_wide_view_road` checks that flight frame by
frame with the same checks, and it gives the same count, 116, 606 and 860
frames, and a fade that widens on every frame (**ROM run**). How it looks is
for the maintainer's play-test.
