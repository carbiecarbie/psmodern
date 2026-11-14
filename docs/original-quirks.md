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
Original mode keeps both oddities below; the widescreen option (off by default)
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
  and column. The census never walked to the edge of a world.
- The widescreen bands show the same things, further out; they do not clamp
  or add black (**Maintainer decision**, 2026-11-14).
