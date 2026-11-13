# How it works: walking in the dungeons

This chapter explains how the first-person dungeons work: how a floor is
stored, how the party's place and facing are kept, what each button does, how
chests, fixed fights and events are found, what the 3D view looks at, and how
how the optional automap reads all of this.

**ROM static.** The code and tables described here are identical at the same
addresses in the US Rev-A and Brazilian cartridges (the comparison in
[design, section 5.5](../design.md#55-languages) and the milestone 7 study).
Addresses below are the US Rev-A ones and serve both. Runs described as
"ROM run" are US runs.

**What runs where.** Since milestone 7 the walking code runs in C++: the
dungeon handler `01:65EE` with its pitfall, steps, turns, stairs, door passing
and buttons; the door opening `01:681B`; the exit lookup `01:68BC`; the object
check `01:6963` with its event part; and the two cell readers `01:6BCA` and
`01:6BE9`. Each rewritten piece ends where the original calls something that
stays on the Z80: drawing the 3D view and its animations, the fades, the
battle and the field menu, the dialogue, the layout loading `01:6D56`, and the
video copies of the pitfall. Three short stretches of the pitfall and the 3D
drawing run with interrupts off and stay on the Z80 as they are. The
[generated coverage](../coverage.md) lists what runs in C++ in each recording.

**Maintainer decision.** Original mode behaves exactly like the cartridge. The
automap (last section) is an option: it only watches while the party walks,
and opening it is the same original action as opening the Options window.

## How a floor is stored

**ROM static.** There are 61 floors, numbered 0 to 60. A floor is a grid of 16
by 16 cells. The ROM keeps each floor in 128 bytes, two cells per byte. When a
floor is entered the game expands it into 256 bytes of work RAM, one byte per
cell (`$CB00`-`$CBFF`), by the routine at `01:6D56`. A cell is addressed as
row times 16 plus column, so one byte holds both the row and the column.

**ROM static.** The low three bits of a cell say what it is: 0 open floor, 1
wall, 2 stairs up, 3 stairs down, 4 plain door, 5 locked door, 6 magic door,
7 fake wall. Bit 3 means one of two things. On stairs and doors it marks an
exit: the cell leads out of the dungeon or starts a scene instead of moving
between floors (see "Stairs and exits"). The value 8 alone, floor plus bit 3,
marks an object cell: a chest, a fixed fight, an event, or a pitfall (below).
Values 9 and 15 do not occur. Bit 7 is set at run time on a door that has
been opened; it exists only in RAM.

**ROM static.** Counting the table, the 61 floors hold 4,305 open cells,
10,709 walls, 97 stairs up and 97 down (every stairs cell has the opposite
kind at the same place on the floor it leads to), 103 doors of the three
kinds, 21 fake walls (14 of them on the two floors the ending credits walk
through) and 204 object cells.

**ROM static.** Everything else about a floor lives in tables in bank `03`
that the game scans from the start each time: object records of seven bytes
(floor, cell, the address of a "done" flag, a type, and two data bytes), exit
records of five bytes (floor, cell, kind, two data bytes), and a per-floor
record. A floor byte of FFh ends each table. There are 186 object records (95
item chests, 54 money chests, 25 fixed fights, 12 events) and 65 exit
records. The done flags are bytes inside the saved game block (`$C50A`-`$C6D8`,
one per record; FFh means done), so chests stay opened across saves.

## Where the party is

**ROM static.** Three bytes in the saved game block hold the place: the floor
at `$C30D`, the cell at `$C30C`, and the facing at `$C30A` (low two bits: 0
north, 1 east, 2 south, 3 west). The game looks at the world through a table
of sixteen small signed offsets per facing (the view offsets): added to the
party's cell they name the cell ahead (index 1), two ahead (4), three ahead
(7), the cells beside those (2, 3, 5, 6, 8, 9), four ahead (10), the cell
behind (11) and the cells to the left (12) and right (13). The sum wraps at 8
bits. Every border cell of a normal floor is a wall, so nothing wraps in
play.

**ROM static.** The two cell readers answer "what is at offset index B from
the party". `01:6BCA` returns the kind (the low three bits); `01:6BE9`
returns the value without the opened-door bit and leaves the address of the
cell for the caller. Almost every dungeon routine uses them.

## What happens on each input

**ROM static.** The game loop of the dungeon (`00:0F3C`) waits for the frame,
calls the handler `01:65EE` once, and then runs the random encounter check.
The handler reads the pad's held bits, not the new presses: Up is tested
first, then Down, Left and Right, and the first that applies acts. A walk,
turn or similar action completes (including its animation and the waits for
the screen) inside that one call. **ROM run.** A forward step takes 37
frames and a turn 48, measured from the call to the next one; from the
button press to the next wait a step takes 38 to 42 frames and a turn 50 or
51 (the recordings of milestone 7).

**Up, the forward step.** **ROM static.** The handler reads the value of the
cell ahead.
- Open floor: the party takes one step (`01:6758`): the animation plays from
  the old cell, the cell byte changes by the facing's delta, the final view is
  drawn, and the object check looks at the new cell ahead.
- Wall: nothing moves. Pushing Up into a wall only bumps; the game falls to
  the Down and turn tests (see below).
- Stairs without the exit bit: the floor number changes by one (up adds one,
  down subtracts one), the new floor is loaded, the screen fades out, the
  party is placed two cells forward (the stairs cell and the one beyond), the
  view is drawn and it fades in. The stairs sound plays first. So the party
  arrives on the new floor just past the matching stairs, facing the same way.
- A door that is closed (bit 7 clear): nothing happens. An opened door (bit
  7 set) is passed the same way as stairs without a floor change: fade, two
  cells forward, draw, fade in.
- Stairs or an opened door with bit 3: the exit lookup (below).
- A fake wall: see "The fake wall".

**Down, the back step.** **ROM static.** The cell behind is read. Only open
floor lets the party step back one cell (animation, new cell, no turn); any
other cell, stairs included, leaves Down doing nothing. **ROM run.** At the
entrance of Maharu Cave, facing east with the stairs behind, Down gave no
movement and no animation. After a back step the object check runs for the
cell ahead and then for the cell behind.

**Left and Right, the turns.** **ROM static.** The facing changes by one,
then the cells ahead and to the side are read to choose the animation frame,
and the object check looks at the new cell ahead. A turn takes the same cell
and never fails.

**Buttons 1 and 2.** **ROM static.** With no direction held, a new press of
button 1 or 2 looks at the cell ahead. If it is a plain door (kind 4) the door
opens (`01:681B`): bit 7 is set, a sound plays, and the opening is drawn in
three rounds. An already open door instead opens the field menu, as does any
other cell. Locked and magic doors never open this way; the key and spell
actions of the field menu reach the same opening routine (`00:1C35`). **ROM
run.** In the walk recording the door opened with button 1 and Up against it
only bumped.

**Opened doors do not last.** **ROM static.** Bit 7 is in the expanded floor
in RAM only, and every floor load expands the floor again from the ROM. Stairs,
a pitfall, resuming from a save and returning from an exit all load the floor,
so every door closes again. Chest, fight and event flags are in the save and
survive. **ROM run.** After opening a door, walking through it and back, the
door was still open, because the floor was not reloaded.

### The fake wall

**ROM static.** A fake wall looks like a wall in every view. When the party
pushes Up into one, the game does not test it as a wall: it calls the forward
step with a call to the very next instruction, so the step runs twice. The
first run moves onto the fake wall's cell, draws and checks it; the second
run returns to the same code, moves one more cell and ends there. The second
step never looks at the cell beyond. The fake wall's cell itself does not
reach the loop's next call, so the party seems to pass through. The fake walls
on ordinary floors all have open cells on both sides in a line, so nothing
bad can happen there. **ROM run.** In the walk recording the party went from
one side of a fake wall to the other in two steps.

### The pitfall

**ROM static.** A pitfall is an object cell with no object record. The handler
tests the cell under the party: when it is 8, it scans the object table for
this floor and cell. A record found means a chest, fight or event and the
handler goes on to the directions. The end of the table without a record
starts the fall: a scrolling animation, the floor number goes down by one
(floor 0 would stay 0, but no floor-0 cell is a pitfall), the floor is reloaded
and redrawn, and the object check runs. The party keeps its cell and facing, so
it lands in the same place on the floor below. Because the scan comes first,
the fall starts one handler call after the step that reached the cell; the pit
cell is visible to that call. **ROM run.** The pit cell was seen for exactly one
sample before the fall.

**ROM static.** There are 33 pitfalls on 22 floors. 28 land on open floor. Four
(on floors 49 to 52, one cell) land on another pitfall, so the fall repeats
down to floor 47: a cascade. One, on floor 25, lands inside a wall of floor
24. The game does not guard against it (original quirks).

### Stairs and exits

**ROM static.** Stairs and doors with bit 3 go through the exit lookup
(`01:68BC`) instead of changing floors. It scans the exit table for the floor
and cell. The result decides what happens:
- A record with a kind below 80h leaves for the world map, with the place and
  the map position from the record: 31 cells.
- No record at all: the game mode becomes the world map and the party returns
  to where it entered the dungeon: 16 cells. **ROM run.** The stairs at the
  entrance of Maharu Cave have no record and led to the map in front of the
  cave.
- Kind FFh runs a scene; FEh and FDh run an interaction; any other kind of 80h
  or more (here FCh) runs an event only. After the scene or interaction, the
  event data of the record is run, and if the game is still in the dungeon
  afterwards, the screen is redrawn and the party stays: 33 cells (30 FFh,
  one each FEh, FDh and FCh), mostly doors and locked doors.

One exit record names a plain open cell, which the lookup never reaches. It is
a dead record.

### Floors in stacks, and the map's floor label

**ROM static** (the dungeon tables read by the automap; both ROMs give the
same figures; checked by `automap_view_tests`). Stairs without the exit bit
always join floor number n to n+1, in the same cell on both: every stairs up
has a stairs down in the same cell one floor higher, all 60 pairs, and none
leads below floor 0 or above floor 60. So the direction between two floors is
the same from both sides and never reverses, and floors fall into **stacks**
of consecutive numbers joined by stairs: 21 stacks on the 61 floors, the
largest being floors 3 to 15. The floor number is therefore the height inside
its stack.

**ROM static.** Where the party enters each dungeon is not in these tables, and
the exits to the world map (cells with the exit bit whose record kind is below
80h, or with no record) do not mark one floor per stack: they lie on floors 4
and 11 (stack 3 to 15), floors 20 and 21, and floors 47 to 50 and 52 (stack 47
to 52); three stacks have none (floors 31, 32 and 59-60, the last two being the
credits walk's floors). Floors can also be reached by pitfalls, scenes and
events, which this study did not follow.

**Maintainer decision (2026-11-13).** The map window shows a floor label that is
PSModern's own convention, not something the original shows. The lowest floor
of a stack that has an exit to the world map is `FLOOR 1`; floors above it are
`FLOOR 2`, `FLOOR 3` and so on, and floors below it `FLOOR B1`, `FLOOR B2`. A
stack without such an exit starts at its lowest floor. The label is computed
when the tables are read and nothing the game reads changes. The result, by
stack (first to last floor, label of the first and of the last floor): 0, 1 and
2 alone (1); 3-15 (B1 to 12); 16-19 (B3 to 1); 20-21 (1 to 2); 22 (1);
23-27 (1 to 5); 28-30 (B2 to 1); 31, 32 and 33 alone (1); 34-36 (B2 to 1);
37-39 (B2 to 1); 40 (1); 41-42 (B1 to 1); 43-46 (1 to 4); 47-52 (1 to 6);
53-57 (B4 to 1); 58 (1); 59-60 (1 to 2). Stacks entered at the top of their
numbers read 1, B1, B2... as a cave does; those entered at the bottom read 1, 2...
as a tower does.

## How chests, fixed fights and events are found

**ROM static.** After every step, back step and turn, and when a floor loads,
the game runs the object check (`01:6963`) on a chosen cell: the cell ahead,
and after a back step also the cell behind. The check works only if the cell
value (without bit 7) is exactly 8. It scans the object table for the first
record of this floor and cell. If the record's done flag is FFh the check ends.
Otherwise it remembers the flag's address and arms the random encounter check
(below), then acts on the record's type:
- **Item chest and money chest:** the reward is set up. Only when the chest is
  directly ahead does the dialogue start, followed by the chest routine. A
  chest behind the party never opens: the reward is prepared and nothing else
  happens.
- **Fixed fight:** the monster byte is read and the battle starts. If the fight
  is behind the party, the party first turns around (two turns). A zero monster
  byte ends the check.
- **Event:** the event pointer is run. Behind the party it also turns around
  first.

Chests, fights and events therefore start when their cell is directly in
front, not when the party stands on it. **ROM run.** A fixed fight started with
its cell directly ahead.

**ROM static.** Where the table disagrees with the layout: five fight records
(one on a wall cell on floor 12, four on open floor cells of floor 41) are on
cells whose value is not 8, so the check never reads them. Ten cells carry two
records (nine pairs of money chests and one item chest with a money chest);
the scan stops at the first match, even a done one, so the second record never
fires. One exit record is dead (above).

**ROM static.** The game never draws a chest in the 3D view. A chest cell is
drawn as plain floor; the game announces the chest with text when it is the
cell ahead.

## What the 3D view looks at

**ROM static.** When it draws the standing view (`01:6AED` with frame 0) the
game reads a fixed cone of cells ahead of the party. If the cell ahead is not
open floor, only that cell matters and a close wall is drawn. Otherwise it
reads the cell two ahead and the cells beside the first one (indexes 4, 2 and
3); if that one is open floor, it reads the cell three ahead and the sides
of the one before (7, 5, 6); if that one is open floor, it reads the cell four
ahead and the sides of the last (10, 8, 9). The read is cut at the first cell
that is not open floor. Side cells only show "open" or "closed", walls at a
distance share one face, and stairs and doors look alike at a distance.
Object cells, pitfalls and exit bits are invisible, because only the low three
bits are read. Step and turn animations read the same shape from the old or the
new cell.

**ROM run.** The milestone 7 study ran the original drawing with every unseen
cell changed and saw no change in the picture; this is how the cone was
established, and the automap's tests check it again.

## How random encounters hook into walking

**ROM static.** The handler does not draw encounters. The dungeon loop does,
right after the handler returns, and only if a pending byte at `$C2D2` is
nonzero ([random numbers](random-numbers.md#the-encounter-check-in-a-dungeon)).
The walking code sets that byte: every forward step, back step and turn does,
because they all go through the animation routine `01:6A58`, and so does the
object check when it finds a record that is not done. The loop clears it
after one draw. Two consequences follow, kept in original mode:
- Turns and back steps arm an encounter as well as forward steps. **ROM run.**
  When the milestone 7 recordings were made, a back step, two left turns and a
  right turn had to be moved to avoid a fight.
- The fake wall runs two steps but the loop draws once.

The local-space check at `00:0F5F` reads the kind of the cell directly ahead
with the cell reader `01:6BCA`. A draw that passes becomes a fight only when
that cell is open floor (kind 0). An object cell counts as open here, since
only the low three bits are read, a wall, stairs, door or fake wall ahead stop
the fight.

## How the automap reads this

**Maintainer decision.** The automap is optional and off by default. Tracking
is passive: it only reads memory and never changes what the original code
does. Opening the map is not invisible: like the Options window, it holds the
game by running and cancelling the game's own field menu at the walking wait,
a legal original action that clears the two bytes the field menu always
clears. The parts below describe it; they are PSModern's behavior and not the
original's.

**Where it samples.** The host watches the instruction at `00:0F48`, the
handler call of the dungeon loop, and reads memory without stopping or
changing anything. A sample is taken at each pass in game mode `$0B`: the
floor, the cell, the facing and the 256-byte layout. Because the handler does
its whole action in one call, the sample sees the party between actions. The
exceptions are the middle cell of a fake wall, which never reaches it, and
the pit cell, which is seen once before the fall.

**What counts as seen.** Each sample marks the cells of the 3D view's cone (the
rule above) from the party's cell and facing, plus the cell stood on. The map
therefore shows a little more than the picture does: side cells of a distant
hallway, and the exact kind of doors seen from the side or afar, by the
maintainer's choice.

**What counts as found.** A second set holds cells whose disguise the game
lifts:
- A fake wall walked through: two consecutive samples one facing and two cells
  apart, with a fake wall between them, mark that middle cell as floor. When
  the main loop at `00:00AF` sees a game mode other than `$0A` or `$0B` after
  a pass, as when the party leaves for the world map or the title, the next
  sample starts a new walk, so leaving and coming back never counts as
  walking through. Battles and the field menu run inside the dungeon pass and
  leave the mode alone (ROM static).
- A pitfall stood on: an object cell with no record.
- A chest: the map shows a chest as soon as its cell is seen, drawn as opened
  when its done flag is set. **Maintainer decision (2026-11-11).** This shows
  more than the original: the game never draws chests in the 3D view and
  announces one only when it is directly ahead.
- Fixed fights and events are never marked. Dead and shadowed records are never
  used, since the rule asks for the first record on a value-8 cell.

Cells neither seen nor found are unknown and the map never reads their layout
bytes. An exit is shown on stairs or doors with bit 3 whose first exit record
has a kind below 80h or that have no record; the 33 cells that start a scene,
an interaction or an event show as plain stairs or doors.

**What is kept with each saved game.** The exploration (the seen and found
sets of every floor) is kept in a file beside the save, `<save>.automap`. Each
record is keyed by a digest (SHA-256) of the saved game block, so saving stores
the current exploration under that digest and Continue finds the one that
matches the slot it loaded. A new game starts with an empty map, and a slot
without a record starts empty. The file keeps at most the records of the used
slots. A file that cannot be read gives an empty map; at the next save it is
kept as a backup and a new file is written.

**Maintainer decision.** The maintainer played the map on 2026-11-12 and
accepted these rules as built.
