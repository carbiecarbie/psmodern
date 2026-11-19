# How it works: menus and windows

What this chapter covers: how the game draws a window and puts back what
was under it, the windows it opens on the field and in battle, how a choice
is made with the cursor, the field menu with its commands, the hospital, the
church and the shops, and how the screen fades in and out. The game rules
behind the spells chosen here are described in [battles and magic](battle.md);
the item selector also hands its effects to separate routines.

**Since milestone 14.** The Z80 is no longer in the program. Where this chapter says that a
stretch stays on the Z80, is left to the original code, runs on the emulated processor, or falls
back when an interrupt or a frame boundary is near, it describes the milestone the text was
written for. Today every such instruction is rendered C++, an interrupt is accepted at the exact
instruction even inside a piece, and a state a piece cannot render stops the game with a message
([interrupts](interrupts.md), [development](../development.md#registered-routines)).

## What runs where

**ROM run.** The save transfer at `00:1D1D` can copy part of a slot before
the video frame ends. The source, destination and remaining byte count then
pick up exactly where they left off. The C++ version copies short sections
and preserves the order in which the host observes the bytes. Both cartridges
were checked against the original across interrupt and frame boundaries.

**ROM static.** Menus share a small selection routine with other parts of
the game. The caller supplies a list of addresses and a choice number. The
routine doubles the choice to find a two-byte address, reads it and sends
control there. The chosen action returns directly to the caller. Doubling
uses eight bits, so a choice of 128 would select the same entry as zero.
The per-frame selector already holds a byte offset. An interaction uses
a third form that doubles the choice in sixteen bits. The C++ replacements
use the live table in the player's machine each time
(`00:00E6`, `00:0131`, `01:4769`).

**ROM run.** Everything in this chapter was first code of the cartridge's
Z80 processor. Since milestone 12 PSModern runs the decisions of these
routines as C++ instead: which window to open and where, what goes into it,
how the cursor moves, which command was chosen, what a shop or a hospital
charges, and which colours a fade changes next. They run in 323 short pieces
(the code calls them segments), in six groups that match the sections below:
drawing windows (29), the windows themselves (125), choosing with the cursor
(25), the field menu (63), the hospital, church and shops (58) and the fades
(23). A piece starts at one instruction of the original and runs until the
original must take over again: a call to code that has not been rewritten,
a wait for the next picture, or a question only the machine can answer.

Every piece is checked against the original, call by call: with the same
starting state the C++ must leave the same registers, the same memory, the
same video memory and sound writes, and use the same number of clock cycles as
the Z80 would. A piece that cannot be run safely at that moment (for example
because the end of a picture falls in the middle of it) is not run at all and
the original code runs it instead, so a rare fallback costs nothing in
accuracy.

**Where milestone 12 stopped.** The following boundaries describe the menu
implementation at the end of milestone 12. Milestone 13 changes the save,
host observers and dialogue rows described below.
- The instructions that switch the console's interrupts off and on around the
  drawing of a window, and the one or two instructions next to them. A window
  is written to the video chip with interrupts off so that nothing interrupts
  the writes; the pieces run between these switches, as the text output has
  done since milestone 9.
- Waiting for the next picture, the tables that jump to the right routine and
  the sound driver: other chapters and later milestones own them.
- Saving a game: switching the cartridge's battery memory on, copying the
  1,024 bytes of the save and switching it off again.
- The two places the PSModern program itself watches (the cancel point of the
  selection loop, `00:2D93`, and the point after a save, `00:1D2D`): Options,
  Language and the automap hold the game inside its field menu through them, so
  they must run exactly as the original runs them.
- Where the end of a picture almost always falls: the two loads
  `00:3451` and `00:3454` before the last window of a dialogue scroll, and the
  whole end of a row of a window drawn at once, `00:3A97`-`00:3A9E` (six
  instructions, 52 cycles). A piece there would almost always fall back, so
  they stay native (ROM run).
- What the commands do once chosen: the effects of items and spells, the
  battle commands and the name entry belong to milestone 13.

**Milestone 12 coverage.** In its twenty recordings the menu pieces were
called 694,553 times and 693,170 of those calls (99.8%) ran in C++; every piece
called 20 times or more in a recording ran in C++ on at least 93.9% of its
calls in that recording, and no call was forced. 296 of the 323 pieces are
reached by a recording; the other 27 (for example the second kind of shop and
some palette effects tied to the story) are exercised by state probes that set
up the machine and run the piece on both cartridges. Where a section says "ROM
run" it describes what happened in such a run, and "ROM static" what the code
says when read; the labels are those of the
[documentation rules](../AGENTS.md#evidence-labels).

## Drawing a window and keeping what was under it

**ROM static.** The background of the screen is a grid of tiles kept in
video memory (the name table, two bytes per tile: the tile number and its
attributes, see [the console chapter](master-system.md)). A window is a
rectangle of that grid written over what was there. Before a window is drawn,
the game copies the rectangle it will cover into a buffer in work RAM
(`$D700`-`$DEAF`, a separate place for each kind of window); closing the
window draws that copy back. Opening and closing therefore use the same two
operations, "save a rectangle" and "draw a rectangle", given the screen
position (DE), the source or buffer (HL) and the size (B rows of C bytes).

**ROM static.** Saving the rectangle (`00:3AA6`) reads video memory through
the VDP's data port, byte by byte, row by row, each row 40h bytes (one screen
row) below the last. Each byte read is followed by a short idle pause (a
push and pop of a register that changes nothing, 21 cycles), so that the VDP
has the time it needs between two accesses while it draws the picture.
Interrupts are off for the whole rectangle.

**ROM static.** Drawing has two forms. `00:3A57` switches slot 2 to bank 27
(where the window layouts are; a copy in the work-RAM buffer is not
affected by the switch) and calls `00:3A68`, which writes one row
with interrupts off, then turns them on and waits for the next frame before
the next row: the window unrolls downward, one row per frame, and the
original bank is put back at the end. `00:3A83` does the same rows without
the waits, all in one go with interrupts off. The window code of the next
section chooses one or the other for each window.

**ROM static.** The printers write a short line of tiles at the VDP's
current address, also with interrupts off, and finish by moving DE one screen
row down and waiting for the next frame:
- numbers: `00:2FE1` (a 4-tile label and a number 0-255 in three digits),
  `00:2FD8` (the same with an 8-tile label), `00:3036` (a 6-tile label and a
  number 0-65535 in five digits, used only by the status window at
  `00:3805`); the
  money printers `00:35C4` and `00:35BC` (4- and 6-tile label, the money at
  `$C4E0` in five digits). Each digit is counted by repeated subtraction of
  10000, 1000, 100 and 10. The digit helper `00:3645` writes a digit as a
  blank while only zeros have come before it and as a digit after the first
  non-zero one; the last digit is always written, so 0 shows as "0";
- names, two tiles high: each character has a top and a bottom tile in a
  font table, and the printer is called once per tile row (C = 0 top, 1
  bottom). `00:356B` prints item number (HL) from the item-name table (8
  characters per item), `00:315C` an enemy name from HL;
- a shop line, `00:393A`, from a shop record (item, price): the top row is the
  item name and six blanks, the bottom row the name's bottom half, one blank
  and the price through the money printer's digits.
Each line starts with a border tile and ends with the same tile with
attribute 13h instead of 11h, which the VDP draws mirrored left to right
(hardware documentation: attribute bit 1 flips a tile horizontally), so the
lines of a window close it on both sides.

**ROM static.** The dialogue scroll (`00:3439`) moves the dialogue box's text
up by one tile row: it saves the lower three tile rows of the box, draws them
one row higher, draws a blank row at the bottom (both with `00:3A83`) and
waits four frames. `00:342C` calls the scroll twice, keeping its caller's
registers. **Inference.** A line of text is two tile rows high (each
character has a top and a bottom tile), so the double scroll moves the text
by one line, which takes eight frames.

**ROM static.** The US and Brazilian ROMs have this code byte for byte the
same at the same addresses.

**ROM run.** In PSModern these routines run in C++ (milestone 12), verified
against the original call by call on every recording and by state probes,
including every window size the game uses. The switches of interrupts (each
DI and EI, and the instructions right next to them) stay on the Z80. Each
read and write of the VDP happens at the same cycle as in the original, so
the VDP sees exactly the same accesses. A window too big to save or draw
within what is left of a frame is done in short pieces (one byte while
drawing at once, three while saving, two characters of an enemy name): when
a piece does not fit, the Z80 runs the original until the frame boundary is
passed and C++ takes over again. Short pieces keep that hand-over brief,
because the instruction after a piece that did not fit is usually the start
of the next piece.

## The windows

Every window the game shows is a small routine of its own, 49 of them between
`00:2EAC` and `00:3A56`, and they all follow the same three-step pattern. The
window engine (previous section) does the real work; a window routine only
says *which* window: where its picture is kept, where on the screen it goes
and how big it is.

**Opening.** The routine first asks the engine to save the part of the screen
the window will cover (`00:3AA6`): the picture cells under the window are read
back from video memory into a holding area in work RAM (`$D700`-`$DEAF`). Each
kind of window has its own place there, and kinds that can never be open at the
same time (the battle commands and the field menu, say) share one. The routine
then draws the window itself (`00:3A57`) from a table in bank 27 of the
cartridge: a frame, and the fixed words inside it, laid out as rows of picture
cells. Windows with a number in them (hit points, money, experience) are
drawn in pieces: a bare frame first, then each label and each number, then the
rest of the frame. Three values go with every call: the address of the picture,
the place on the screen (a video-memory address) and the size, rows in one
register and bytes per row (two for each cell) in the other. These are the
three loads that make up most of the window routines.

**Closing.** The routine does not erase anything. It hands the saved copy back to
the same drawing routine, aimed at the same place on the screen, and the
picture that was there before reappears. Opening and closing therefore always
use the same position and the same size, and the routines come in pairs.

**Redrawing** (the party window and the battle commands in battle, the field
menu) feeds the drawing routine at once (`00:3A83`) instead of row by row, so
the window can be refreshed after a change without a frame of flicker.

What each group of routines is for:

- *Battle*: the name box over the acting character (`00:2EAC`, closed by
  `00:2ECD`); the party window with one box per living member, showing HP and MP
  (`00:2ED9`, boxes `00:2F16`, redraw `00:2F3C` and `00:2F6D`, refresh of one
  member `00:2F93`, close `00:30B7`); the command window (`00:30C3`, `00:30D5`,
  `00:30E1`); the two enemy windows, the list of enemy names with a count and
  the party's HP column beside them (`00:30ED`, `00:3105`, close `00:319E`); the
  member choice windows (`00:3665`, `00:3682`, names `00:369F`, close `00:36BB`
  and `00:36CC`).
- *Field menu and what it opens*: the field menu itself (`00:36DD`, redraw
  `00:36EF`, close `00:36FB`); the status window, one number at a time
  (`00:37CF`, close `00:38C1`); the item list, eight names a page, with the
  money above it (`00:3521`, `00:352D`, choice `00:34D5`, close `00:3656`); the
  magic list (`00:3478`, close `00:34C9`); the equipment window (`00:3707`,
  close `00:374D`); the item actions window (`00:3759`, close `00:376B`).
- *Towns*: buy or sell (`00:3777`, close `00:3797`); the shop list (`00:38CD`,
  `00:38D9`, close `00:3999`); the money window (`00:39E9`, `00:39F7`,
  `00:3A12`); the save slot window, whose names are read from the cartridge RAM
  (`00:39A5`, `00:39B1`, close `00:39DD`).
- *Questions*: yes or no (`00:37A3`, close `00:37C3`) and the two-choice window
  (`00:3A21`); the dialogue window is closed by `00:3464`, which does nothing
  when no dialogue is open.

Some of these routines also start the choice (a jump into the cursor loop of
the next section) and return what the player chose; the shop list turns the
choice into the address of that row's entry in the shop table.

**What runs in C++.** A window routine is nearly all loads and calls, so it is
rewritten as 125 short stretches: each one does the loads and stops at the next
call into the engine, which runs on the Z80 or in its own rewrite. Every
stretch is checked against the original on every call. The table addresses,
screen positions and sizes are not in the program: it reads them from the
cartridge as the original does. What is left to the Z80 in this group is the
four instructions that switch the cartridge RAM for the save slot names
(`00:39B1`-`00:39B3`, `00:39C2`-`00:39C4`).

*Evidence:* ROM static (the listing of `00:2EAC`-`00:3A56`); ROM run (125
stretches against the original on both cartridges, in the state probes and on
the recordings that open these windows).

## Choosing: the cursor, yes or no, the waits

Evidence labels: **ROM static** is a reading of the ROM's code and tables,
**ROM run** is something the original did when run, **Lockstep** is the
rewrite compared with the original on recordings and probes.

Every menu in the game waits the same way: it asks the game's frame wait
(`00:0052`) to sleep until the next picture is finished, looks at which buttons
were newly pressed in that picture, reacts, and sleeps again. The routines of
this section are those few instructions between two sleeps. The sleep itself
stays on the original code (it is the game's heartbeat), so each rewritten
piece starts right after a wake-up and ends at the next call of the wait.

**The cursor loop (`00:2D51`).** It switches the selection on (`$C268` set to
FFh, which tells the picture routine to draw the cursor), puts the choice back
to the first option and clears the blink counter. After each picture it reads
the pressed buttons: Up lowers the choice and Down raises it, both wrapping
around at the ends (Up on the first option gives the last one, Down on the last
gives the first; both in the same picture cancel out). The number of options
minus one is in `$C26E`. Then it reads the buttons again, and button 1 or 2
leaves the loop. The one instruction that notices the confirmation, `00:2D93`,
is observed before the registered routine runs. This lets the host keep the
field menu open or accept the selection at the original cycle. The choice is then returned after
one more wait with the selection switched off (**ROM static**, **Lockstep**).

**The item list (`00:2DA5`).** The same loop, with two additions: Left and
Right turn the page by eight items (the pair at `$C299` holds the first item
shown and the last page start; past the end the page wraps to the first, and
before the start to the last), and a page turn redraws the list through the
list window code, then works out how many options the new page has and keeps
the choice inside them. With nine or more items the first row of the list is a
page row: choosing it turns the page instead of returning an item, and every
other row returns its number minus one. With fewer items the row number is
returned as it is (**ROM static**).

**Yes or no (`00:2D19`).** It opens the yes/no window, waits for the choice with
the cursor loop, closes the window and returns the answer with the flags set
from it (**ROM static**). The registers BC are saved around it.

**The waits.** `00:2D25` waits for button 1 or 2; `00:2D33` and `00:2D37` wait
for a button for at most 30 or 180 pictures; `00:2D47` and `00:2D49` wait a
fixed number of pictures (208 or the count the caller put in B), ignoring the
buttons (**ROM static**).

**The cursor blink (`00:2E62`).** It runs in the picture interrupt of menus,
only while a selection is active. It erases the cursor cell it drew last, draws
the cell where the choice now is, and then alternates what it draws between two
characters every 8 calls, so the cursor flashes on a 16-call cycle. Each cell is
64 bytes of the picture's name table apart, so the choice number picks the row.
In game mode 3 (battle) the two characters are different from every other mode
(**ROM static**).

**The picture-interrupt handlers (`00:015F`, `00:01AD`, `00:01D1`).** One per
kind of screen: fades, menus and battle, and text rows. All three begin the
same way: they write the two scroll values to the picture chip's registers 8
and 9 (from `$C300` and `$C304`) and call the sprite upload. What follows
differs: the fade handler runs the fade and flash steps and uploads the colours
(`00:017A`), the menu handler polls the controller and runs the blink, the text
handler only the sprites. The calls to other routines stay on the original code;
only the lines between them are rewritten (**ROM static**, **Lockstep**).

What runs in C++ here: all 25 entries. On the 20 recordings of this milestone
every one of these entries ran in C++ on every call that reached it, with no
fall-back to the original code at all (**Lockstep**); the cursor loop's poll and
the blink alone were called more than 45,000 times.

## The field menu

**ROM static.** The field menu is the small menu the player opens on the
map, in a town or in a dungeon. It lives in `00:1BE1`-`00:1C79` (the menu
itself), `00:1CA1`-`00:1DDB` (status, save, magic), `00:2168`-`00:21FA` and
`00:2201` (items), `00:26C8`-`00:297A` (equip, drop, the inventory helpers,
search and chests). The code is identical, at the same addresses, in the US
Rev-A and Brazilian cartridges (compared byte for byte over these ranges).
The windows it opens are drawn by the window routines described above, the
choosing is done by the cursor loop, and the texts by the dialogue printer;
this section is what the menu itself decides.

### The menu and the byte that ends it

**ROM static.** Opening the menu (`00:1BE1`) clears two bytes, `$C29D` and
`$C2D8`, then draws the command window and the party window. The loop at
`00:1BEE` has two ways out. If `$C2D8` is not zero, the loop leaves at once with
that value. This is how an item or a spell used from the menu ends it: the
item or spell code leaves a number there (see below). Otherwise it puts the
cursor on the first of five commands, lets the player choose
(`00:2D51`), and, unless button 1 was pressed (that leaves with the value
`$FF`), jumps through a five-entry table at `00:1C97`: status (`00:1CA1`),
magic (`00:1D4D`), items (`00:2168`), search (`00:2839`) and save (`00:1CDF`).
When the command comes back, the command window is redrawn (`00:36EF`) and the
loop starts again.

**ROM static.** Leaving always does the same cleanup first (`00:1C15`-`00:1C26`):
unless the value is 5, the first object slot's state byte (`$C800`) is cleared
and the first sprite Y byte (`$C900`) is set to the end marker, so the party's
sprite is not drawn; the party window and then the command window are closed.
The value then decides what happens next (`00:1C29`-`00:1C79`):

| Value | What happens |
| --- | --- |
| `$FF`, 0, 1, 2 | Back to the game |
| 3, 4 | The dungeon's door opening (`01:681B`, with the value in C) |
| 5 | A palette effect (`01:7C85`), a wait for button 1 or 2, then a scripted scene at `01:4414` |
| 6 | A sound request, a second palette effect (`01:7CBB`), the byte `$C2DC` set to `$FF`, and the menu opens again |
| 7 | A scripted scene at `01:4497` |
| 8 and up | A sound request, the game mode set to 8 (map loading), the transport byte cleared (on foot) and a jump to the map transfer with a three-byte record taken from a nine-record table at `00:1C7C`, chosen by `$C317` |

**Inference.** Values 3 to 8 are the "after effects" an item or a spell asks for
(a door, a story effect, a return to a place). The values are the code's; which
item or spell leaves which one belongs to the item and spell effects, outside
this part.

### Status

**ROM static.** Status (`00:1CA1`) opens the member choice (`00:3665`). Button 1
leaves. A member who is not alive is refused by the check at `00:188E`, which
shows its own message and answers "not allowed". For a live member the menu draws
the equipment window (`00:3707`) and the status window (`00:37CF`), waits for button 1
or 2 (`00:2D25`), and, if the member's count of battle spells (`$C40E` of the
record) is not zero, also opens the spell list window (`00:3478`) and waits again.
The fourth member is drawn in the third member's row of that window
(`00:1CC8`-`00:1CCC`: member 3 becomes 2 for the window; the count and the
record are still member 3's). Everything is then closed in reverse order.

### Magic

**ROM static.** Magic (`00:1D4D`) opens the member choice again. A dead member is
refused as above. The third member (number 2) is refused with a message of its
own (`00:1D5B`-`00:1D5D`), and a member whose count of map spells (`$C40F`) is
zero gets another (`00:1DC0`). Otherwise the spell list window opens
(again with member 3 drawn as row 2), the cursor is set on the first spell with as
many choices as the member knows, and button 1 leaves.

**ROM static.** The chosen row indexes a table of 15 bytes at `00:1DEF`: five
slots for each of the three members who can cast on the map (row = member, with
member 3 as row 2). The low five bits of the byte are the spell number (the
upper three are ignored, `00:1DA7`). The cost comes from a 19-byte table at
`00:1DDC` (`00:1B87`): the member's magic points minus the cost; a borrow is
"not enough points" and shows a message, anything else jumps through a table of
19 spell routines at `00:1DFE`. The [spell effects](battle.md) perform their
own MP subtraction, show their messages and may leave a value in `$C2D8` for
the menu to act on.

### Items

**ROM static.** Items (`00:2168`) looks at the item count (`$C4E2`). With no
items the list window is opened and closed again without a choice and control
returns (`00:216C`-`00:2172`). Otherwise the list is opened (`00:34D5`); button 1
leaves (`00:2178`). A chosen item `21h`-`23h` is one of the three vehicles. If the
party is travelling in that vehicle (the transport byte `$C30E` equals
4 times the vehicle plus 4), the menu asks the map code whether a spot to put it
down exists (`01:7656` for the ground vehicles, `01:7732` when the transport byte is 8). If
there is one the party is put on foot, the menu is told to leave (`$C2D8` set to
`$FF`), a message is shown and the list closes; if not a different message is
shown. Any other item, or a vehicle that is not the one in use, goes on to the
choice of who uses it: the first living member, searched from member 0 to 3
(`00:21C1`-`00:21D3`), becomes the current character (`$C2C2`). The action
window (`00:3759`) offers three actions and button 1 closes it. The choice indexes a
three-entry table at `00:21FB`: use (`00:2201`), equip (`00:26C8`) and drop
(`00:2752`).

**ROM static.** Use (`00:2201`) only jumps through a table of the 64 item
effects at `00:220A`, indexed by the item number. What an item does is
milestone 13's. The menu closes the item list and redraws the party window
(`00:21F5`) when the action returns.

### Equip, drop and the inventory

**ROM static.** Every item has a property byte in a 69-byte table at `01:7FAB`.
Its upper four bits say which members can equip the item (one bit per member),
its lower two bits say which of the three equipment bytes of the record it takes
(weapon `$C40A`, armour `$C40B`, shield `$C40C`), and bit 2 means it cannot be
dropped.

**ROM static.** Equip (`00:26C8`) first checks that the upper four bits are not all
zero (otherwise a message). It then chooses a member, refuses a dead one, and
tests the member's bit (`00:26FE`-`00:2708`); if the member cannot use it the menu
says so. If it can, the member's old item in that slot goes into the inventory slot
the new item came from (`$C29B` points at it), the new item takes its place in the
record, a message and the equipment window are shown (`00:272A`-`00:2736`), and if
the old slot was empty (item 0) the now-empty inventory slot is removed
(`00:273C`-`00:273E`). The member's figures are then recomputed (`00:17BA`).

**ROM static.** Drop (`00:2752`) refuses an item with bit 2 (a message) and
otherwise shows a message and removes the item.

**ROM static.** Removing an item (`00:277C`, `00:277F`) slides the rest of the
list down one place (a copy of as many bytes as are left up to the end of the 24
slots at `$C4C0`-`$C4D7`), lowers the count and clears the new last slot. Adding an
item (`00:279F`) appends it, lifts the count and asks for a sound (`$B3`) when there
is room. A full inventory (24 items) starts a small dialogue: a yes/no question,
and on "no" a second one before the item is left behind; on "yes" the item list
opens so the player can pick an item to give up in its place, the item chosen must
be droppable (bit 2), and the new item takes its slot (`00:27BC`-`00:2824`). Finding an
item (`00:282E`) searches the 24 slots for the item number in A and answers "zero"
when it is found (it is a search for an item, not for an empty slot).

### Save

**ROM static.** Save (`00:1CDF`) shows a message, opens the slot window
(`00:39A5`), shows a second message and asks yes or no. "No" closes the slot
window and the dialogue. "Yes" copies the game mode into `$C316` and shows a third
message. The slot is the dialogue number the slot window left (`$C2C5`); its base in
cartridge RAM is `$8000` plus the slot times `$400`. First `00:073A` reads the slot's
used flag (`$8200` plus the slot) and leaves the zero flag set when it was clear. The
menu then clears the flag, copies the 1,024 bytes at `$C300` into the slot, and sets the
flag again (switching cartridge RAM in and out is the stretch `00:1D0E`-`00:1D32`). The
copy happens either way; what follows depends on the zero flag from before. A slot that
was unused ends the menu for good: `$C780` is cleared, the game mode is set to 16 and
the routine drops its own return address and the menu's with two pops before it returns
(`00:1D41`-`00:1D4C`). A slot that was in use shows a fourth message and closes the slot
window and the dialogue (`00:1D35`-`00:1D3E`).

**Lockstep.** The C++ save path invalidates the selected slot before the
first data write. The copy can stop at a frame or interrupt with the slot
still invalid. After the last byte the game sets its flag to one. The host
observes `00:1D2D` while cartridge RAM is still mapped and can associate
the completed bytes with the automap. Tests compare every cartridge write,
its cycle and the contents visible at that callback for all five slots.

### Search and chests

**ROM static.** Search (`00:2839`) first looks at the first object slot's state
(`$C800`). The value `0Eh` closes the party window, shows the search
message and runs the chest routine (`00:28DB`). **Inference.** The object code leaves
`0Eh` there when a chest is in front of the party; the menu itself only compares it. Otherwise two places can give an item.
The place's identity is the location number (`$C309`) and a position found from the camera
(`$C305`, `$C301`) with the same arithmetic the map code uses (`00:284F`-`00:286F`):
in location 7 the cell (`28h`, `1Eh`) gives item `26h`, once, while a story byte
(`$C507`) is set and is not `FFh` (it is set to `FFh` when the item is given); in
location 1 the cell (`30h`, `48h`) gives item `1Fh` while another story byte (`$C509`) is
set, the third member's shield byte (`$C42C`) is not already that item and the item is not
already in the inventory. Elsewhere the scene byte (`$C2DB`) `A2h` or `A3h` starts one
of two scenes (`01:5566`, `01:558C`), and anything else shows the "nothing" message.

**ROM static.** The chest routine asks first (yes/no window). On "yes" it asks for a
sound, marks the chest done (`$FF` through the pointer `$C2E1`), sets `$C80A` and lets the
object engine play the opening (`00:18B9` waits for `$C800` to clear). The byte `$C80F`
then decides the trap (`00:2950`): `3Dh` is safe, `3Eh` hurts every member, any other value
hurts one member picked by the random generator (`00:05B1`, two bits). Hurting a member
(`00:2966`) does nothing to a dead member and otherwise takes a third of the current
hit points, rounded down, so it never kills (`00:296B`-`00:2978`). The reward is the money
(`$C2DD`) and the item (`$C2DF`) the object left: both zero shows the "empty" message;
otherwise the money is added to the party's (`00:297A`) and shown if it is not zero, and
the item, if there is one, is announced and added with the inventory routine. The party
sprite's Y byte is set to the end marker at the end.

### What runs where

**Milestone 12 ROM run.** The menu, the commands and the inventory helpers ran in C++ in 63 short
pieces. A piece starts where the Z80 code starts after one of its calls and ends
where the next call begins: the calls themselves (windows, the cursor loop, the text,
the spell and item effects, the map code) stay on the Z80 core, and so does the
save copy (`00:1D0E`-`00:1D32`, it switches the cartridge RAM and watches the
write that marks the slot used for the automap's backup) and the instruction at
`00:2D93`. Every call of a piece is checked against the original, with the machine and
the cycle count compared. On the new field-menu recording and the other 14 recordings
every call of the 63 pieces that happened ran in C++ (no fallback), on both cartridges;
the state probes in `tests/menu_field_tests.cpp` run each piece 1,500 or more times with
changed registers, items, stack words and slot mappings, and run every instruction of the
part (561 in all), including the ones no recording reaches (the vehicle special case,
the two search places, the trap on every member).

## Shops, the hospital and the church

Every place is a conversation, and the code of a place is mostly the
decisions between two of its steps. The game shows a line of dialogue (the
routine 00:31CF), asks a yes-or-no question (00:2D19), lets the player pick a
party member (00:3665) or an item (00:38D9, 00:34D5), and closes the windows
it opened (00:36BB, 00:3464). Those steps are routines of their own and are
described in the chapters above. What this section explains is what each place
decides between them: which line comes next, how a price is worked out, and
what the party's records look like afterwards. Evidence for all of it: ROM
static, and ROM run on the recording `places` (Paseo: the hospital, the
church, two shops and the buy-or-sell shop) on the US ROM, with state probes
on both ROMs for the paths the recording does not reach.

**The hospital (00:298D).** It asks whether the party wants healing. With a
party of more than one it then lets the player pick a member, and says so when
that member is dead or has nothing to heal. A member's record keeps current
hit points and magic points in bytes 1 and 2 and the maxima in bytes 6 and 7.
The price is the missing hit points plus the missing magic points, one meseta
each (00:29E4-29F3), shown in the dialogue through the number word at C2C5.
Asked again, the game takes the money if there is enough (money equal to the
price is enough), shows the new total, requests the jingle C1h, copies the
maxima over the current values and says it is done. With more than one member
it offers to heal another; a lone party goes straight to the goodbye line
(00:2A37-2A6A). Not enough money gets its own line (00:2A5E). 00:2A6D, a
separate routine, restores every living member's points the same way without
asking and without a price.

**The church (00:2A85).** It remembers which church this is (C2DB, copied to
C317; the field menu's fly transfer, 00:1C6C, reads it to come back here) and
asks whether the party wants a dead member revived. It lets the player pick a
member; a member who is alive is refused. The price is twenty times the
member's level (the level doubled twice and then multiplied by five,
00:2AB8-2AC5). When the money is enough the member is marked alive, the
current points are set to the maxima, the jingle C5h plays and the game waits
30 frames, or until a button is pressed (00:2D33, native). Saying no, or
having nobody to pick, ends with the next-level report: 00:2B58 asks 00:2B84
for each of the four characters in turn. A dead character is skipped; a
character at level 30 gets one line; any other gets the experience still
missing for the next level, the difference between the level table's threshold
for the next level (bank 3, eight bytes a level) and the experience in the
record (bytes 3 and 4).

**The shops (00:2BC0, 00:2C8F, 00:2C98 and 00:2CB1).** A shop greets, asks, and
shows the list of what it sells (00:38CD, 00:38D9, native) in a loop. The
second shop kind (00:2C8F) differs only by its greeting and then joins the same
question as the first (00:2BC6). A choice is refused, in this order, when the
party already carries 24 items and when the money is short. An item below 40h
is paid for (the money is taken, the new total shown) and added to the party's
list (00:279F); a shop asks "anything else?" until the player says no or
cancels. Three items, 21h to 23h, get an already-carried check: the game looks
the list up after it has already taken the money, and when the party has the
item nothing is added (see the quirks). An item numbered
40h or more is not a real item. The first two such choices only show a line
(0142h, then 0144h) and advance the counter at C2EC; the third buys the item
numbered 33h instead (the Road Pass) when the party does not carry it, takes the
chosen line's price and says 0146h; if the party already carries it, the
counter starts again and the first line comes back.

The buy-or-sell shop (00:2C98) first asks buy or sell (00:3777). Buying joins
the shop loop above. Selling (00:2CB1) lets the player pick an item from the
party's list and looks up its price in a table of bank 3, indexed by the item
number with its top two bits ignored. A price of zero cannot be sold; any other
asks for confirmation, removes the item (00:277C), pays the money (00:297A),
and asks whether to sell more.

How much of this runs in C++: every decision above (58 pieces in
00:298D-00:2D18) runs in C++, ending each time at the next call of a
routine that stays on the original code for now; the calls themselves, the
redraws and the waits are described in the chapters of the routines that own
them.

## Fading the screen

Every change of screen in the game goes through a black screen: the picture
fades out, the new one is prepared, and it fades in. The game does this
with the console's 32 colours, which it keeps in a working copy in memory
(the palette buffer) and sends to the video chip once per frame. A fade
changes the working copy a little at a time and lets the frames do the rest.

**The fade waits (01:7AFD to 01:7B3F).** There are four of them: fade out
and fade in, each in a short form (the first 16 colours, the background)
and a long form (all 32). Each one puts two numbers in memory: how many
steps the fade has (nine) and how many colours it touches, then waits one
frame at a time (a frame of the fade kind, 16h) until the step counter
says it is finished. The fade-in starts the long form by turning the whole
working copy black, so it climbs from nothing toward the colours the new
screen asked for (kept in a second copy, the target palette). The waiting
itself, and the frame interrupt that does the work, are the console's own
and stay on the original code; the C++ here runs the set-up before each
wait and the check after it. *Evidence: ROM static (01:7AFD-7B3F); ROM run
(every checkpoint recording: 100% of these calls ran in C++ and matched).*

**The fade step (01:7B40).** Each frame of the fade kind the interrupt
handler (00:015F) calls this step, but the step only acts on every fourth
frame, counted by a small counter. On those frames it moves every colour of
the working copy one notch. A colour on this console is three small
numbers, red, green and blue, each from 0 to 3. Fading out lowers red until
it is zero, then green, then blue (01:7B5F), so a full fade of one colour
takes at most nine steps, and a colour that is already black is left alone.
Fading in goes the other way, blue first, then green, then red, toward the
target colour (01:7B92), and it only ever raises a channel: a channel that
is already above the target is not brought down. When the timer shows the
fade is over, it is set to zero and the waiting code lets the screen go on.
The 32 colours are handled one after the other in one call, so a step is
the longest piece of this chapter (a few thousand cycles). *Evidence: ROM
static (01:7B40-7BAB); ROM run (about 20,000 calls on the recordings; a
state probe tried every pair of current and target colours, 4,096 of them,
and every colour value for the fade-out step, on both ROMs).*

**The flashes (01:7BAC, 01:7BC4, 01:7BEE).** A battle flash and a magic
flash share one mechanism: a counter of ten is set, and on each frame the
step at 01:7BEE counts it down. On the odd counts a run of colours of the
working copy is painted white; on the even counts the whole working copy is
put back from the target copy, so the screen blinks five times in ten
frames. The battle flash whitens 13 of the sprite colours; the magic flash
whitens 13 of the background colours, or 14 for three particular spells
(identified by the spell number kept in the effect byte). The flash waits
(01:7BAC-7BED) are the same frame wait as the fades. *Evidence: ROM static;
ROM run (battle and boss recordings, 100% in C++; the state probe ran every
counter value from 0 to 255).*

**The palette effects (01:7C85 to 01:7D16).** Four effects play with the
colours for a story moment: a cycle of colours that runs for 111 frames and
every sixteenth frame brings in eight new colours (01:7C85, the field menu's
fifth result), a single load of five colours (01:7CA6, an item's effect), a
long sequence that plays one table of 24 steps of three frames three
times (216 frames) and then 13 groups of six colours of eight frames each
(01:7CBB, the field menu's sixth result), and a scene sequence of nine
steps of 24 frames each (01:7CE4). The table steps share one routine
(01:7CF1): it takes a pair of colours from the table, puts the first in
the background colour and in six colours of the last group (colours 10 to
15) and the second in colour 7, then waits the step's frames. All of them first switch the third
cartridge window to bank 3, where their tables live, and leave it there.
No recording reaches these effects (they belong to the story and to rare
menu results); they were checked on both ROMs by running each whole effect
from a real frame beside the original and comparing the two machines, as
well as with state probes of every entry. *Evidence: ROM static
(01:7C85-7D16); ROM run (state probes and whole-effect chains, US and
Brazilian).*


## Completing the drawing boundaries

Milestone 13 adds the interrupt and cartridge-memory handoffs that surrounded
the milestone 12 pieces. The original processor executes each DI and EI as
the last instruction of a declared segment. Restoring saved registers before
EI is now part of that segment too, with the saved words explicitly counted.
The drawing, numbers, prices and text keep their original video timing.

The save-slot window enables cartridge RAM while it draws the names, then
restores ROM before asking which slot the player wants. Interrupts can run
between rows; their own bank and cartridge-control saves remain in order.
Drawing a window without row waits also preserves the old bank. Its one
mapper store immediately before DI is a separate processor-owned boundary,
restricted to that exact window routine and instruction address.

**ROM static:** `00:2FD8`, `00:2FE1`, `00:3036`, `00:315C`,
`00:356B`, `00:35BC`, `00:35C4`, `00:393A`, `00:39B1–39C7`,
`00:3A68–3A8C`, `00:3AC0–3AC3`. **ROM run:** both cartridges' boundary
probes compare registers, memory, clock cycles and the processor's interrupt
state. Complete save-name and immediate-window chains compare every memory
write callback and every video/sound port operation, including values,
addresses and cycles, with cartridge RAM enabled or disabled and several
original bank selections.

The instruction directly protected by EI still executes on the original
processor. Seven drawing/text waits use a simple immediate load at that
point (`00:3030`, `00:30A1`, `00:332B`, `00:3411`, `00:35B6`,
`00:3633`, `00:3A7A`). Other epilogues restore a saved register or return
(`00:319D`, `00:33C7`, `00:3AA1`, `00:3AC3`). These are individually
identified machine scaffolding for milestone 14. They preserve the exact
interrupt-delay step and are not counted as rewritten game decisions.
The chapter's earlier counts and exclusion lists describe milestone 12.
