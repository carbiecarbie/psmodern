# Original quirks

Odd behavior of the original game that original mode keeps on purpose, and
differences between the US Rev-A and Brazilian cartridges. Each entry gives
its evidence label ([documentation rules](AGENTS.md#evidence-labels)) and
where it lives.

## US and Brazilian versions

The two cartridges share their code and rules
([design, section 5.5](design.md#55-languages)). PSModern keeps every
difference below by running each version's own program and reading its own
text from the player's ROM.

### A different blank tile on the save-name screen

- **Where:** the operand at `01:4181`, the only byte of code that differs.
- **ROM static.** The instruction there fills the spacer cells of the
  name-entry screen. The Brazilian version writes a different blank tile.
  The instruction's length and cycle cost are the same.
- **ROM run.** In one sampled frame of that screen, video memory differed in
  those cells and the picture did not: the cells are black in both versions.

### Conversations that stop in different places

- **Where:** the text and its control codes, in banks `02` and `0C` of each
  ROM. The code that prints them is the same.
- **ROM static.** The Brazilian text is not the US text with other words.
  Its control codes decide where a conversation ends, waits for a button or
  runs into the next message, and in some scenes they differ. Examples: one
  yes/no question waits for a button first only in the Brazilian version;
  two conversations end one message earlier there, one of them leaving out
  a clue; one greeting is skipped there; the ending's last caption waits for
  a button only there.
- A Portuguese mode made by replacing only the words of the US text would
  therefore not behave like the Brazilian cartridge.

### Text length changes timing and random draws

- **Where:** the same text banks; the random draw reads the processor's R
  counter at `00:05C8`.
- **ROM run.** Printing takes about one frame per character, so text of
  another length takes another time. With the same input given to both
  versions, the first random draw on the map came on the same frame but a
  few cycles apart, read a different R and returned a different number. By
  the end of that input the two versions had made different numbers of
  draws.
- This is why each version has its own input recordings. See
  [random numbers](how-it-works/random-numbers.md) and
  [why exact timing matters](how-it-works/master-system.md#why-exact-timing-matters-for-this-game).

## Rules of the original game

### A total at the experience ceiling stops levelling

- **Where:** the addition and the exit that follows it, `00:1773`-`00:177F`
  (the per-character routine of [the award](how-it-works/party.md#giving-experience-after-a-battle)).
- **ROM static.** A character's experience is a two-byte total. When adding a
  reward overflows it, the game stores the largest value and returns before it
  tests the level. A character below level 30 whose total reaches the ceiling
  stops levelling for good: every later award overflows again and exits early.
  Original mode keeps this on purpose.
- **ROM static, Inference. Not reachable in play.** The largest reward one
  battle can produce is a few hundred points: the experience of any monster
  the game can name (every group of every encounter pool, every fixed fight
  set by a direct load, and every monster record up to the highest one named)
  times the largest group the setup can build, which is eight for a random
  group of a four-member party and a fixed number for a fixed fight. The
  smallest margin between the last level's threshold and the ceiling is a few
  thousand points, for one of the four characters, and far more for the
  others. A character can hold a total above a level's threshold only by
  carrying the surplus of an award that gave a single level, and the bound
  below accounts for that: starting a character with the largest possible
  surplus at every level, one award at a time, the total before an award plus
  the largest reward stays under the ceiling for every character at every
  level before 30. So no sequence of battles overflows the counter before the
  top level. The bound reads the tables through the machine in
  `tests/experience_tests.cpp` and fails if the ROM data would ever allow it.
- **Unknown.** One fixed fight takes its monster from event data rather than
  an immediate value (`01:69F7`); its identifiers were not enumerated. The
  bound above covers every identifier up to the highest the game names, and
  the records beyond that hold data of another shape (Inference from their
  contents).
- With the experience-rate option on, the ceiling is reachable (a rate of 400
  makes big rewards), and PSModern deliberately differs there: the total
  still saturates, and the character keeps gaining one level per award. This
  is a maintainer decision (2026-11-11)
  (see [party](how-it-works/party.md#the-experience-rate-option)).

## Dungeons

How these work is in [dungeons](how-it-works/dungeons.md). Original mode keeps
every entry below on purpose; the counts come from reading the tables of the
US Rev-A ROM (the Brazilian tables are identical here).

### Dead fight records

- **Where:** the object table of bank `03`, read by `01:6963` and `01:65EE`.
- **ROM static.** Five fixed-fight records (one on floor 12, four on floor 41)
  name cells whose value is not the object marker, and every path that reads the
  table first requires exactly that value, so the fights can never start.

### Shadowed second records

- **Where:** the scan in `01:6963` (and the same scan in `01:65EE`).
- **ROM static.** Ten cells carry two records (nine pairs of money chests and
  one item chest with a money chest). The scan stops at the first record, even a
  done one, so the second record and its done flag are never used.

### The dead exit record

- **Where:** the exit table, read by `01:68BC`.
- **ROM static.** One record (kind FFh) names an open floor cell. The lookup is
  reached only for stairs and doors with the exit bit, so it never runs.

### Exit cells without a record

- **Where:** `01:68BC`, no-record ending.
- **ROM static.** 16 cells (10 stairs up and 6 doors) carry the exit bit but
  have no record; the game mode becomes the world map and the party returns to
  where it entered. **ROM run.** The entrance stairs of Maharu Cave did this.

### Exit cells that start a scene, an interaction or an event

- **Where:** `01:68BC`, kinds of 80h or more.
- **ROM static.** 33 of the 64 exit-bit cells with a record do not lead out
  (30 FFh, one each FEh, FDh and FCh): kind FFh runs a scene, FEh and FDh an
  interaction, FCh an event alone; the event then runs and the party may stay
  in the dungeon. Only 31 lead to the world map through a record. Of the 80
  exit-bit cells in all, 16 have no record. A 31st FFh record names a plain
  open cell and is never reached.

### A chest behind the party does nothing

- **Where:** `01:6963`, the return at `01:69C7`-`01:69CA`.
- **ROM static.** After a back step the object check also looks at the cell
  behind. For a chest there it prepares the reward and the pending byte and
  returns before the dialogue, so the chest stays closed. A fixed fight or an
  event behind turns the party around first and then runs.

### Stairs approaches that land inside a wall

- **Where:** the stairs path of `01:65EE` (`01:6715`, `01:6734`).
- **ROM static.** Stairs put the party two cells forward on the next floor
  without testing that cell. Three approaches from open cells (one on floor 10,
  two on floor 55) land on a wall of the floor above. Every stairs cell has its
  opposite at the same place, so no other landing differs.

### A pitfall that lands inside a wall

- **Where:** `01:661C`.
- **ROM static.** The fall keeps the cell and lowers the floor by one. The
  pitfall on floor 25 lands on a wall cell of floor 24. Of the 33 pitfalls, 28
  land on open floor.

### Cascading pits

- **Where:** `01:65EE` and `01:661C`.
- **ROM static.** The pitfalls on floors 49 to 52 share one cell, so each fall
  lands on another pitfall and the next handler call starts another fall, down
  to floor 47.

### The fake wall's unchecked second step

- **Where:** `01:6755` calling `01:6758`.
- **ROM static.** The call is to the very next instruction, so the step runs
  twice and the second run never reads the cell beyond. **ROM run.** The party
  crossed a fake wall in two steps.

### Doors close on every floor reload

- **Where:** the opened-door bit set at `01:6823`; the layout expanded by
  `01:6D56`.
- **ROM static.** The bit is only in the RAM copy of the floor, rebuilt from the
  ROM at each load: stairs, a pitfall, a resume and re-entering all close every
  door. Chest, fight and event flags are saved and stay.

### A back step towards stairs does nothing

- **Where:** `01:677A`.
- **ROM static, ROM run.** A back step needs open floor behind; stairs, doors
  and walls do nothing. Observed at the entrance of Maharu Cave.

### Encounters armed by turns and back steps

- **Where:** `01:6A58` (sets `$C2D2`) and `00:0F4B`.
- **ROM static.** Every turn and step goes through the animation routine that
  arms the encounter check, and finding an unfinished record arms it too.
  **ROM run.** Turns and a back step triggered fights while the recordings were
  made.

### Fights and events start when their cell is ahead

- **Where:** `01:6963`.
- **ROM static, ROM run.** The object check reads the cell ahead, not the one
  stood on, so a fixed fight or event begins when the party faces it.

## The world map and the towns

How these work is in [the world map and the towns](how-it-works/world-map.md).
Original mode keeps the oddities below; the widescreen option (off by default)
draws around them without changing them.

### The original shows 248 pixels of map

- **Where:** the map load `00:0B6A` sets video register 0 to 26h at `00:0C8E`.
- **ROM static, Hardware documentation.** The value turns on the left-column
  blank: the first 8 pixels of every line show the backdrop colour instead of
  the map. The background name table has 32 columns, exactly one picture wide.
- **ROM run.** In the census of all 18 recordings, the leftmost name-table
  column was stale in every map frame it was checked in.
- **Inference.** The blank hides the column that the scrolling refills, since
  there is no spare column. The reason is not proven; the effect is.
- With widescreen on, PSModern draws those 8 pixels from the map instead
  (**Maintainer decision**, 2026-11-14); with it off they stay blank.

### The camera never stops at a map's edge

- **Where:** the party is fixed on the screen (`01:599F`) and the camera is moved
  by `01:7143`; no code clamps it.
- **ROM static.** Near the edge of a town the screen shows whatever the town's
  sheet holds past it: uniform filler screens (one metatile each, not the same
  on every sheet) and, when close enough, other towns of the same sheet. The
  worlds wrap around instead, since their screen table repeats its first row
  and column.
- **ROM run** (milestone 11 probes). The camera went from screen 7 to screen 0
  and back in all four directions, on foot and in a vehicle; the picture,
  the screen window and the strips matched the original on every frame. The
  recordings still never walk to the edge of a world.
- The widescreen bands show the same things, further out; they do not clamp
  or add black (**Maintainer decision**, 2026-11-14).

### Broken ice is forgotten at a screen change

- **Where:** the ice digger's break `01:756C`; the screen window refill
  `01:723D`.
- **ROM static.** Breaking ice writes the broken metatile (D7h) over the ice
  cell (D8h-DFh) in the screen window in work RAM and its four names in the
  name table; the routine keeps no other record of the cell. The window is
  decoded again from the cartridge when the camera crosses into another
  screen.
- **ROM run** (recording `ice`). The window held broken cells until the
  digger crossed into another screen at frame 1878; from then on it matched
  the cartridge again, while the picture still showed the broken trail.
- **Inference.** Broken ice that scrolls out of the picture after such a
  crossing comes back as whole ice when the party returns, because the
  column and row producers draw from the window. Not played back in a
  recording.
- With widescreen on, the bands keep showing a broken cell while it stays
  in the wide picture and the name table still shows it, so the trail does
  not end at the edge of the original picture.

### A vehicle's new column shows late

- **ROM run** (recording `vehicles`, frame 3078). On the first frame of a
  Land Rover step to the right, the names of the column entering on the right
  were still being written while lines 0-9 were drawn, so those lines show
  the column leaving on the left at x 254-255 for one frame. Original mode
  keeps it; with widescreen on, the band beside it already shows the new
  column.

### Found in the picture updates (milestone 11)

Quirks of the frame-interrupt work of the map (`00:0235`, `00:029F`,
`01:588B`, `01:61F5`, `01:62BC`, `01:73D0`, `01:7C18`).

#### A firing tile animator skips the others for the frame

- **Where:** `01:630A`, its end at `01:6343`; the six calls in `01:62BC`.
- **ROM static.** After copying an animation step, `01:630A` removes its
  caller's return address from the stack and returns to the frame handler
  directly, so the animators listed after the one that fired are not looked
  at in that frame; their countdowns wait one frame.
- **ROM run.** With the first two animators due in the same frame, the second
  one's countdown stays untouched, and with the first one off the second fires
  (`tests/map_video_tests.cpp`).
- **Inference.** Probably deliberate, to keep the frame's work short; the
  effect is a one-frame delay of an animation when two fall due together.

#### Only one party member's pose is sent per frame

- **Where:** `01:61F5`, its jump at `01:6214`.
- **ROM static.** The pose copy is reached by a jump, not a call, and returns
  to the frame handler, so after the first object whose pose changed the
  others wait for the next frame.

#### Long animations run into the next picture

- **Where:** `01:6337`-`01:6340` (the copy loop of `01:630A`), `00:02D0`
  (the loading copy of `00:029F`).
- **ROM run.** In the checkpoint recordings the longest animation copies and
  the whole loading copy are still running when the next frame begins (the
  milestone 11 census counts the calls that cross it).

### Found in walking and the camera (milestone 11)

Quirks of the step, the camera and the screen window (`01:7143`, `01:723D`,
`01:72A6`, `01:733A`, `01:744B`).

#### Held directions are tried in a fixed order

- **Where:** `01:714D`-`01:7209`.
- **ROM static.** A new step tries up, then down, then left, then right. With
  a diagonal held, the party walks up or down when it can; a blocked direction
  passes on to the next held one, so up and right against a wall above walks
  right. Nothing remembers the direction of the last step.

#### A step under way is never checked again

- **Where:** `01:718B` (and the same test before each of the four terrain
  calls).
- **ROM static.** The terrain check runs only when a step starts. The 15 or
  7 frames that follow move the camera without asking, whatever the ground
  under the next frames is.

#### Standing still sends a sound request every frame

- **Where:** `01:722D`-`01:722F`.
- **ROM static.** Each frame the party stands (no direction held, or all held
  ones blocked), the step writes D6h to the sound request byte `$C004`.
- **Unknown.** What the sound driver does with D6h was not studied.

### Found in the sprites and the object list (milestone 11)

Quirks of the object engine and the map's object types (`01:576A`,
`01:5853`, `01:599F`). Original mode keeps both.

#### Only the first four listed objects are put in order

- **Where:** the sort in `01:576A`, `01:57B9`-`01:57DB`.
- **ROM static.** The engine lists the objects that want to be drawn (up to
  eight) and orders the list by each object's vertical position, the larger
  first, before drawing. The ordering is three passes of a triangular compare:
  the first place is compared with the next three, the second with the two
  after it, the third with the fourth. Places five to eight are never compared
  with anything, so they stay in the order of their slots.
- **ROM run.** A state probe of the milestone 11 tests fills the list with
  eight random positions and runs the sort on both cartridges: the first four
  places come out in descending order, the last four unchanged.
- **Inference.** On a walking map the objects are the party (up to four) and
  one vehicle, at most five, so only the fifth can stay out of the ordering;
  the sort looks written for a list of four.

#### The drawing leaves slot 2 on the sprite bank

- **Where:** `01:5806`, inside the draw loop of `01:576A`.
- **ROM static.** Before each object is drawn the engine writes the bank of its
  sprite data (bank 3 for slots 0-3, bank 21 for slots 4-7) to the slot 2
  register, and never puts the previous bank back.
- **ROM run.** The same probe checks that the register is written before the
  first draw call: slot 2 holds bank 3 when the first listed object is one of
  slots 0-3. After the last object the bank of that object stays mapped when
  the engine returns (ROM static).

### Found in what the ground allows (milestone 11)

Quirks of the terrain, vehicle and ice code (`01:7764`, `01:74E4`, `01:756C`,
`01:7602`, `01:7656`, `01:76C1`, `01:7732`). All are kept as they are.

- **The step that breaks ice is not stopped by it.** `01:756C` puts the broken
  ice in place of the ice and then returns the attribute of the broken ice, not
  of the ice. **ROM static, ROM run.** On areas below 4 the ice's attribute
  has bit 0 set and the broken ice's does not; in the `ice` recording the
  probe broke ice in 96 of 193 reads, and the C++ runs both outcomes against
  the original.
- **Every kind of transport but three takes the ice path.** `01:74E4` knows the
  road, on foot, 4 and 8; any other nonzero `$C30E` takes the two-probe ice
  path. **ROM static.** State probes with the values 1, 14h and FFh run the
  C++ and the original side by side with the same result. **Inference.** The
  game only ever stores 0, 4, 8, 0Ch, 10h and 11h there, so play never
  shows the difference.
- **The lookup leaves bank 03 in the upper slot.** `01:7764`, and the ice path
  of `01:756C`, switch bank 3 in through `$FFFF` to read the attribute table
  and never switch back. **ROM static, ROM run.** The C++ writes the same
  mapper register at the same moment, which is why those segments carry the
  mapper permission.
- **The Land Rover's placement tries four spots, one of them diagonal.**
  `01:7656` tries stay, right, down, then right and down together, because it
  compares the try number only with 4 and 8 (try 0Ch is neither, so it takes
  both moves). **ROM static.** The branch (`01:769A`-`01:76B2`) was reached by
  no recording; the state probes run it on both cartridges against the original.

### Found in entrances, transfers and special ground (milestone 11)

Quirks of `01:77AC`, `01:7877`, `01:787B`, `01:7908`, `01:792A` and `01:7A0C`.
Both cartridges have byte-identical code and entrance tables here.

#### A transfer ends the map loop's frame

- **Where:** `01:7908` (`pop hl; ret`), reached from `01:78BA`, `01:78E2`,
  `01:7905`, `01:7959`, `01:7983`, `01:79B4` (through `01:7877`) and `01:7A09`.
- **ROM static.** The reset after a transfer throws away the return address of
  the code that called the tile check and returns to the caller's caller, so
  the rest of the map loop (from `00:0B1A`) does not run in the frame of a
  transfer. **ROM run.** The C++ rewrite follows the returned word on the
  checkpoint recordings (the road, a dungeon exit) and on constructed stacks.
  Kept.

#### The entrance scan leaves bank 03 in the third slot

- **Where:** `01:783F`.
- **ROM static, ROM run.** Every scan writes bank 3 to `$FFFF` and never
  restores the previous bank, whether or not a record matches. The rewrite
  writes it too, so original mode keeps it.

#### Entrance records cover two rows and two columns

- **Where:** `01:784A`-`01:7864`.
- **ROM static.** A record matches the party's row when it is the record's row
  or the row after it, and its column when it is the record's column or the
  one after; at a page's last row the next row is the first row of the next
  page. **ROM run.** The constructed-table probes matched on both rules.

#### A special-ground check that cannot fire

- **Where:** `01:79D3` (`ret c` after `cp C7`).
- **ROM static.** The code at `01:79D1` is reached only with the tile id at or
  above `C7h`, so the return can never be taken. Nothing to preserve beyond
  keeping the instruction in the cycle count.

#### Damage clears the whole status byte of a fallen character

- **Where:** `01:7A1A`-`01:7A1C`.
- **ROM static, ROM run.** A character whose hit points reach zero from a
  damage tile (including one whose hit points equal the damage) gets its first
  status byte zeroed, not only the alive bit; the fallen character's bit goes
  to the mask `$C2F0`.

### Found in map loading and the road (milestone 11)

Quirks of `00:0B08`, `00:0B6A` and the road `00:0E8B`.

#### A held button opens the menu, a pressed one skips the road

- **Where:** the button test at `00:0B35`-`00:0B3F` in the walking loop and the
  one at `00:0E97`-`00:0E9C` in the road.
- **ROM static.** The walking loop tests the buttons that are *held*, not the
  ones just pressed, and only in a frame in which no step is in progress. A
  button kept down through the end of a step therefore ends the walking mode in
  the first free frame, with no new press needed. The road tests *new presses*
  and, on one, skips the rest of the walk at once, before the object engine
  runs in that frame; the buttons held on the road are overwritten each frame by
  the leg's own script (`$C2EA`), so only a press has any effect there.
- **ROM run.** The road recording walks two legs of 120 frames each (the loop
  ran 240 frames) and presses no button during them, so the skip was run only
  in the scenario probes of the milestone, from a town save.
- **Unknown.** Whether the designers meant the skip; the code does it.

#### The road's countdown only runs between steps

- **Where:** `00:0EA7`-`00:0EB3`.
- **ROM static.** The road's walking time (`$C2EB`) is counted down only in
  frames in which the object engine has left no step in progress (`$C265` is
  zero after it). The length of a leg is therefore not a count of frames, and a
  leg whose party stayed mid-step in every frame would not end by itself.
- **Unknown.** How many frames of a leg count; the road recording's legs were
  not measured against the table's counts.

#### The tune is not restarted when it does not change

- **Where:** `00:0C97`, called by the map loader at `00:0C8B`.
- **ROM static.** A request equal to the last one (`$C2F4`) changes nothing, so
  entering a map whose tune is the one already playing does not restart it.
- **ROM run.** The state probes ran both outcomes (the same request and a new
  one) against the original.

## Menus and windows

How these work is in [menus and windows](how-it-works/menus.md). Each part of
milestone 12 records here the oddities it keeps, with evidence and location.

### Found in the window engine (milestone 12)

#### The five-digit printer counts without clearing carry

- **Where:** `00:3054`-`00:3069` (inside `00:3036`).
- **ROM static.** The thousands and hundreds of the five-digit number start
  counting from `ld a,FFh`, which leaves carry as it was, and the subtraction
  they use takes carry away too. The `add hl,de` before each count always
  sets carry, which would take one extra away; the money printer (`00:35D5`)
  clears it with `xor a` before each count. What saves the five-digit
  printer is the digit helper `00:3645` called in between, whose `and 0Fh`
  clears carry.
- **ROM run.** The original prints 0, 7, 345, 1000, 2345, 10000, 12345,
  40009 and 65535 exactly (the engine probes, both ROMs). PSModern keeps the
  code as it is, flags included.

#### A shop line of an empty item repeats its top row

- **Where:** `00:393E`-`00:3942`.
- **ROM static.** When the record's item is 0, the printer forces the top
  tile row, so the line's bottom row is drawn with the name's top tiles and
  six blanks, and no price is printed.
- **ROM run.** The probes draw item 0 on both rows and get the same tiles; no
  recording reaches this path.
- **Unknown.** Whether any shop record of the game holds item 0.

### Found in the windows (milestone 12)

#### Closing a window is drawing the saved copy back

- **Where:** every close routine, for example `00:36FB`, `00:3A12`, `00:38C1`.
- **ROM static.** A close routine loads the same screen position and size as the
  opening one and calls the window engine with the saved copy as the picture.
  Nothing is erased; if the holding area (`$D700`-`$DEAF`) were overwritten
  while a window is open, the close would paint that data.
- **ROM run.** The state probes ran every close routine against the original.

#### A member number of 3 or more draws Noah's box

- **Where:** `00:2F93`-`00:2FD5`.
- **ROM static.** The refresh of one party member's box picks Alis, Myau and
  Odin for 0, 1 and 2 (`dec a` and `jp z` at `00:2FB9`, `00:2FC7`) and then jumps
  to the common code for Noah without a further test, so any larger number
  draws Noah's box.
- **ROM run.** The state probes called the entry with random numbers in the
  accumulator and the rewrite equals the original on all of them.
- **Unknown.** Whether the game ever calls it with a number above 3.

#### The enemy windows skip the party's HP column for one marker value

- **Where:** `00:3127`-`00:312C`, `00:31A7`-`00:31AC`.
- **ROM static.** When `$C2E6` holds 49h the list draws no HP column for the
  party and the close does not restore it; any other value draws it.
- **ROM run.** Both cases were met in the state probes (`$C2E6` set to 49h on a
  third of them).
- **Unknown.** What puts 49h there.

#### The HP column shows one byte

- **Where:** `00:3145`-`00:3148`.
- **ROM static.** The column reads the byte at offset 1 of each party member's
  record (`$C401`, `$C411`, ...) and prints it as an 8-bit number; no other
  byte of the HP is read there. The status window prints experience with the
  16-bit printer (`00:37FF`-`00:3805`) and the other fields with the 8-bit one.

#### Windows that change the slot-2 bank and do not put it back

- **Where:** `00:3A2D` (two-choice window, bank 18), `00:3726`-`00:3729`
  (equipment window, bank 2), `00:38E5`-`00:38E8` and `00:392B`-`00:392E` (shop
  list, bank 3 in and out).
- **ROM static.** The two-choice and equipment windows write the bank register
  for slot 2 and leave it. The drawing routine at `00:3A57` saves and restores
  whatever was there when it was called, so its own bank switch does not undo
  them; only the shop list sets bank 3 again after the choice.
- **Inference.** The code that follows these windows must not depend on the
  previous slot-2 bank, or the game would misbehave; the recordings showed no
  difference between routines on and off.


### Found in choosing (milestone 12)

#### The item list loses a confirm pressed together with a direction

- **Where:** `00:2DB8` and `00:2DFE`.
- **ROM static.** When Up or Down is pressed, the item list replaces the
  button copy it keeps in C with the direction bits only, and tests for button
  1 or 2 on that copy. The cursor loop of `00:2D51` reads the buttons again
  instead (`00:2D8B`). So in the item list a confirm pressed in the same picture
  as Up or Down is ignored, while in every other menu both take effect.
- **ROM static.** The rewrite renders the same copy and the same test, so the quirk is kept.

#### Button 1 leaves the item list with A = 0

- **Where:** `00:2E0C` to `00:2E12`.
- **ROM static.** The leave clears A and returns at once when button 1 was the
  one pressed; the choice is read only on the other path.
- **ROM run.** The state probes took the exit with random button, count and
  page states.

#### Up and Down together cancel out

- **Where:** `00:2D78` to `00:2D87`.
- **ROM static, ROM run.** Both directions are applied one after the other to
  the same choice, so the choice ends where it started (including the wrap).
  The scenario probe pressed both in one picture on the field menu and the
  machines stayed equal with routines on and off.

### Found in the field menu (milestone 12)

Quirks of `00:1BE1`-`00:1C79`, `00:1CA1`-`00:1DDB`, `00:2168`-`00:21FA`,
`00:2201` and `00:26C8`-`00:297A`. Both cartridges have byte-identical code
here (compared over these ranges). All of it is kept in original mode.

#### The inventory search answers "zero" when the item is there

- **Where:** `00:282E`, called from `00:28B4` and by the shops.
- **ROM static.** One might expect a routine that looks for an empty slot. It
  compares each of the 24 inventory slots with the item number in A and answers
  "zero" when a slot holds it. Callers use it as "does the party already have this
  item".
  **ROM run.** The state probes ran it over random inventories.

#### The two search places guard against a second item in different ways

- **Where:** `00:2877`-`00:28B4`.
- **ROM static.** The first place (location 7) gives its item once: it needs a
  story byte, `$C507`, to be set and not `FFh`, and writes `FFh` there when it gives
  the item. The second (location 1) does not write its story byte, `$C509`; it
  refuses only when the third member's shield byte is already the item or when the
  item is already in the inventory. An item given at the second place and then
  dropped or sold, and not carried or worn, can be found there again.
  **ROM run.** Both places were run against the original, with the flag, the
  shield byte and the camera at and off the place.

#### A trap can fall on nobody, and never kills

- **Where:** `00:2950`-`00:2979`.
- **ROM static.** A chest whose byte `$C80F` is not `3Dh` hurts one member picked by
  two bits of the random generator (`3Eh` hurts all four). A dead or absent member is
  skipped, so a pick can hurt nobody. The damage is a third of the current hit
  points, rounded down, taken off the hit points, so a member with one or two
  hit points loses nothing and nobody dies. **ROM run.** The branch for all
  members (`3Eh`) was reached by no recording; the probes run it on both cartridges.

#### Using an item always starts with the first living member

- **Where:** `00:21C1`-`00:21D3`.
- **ROM static.** Before the use / equip / drop window the menu searches
  members 0 to 3 for the first one alive and makes it the current character
  (`$C2C2`). The player is not asked. Only equip asks (it opens the member choice
  itself).

#### An empty item list is opened and closed again without a redraw

- **Where:** `00:216C`-`00:2172`.
- **ROM static.** With no items the menu opens the item list window and closes it
  at once, without the party window redraw that the other leaves do (`00:21F5`).

#### The fourth member is drawn as the third in the spell window

- **Where:** `00:1CC8`-`00:1CCC` (status), `00:1D77`-`00:1D7B` (magic).
- **ROM static.** The spell list window takes the member number minus one for
  member 3, so the fourth member's spells are drawn in the third member's row; the
  count and the record are still the fourth member's. In the magic menu the
  third member (number 2) is refused with its own message and the 15-byte spell
  table has three rows, so the fourth member uses the third row.

#### A saved game leaves the field menu for good, only into a slot that was unused

- **Where:** `00:1D33`-`00:1D4C`.
- **ROM static.** The copy into the slot runs whatever the slot held. Only a slot
  whose used flag was clear before ends the menu (game mode 16, two return
  addresses dropped); saving over a slot in use shows a message and returns to the
  menu. **ROM run.** The probes run both zero-flag states on both cartridges.

#### The upper three bits of a spell byte are ignored

- **Where:** `00:1DA7`.
- **ROM static.** The spell table keeps one byte per slot and the menu uses only
  its low five bits as the spell number.
  Whatever the upper bits hold is not looked at here.

### Found in shops, the hospital and the church (milestone 12)

#### Buying a vehicle-class item the party already carries costs money for nothing

- **Where:** `00:2C09`-`00:2C0C` and `00:2C0F`-`00:2C1D`.
- **ROM static.** In a shop, items 21h to 23h get an already-carried check, but
  the game takes the money and redraws it (`00:2C09`-`00:2C0C`) before it looks
  the item up in the party's list (`00:2C0F`-`00:2C1D`). A party that already
  carries it pays, hears the line that follows a purchase and gets nothing.
- **ROM run.** State probes on both ROMs reach the path (`00:2C1D` taken) and
  equal the original.

#### The Road Pass takes three tries

- **Where:** `00:2C46`-`00:2C8C`.
- **ROM static.** A shop line numbered 40h or more is not a real item: the
  first two choices only show a line (0142h, 0144h) and count in `$C2EC`; the
  third buys the Road Pass (item 33h) at that line's price and clears the
  counter. A party that already carries the Road Pass is sent back to the
  counter, which was just cleared, so the first line returns.
- **ROM run.** State probes on both ROMs.

#### The hospital's price is a sum of two bytes

- **Where:** `00:29E4`-`00:29F3`.
- **ROM static.** The price is the missing hit points plus the missing magic
  points, each a one-byte subtraction, added in one byte; the high byte of the
  price is always zero, so a sum of 256 or more would wrap.
- **Unknown.** Whether the game's maxima allow such a sum was not checked.

### Found in the fades (milestone 12)

#### A fade only raises colours on the way in

- **Where:** `01:7B92`-`01:7BAB`.
- **ROM static.** The fade-in compares the whole colour value with its target,
  not each channel: it adds a blue step while the sum does not pass the
  target, then green, then red. A channel that is already above its target is
  never lowered, so the screen can end a fade-in brighter than its target
  palette. The long fade-in starts from black (`01:7B20` clears the working
  copy), and the short one (`01:7B18`) starts from whatever the buffer holds.
- **ROM run.** A state probe ran all 4,096 pairs of current and target colour
  against the original on both ROMs; the recordings fade in from black only.

#### The fade-out takes red, then green, then blue

- **Where:** `01:7B5F`-`01:7B79`.
- **ROM static.** One step lowers only the first non-zero channel in the
  order red, green, blue, so a white colour takes nine steps and a pure blue
  takes three. Fade-in climbs the other way round, blue first.
- **ROM run.** All 256 colour byte values were probed on both ROMs, the two
  unused top bits included (they are never touched by the step).

#### The fade acts only on every fourth frame

- **Where:** `01:7B40`-`01:7B44`.
- **ROM static.** A counter at the start of the step is lowered on each frame
  of the fade kind; the step runs only when it goes below zero and then
  restarts from three. A fade's length in frames depends on where this counter
  stood when the fade began (a first step comes after 1 to 4 frames).
- **ROM run.** The counter is not reset by the fade waits, so every
  recording's fades start from the leftover value; the C++ step matched the
  original in about 20,000 calls on 14 US and 6 Brazilian recordings.

#### A flash is five blinks, and its colour range differs by spell

- **Where:** `01:7BAC`-`01:7BED`, `01:7BEE`-`01:7C17`.
- **ROM static.** A flash sets a counter of ten; each frame the step lowers it
  and, when the new value is odd, paints a run of working-copy colours white,
  otherwise restores the whole palette from the target copy. The battle flash
  paints 13 sprite colours (19 to 31); the magic flash paints 13 background
  colours (3 to 15), or 14 (3 to 16, one sprite colour more) when the effect
  byte holds 46h, 49h or 4Ah.
- **ROM run.** State probes ran each effect byte and every counter value from
  0 to 255; a run length of zero would paint 256 bytes, which no caller asks
  for and no probe tried.

#### The palette effects leave slot 2 on bank 3

- **Where:** `01:7C85`, `01:7CA6`, `01:7CBB`, `01:7CE4`.
- **ROM static.** Each effect switches the third cartridge window to bank 3
  for its colour tables and does not put the previous bank back, so a C++
  replacement must keep the write. **Unknown:** whether any caller depends on
  the bank left behind.
- **ROM run.** The probes ran the effects with every window mapping
  (including cartridge RAM) against the original, and whole effects from a
  real frame equal to the original, on both ROMs.

#### The palette cycle changes colours after 15 frames, then every sixteenth

- **Where:** `01:7C85`-`01:7CA5`.
- **ROM static.** The effect counts 111 frames down from 6Fh and copies eight
  colours into the last group of the working copy whenever the low four bits
  of the count are zero: six times, the first after 15 frames. The tables are
  in bank 3.
- **ROM run.** Whole-effect chains from a real frame (both ROMs) matched the
  original; no recording reaches the effect.

## Battle effects

### Cancelling revival still spends its MP

- **Where:** `00:20BF`, `00:20C3`, `00:20C7`, `00:20D8`.
- **ROM static.** Revival commits the MP subtraction before opening its
  recipient picker. Cancelling closes that picker without restoring the
  points. Choosing an already active recipient also leaves the debit in
  place. Ordinary HP recovery validates its recipient before committing its
  own debit.
- **ROM run.** On both cartridges, complete calls from a real battle frame
  cancel the revival choice and confirm an unchanged recipient alongside
  the actual nonzero ROM spell cost spent by a different caster. Active
  refusal and inactive restoration are checked separately against the
  original, with state, cycles and ordered sound output compared.

### Some strength gates use the first records rather than the caster

- **Where:** `00:192B`, `00:1B0D`, `00:1F36`, `00:1FDF`.
- **ROM static.** The strength checks for talking, one communication spell,
  blocking and weakening read the first party member's attack field and the
  first enemy's attack field directly. They do not select the currently casting character's attack or
  the enemy that will later receive the effect.
- **ROM run.** Both cartridges pass every pair of byte strength values for
  the blocking and weakening gates while the caster's attack is kept
  different from the first party member's. Their control flow matches the
  fixed first-record comparison, including equality and the ordering of the MP debit.

### Direct death ignores protection after the Mirror Shield check

- **Where:** `00:151A`, `00:1544`.
- **ROM static.** The direct-death action checks the Mirror Shield in Odin's
  record specifically. After that check it consumes protection but clears the
  selected actor's activity and HP without testing protection again.
- **ROM run.** Prepared complete calls confirm that a high protection value
  does not prevent that direct death.

### The Crystal-dependent group action can wrap HP

- **Where:** `00:1561`, `00:1514`, `00:15C2`.
- **ROM static.** The group action uses physical variance when the inventory
  contains the Crystal. Without it, the shared HP writer receives a positive
  amount 1..16. Its carry/clamp rule usually produces zero HP; if HP plus that
  amount overflows a byte, it keeps the wrapped result.
- **ROM run.** A prepared 250-HP party checks each target's actual draw and
  resulting HP on both cartridges.

### Escaper and Magic Hat are spent even when refused

- **Where:** `00:23E2`, `00:2419`.
- **ROM static.** Escaper is consumed in battle before the shared escape
  effect checks permission, so a refusal can still spend the item. Magic Hat
  (following its native removal call) is also already spent when field use is
  refused.
- **ROM run.** Complete prepared calls confirm both cases and their unchanged
  alternatives on both cartridges.

### Nuts have two acceptance routes

- **Where:** `00:25C3`, `00:25EF`.
- **ROM static.** Castle location 17h checks field/battle context, while
  scene B0h plus auxiliary FFh reaches the same request through `00:25EF`
  without that additional battle check. The door-key handlers similarly gate
  by dungeon interaction and cell state rather than battle state.
- **ROM run.** Prepared battle and field calls confirm the distinction; this
  does not claim the prepared combination is reachable in ordinary play.

## Scenes and saves

### Cartridge initialization preserves its last four bytes

- **Where:** `00:0847–0852`.
- **ROM static.** The initialization seed and overlapping copy clear the first
  8,188 cartridge RAM bytes. The final four bytes are outside the copy.
- **ROM run.** Both-release initialization/re-entry chains preserve four
  distinct trailing canaries while matching the original's whole state and
  ordered writes across frame boundaries. **Unknown:** whether game code
  assigns a meaning to these four bytes.
