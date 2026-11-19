# Title and saved-game choices

The title screen first offers a new game or the saved-game menu. The cursor
position and number of choices are set before the shared menu handles input.
A new game switches to its initialization path; Continue first checks whether
any of the five saved slots is occupied. If all five are empty it starts a new
game instead. Otherwise it opens the saved-game conversation.

**Since milestone 14.** The Z80 is no longer in the program. Where this chapter says that a
stretch stays on the Z80, is left to the original code, runs on the emulated processor, or falls
back when an interrupt or a frame boundary is near, it describes the milestone the text was
written for. Today every such instruction is rendered C++, an interrupt is accepted at the exact
instruction even inside a piece, and a state a piece cannot render stops the game with a message
([interrupts](interrupts.md), [development](../development.md#registered-routines)).

The cartridge holds a separate occupied flag for each slot. The game enables
cartridge RAM only while checking a flag, then restores the cartridge control
value. The processor executes each control-register store as the last
instruction of a segment, preserving the exact moment at which RAM becomes
visible. The flag helper preserves the zero result of its check while it
restores cartridge control, so callers can decide whether the slot is empty.

**ROM static:** US Rev-A and Brazilian `00:05D0–05E5`, `00:0634–0652`,
`00:073A–074C`. The menu conversation prepares its dialogue pointers at
`00:0679`, `00:0684`, `00:0694`, `00:06C5`, `00:06D0`, `00:06E4` and
`00:06EF`; text remains read from the player's cartridge.

**ROM run:** the segment probes compare complete state, elapsed cycles and
ordered port operations against the original processor on both identified
releases, including the occupied flag in each of five slots and no occupied
slots. These probes establish those segment paths; they do not establish an
entire opening or ending sequence.

Continue enables cartridge RAM before restoring a saved slot. **ROM static:**
`00:069D–06A1`. **ROM run:** the directed chain begins with ROM visible or
with cartridge-control values `$08`, `$0C` and `$88`, checks the control-store
callback at seven cycles after entry, restores each of the five slots and
compares all payload writes, final mapping and completion observation. The
machine model maps both control values to its eight-KiB SRAM.

## Starting and the opening picture

A new game clears the party and world state, equips the first character and
unlocks her statistics. The clear starts with one zero byte and copies that
byte forward through overlapping memory. The seed store ends its own window;
the copy can then pause at an interrupt and resume with the same remaining
count and pointers. The host can still observe the completion of this clear
and of a saved-game restore at their original instruction boundaries.

The opening loader clears temporary objects, installs the initial palette,
selects the graphics and layout banks, starts the sound request and enables
the picture. It advances the game mode before drawing begins.

**ROM static:** `00:05E8–0633`, `00:06A2–06C4`, `00:0750–07B7`.
**ROM run:** both-release probes compare the rewritten segments with original
execution, including partial overlapping copies. Whole-sequence visual evidence
is a separate replay check.

## Flying and landing

Flight keeps a fractional scroll position as well as the visible tile row.
When a movement crosses a tile boundary the game refreshes the row strip; a
larger crossing also refreshes the screen window. Near the destination, a
second movement step slows the vertical motion, updates the remaining landing
counter and chooses the destination's palette and layout. The route record
comes from the cartridge, including the colours it supplies.

**ROM static:** `00:0998–0A49`. **ROM run:** randomized flight positions,
scroll fractions and landing counters exercise the alternative returns and
updates on both releases. State, cycles and ordered ports are compared.

## Naming a saved game

The name editor keeps the selected key, the text position and a short repeat
counter separately. A held direction waits for the repeat counter; a new
press moves immediately. The position helper translates each character into
the two-tile cell where it is drawn. Writing a glyph updates the screen's
memory copy before the video transfer. The cursor uses one sprite over a
letter and several over the wider action keys.

Confirming copies the short name into its working buffer, selects the slot's
name address from the cartridge table, enables cartridge RAM, writes the two
name rows and restores cartridge control. The saved mode decides the next
game mode. The five-byte name, ten-byte rows and destination addresses are
read and computed from the player's cartridge at run time.

**ROM static:** `00:3EB9–3F68`, `01:402C–4139`, `01:413A–414E`.
**ROM run:** both-release segment probes cover all 256 text-position inputs
and repeat-counter values, state, cycles, ports and a frame-edge fallback for
every listed segment. These checks cover the listed segments; cursor search
and the complete name-editor interaction require separate path coverage.

The name-layout incrementing run stores a regional literal between its glyph
cells: US Rev-A writes `$C0`, while the Brazilian release writes `$20`.
The renderer reads that byte from the mapped cartridge instruction. **ROM
static:** `01:4180`. **ROM run:** a directed both-release formatter probe
checks the identified literal and all 32 destination cells at `D2CC`, `D34C`
and `D3CC` (eleven, eleven and ten cells, respectively, four bytes apart),
while the oracle compares the complete segment state, cycles and ports.

In the Brazilian `save-name` recording, the player loads slot one and saves
into the empty third slot. The editor draws the keyboard, accepts three
letters, erases one and writes the name to cartridge RAM on confirmation.
**ROM run:** the store at `01:4180` runs 32 times. The cartridge image
after returning to the dungeon matches the reference emulator byte for byte.

## The scripted finale

The finale uses the same portrait transition as the earlier conversations.
It preserves the requested portrait across the fade, chooses its palette,
graphics and window layout from a cartridge table, clears the lower panel,
then fades the new portrait in. A saved pointer survives the graphics loader
on the stack; its later segment consumes that same saved word.

The final scene sets the music and background, presents the conversation,
then changes to a dungeon view for the credits route. Its cartridge script
contains movement commands, a wait command and an end marker. A movement
command is delivered through the ordinary dungeon handler. A wait preserves
the script pointer; an end marker stops the music, clears the credits latch
and eventually returns to the title mode. The script and dialogue are read
from the player's ROM.

**ROM static:** `01:454E–467B`, `01:46D1–471B`. **ROM run:** both releases'
segment probes compare state, stack offsets, cycles and ports. The US
`ending` recording follows the final victory through the credits on the
rewritten finale and matches the reference emulator on every frame.

The name editor's cartridge stream draws runs of repeated cells, consecutive
cells and literal cells. Separate helpers (`01:41AB`, `01:41B2`, `01:41D5`)
write the attribute bytes that go with the frame and keyboard cells.
The glyph transfer updates the two screen tiles through the video ports.
Direction dispatch is probed for every pressed-byte value and every held
direction combination, and movement across the fixed-bank boundary explicitly
requires the second fixed bank to be mapped.

**ROM static:** `00:3F73–3FFF`, `01:4002–402B`, `01:4152–41FB`.
**ROM run:** ordered video writes and their cycle offsets, frame-edge fallback
and 4,096 direction-dispatch input combinations match on both releases.

## Cartridge memory initialization and scene animation

At startup the game compares the saved cartridge header with a ROM header,
repeating the comparison sixteen times and counting mismatches. Eight or more
failed comparisons cause initialization. Initialization writes one zero byte,
then copies that byte forward through overlapping cartridge memory. The copy
can stop between bytes at an interrupt or a frame boundary and resume with
the remaining count intact. The game then restores the empty-slot names and
header from the player's cartridge and maps ROM back into the third window.
The last four bytes of cartridge RAM are outside this initialization range.

**ROM static:** `00:07DA–0806`, `00:0847–086E`. **ROM run:** both releases'
probes cover matching headers and a mismatch at every header-byte position.
Observer chains compare every write's address, value, cycle and current
mapping, including two cartridge-control bank selections and initialization
re-entered with partially written cartridge RAM.

Scene animation runs only when the scene's enable flag and interaction kind
select it. Each animation has its own countdown and frame index. A frame
selects a group of tile strips from the cartridge; the uploader places each
strip in video memory. The two colour effects have separate countdowns and
rotate small groups of palette values while the relevant game mode is active.
Frame indices wrap at the original sequence length, including inputs already
outside the normal range.

**ROM static:** `01:63A5–63B7`, `01:63CE–6505`. **ROM run:** both releases'
probes test all 256 countdown/frame inputs for the three tile sequences and
two palette steps. Every video write is compared in order and at its original
cycle offset. Initialization and Continue separately compare completion hooks
and SRAM remapping at five frame offsets and all five save slots.

## Choosing and presenting an interaction

An interaction waits for input, installs any requested visual object and
chooses its script through the cartridge's dispatch table. The environment
can substitute another picture for the same requested interaction. Loading
that picture selects its palette, graphics and layout from the cartridge;
a blank-picture request fills the screen with the original two-byte pattern.
When the interaction ends, temporary objects and script flags are cleared.
The current vehicle decides whether play returns to walking, a dungeon or a
vehicle transition.

The scene-object loader at `01:617D` first clears the eight object slots and
all eight enemy work records. A cartridge record then selects eight colours
for both the target palette and its working copy, so the new object starts
with matching colours. A second record supplies the first three object fields;
a following single-byte copy supplies another field before the graphics bank
and source pointer are read. The graphics upload follows these five buffer
copies.

Each buffer copy preserves source order and resumes with the remaining count
and both pointers intact when its cycle window ends. The two zero-fill seeds
still execute at their original native instruction boundaries. At the two
eight-colour copies, a completed C++ transfer may precede a final native
transfer that finishes across the frame boundary; a copy with no completed
C++ transfer keeps ordinary admission. IRQ and NMI limits still cover the
whole transfer sequence. **ROM static:** `01:617D–61EA`. **ROM run:** both
releases compare full state and every seed/copy write, including its callback
cycle, around frame, NMI and pending-IRQ boundaries.

Some visual records also add an overlay to the screen copy. Each cell has
two bytes. A cell whose first byte is zero leaves the old cell untouched;
an opaque cell copies both bytes. Each new destination row starts 64 bytes
after the previous one, while the source advances through consecutive cells.
The renderer preserves the row dimensions and destination on the stack.
Separate row entries allow interruption without losing these saved words.

**ROM static:** `00:3B9C–3C29`, `00:3C52–3D1C`, `00:3D36–3DA5`,
`01:60FD–61F4`. **ROM run:** both releases' probes compare all segment state,
cycles and ordered ports. Environment substitutions, vehicle return modes,
nine portrait selections and transparent/opaque/mixed overlays across several
row widths and heights have explicit alternative-path tests. The visual
routines already documented under walking remain shared; the scene code
reuses them rather than installing another party-animation implementation.

The scene strip uploader also divides long video transfers into short windows.
Its stream stops on the original conditional jump after at least 128 cycles,
keeping the source pointer and outer strip counter on the stack. It resumes
with the same pending byte and video address; the writes keep their original
order and cycle positions. This allows the spaceship route to remain in C++
even when a strip crosses the end of a frame.

**ROM static:** `01:641B–6441`. **ROM run:** both-release probes cover the
new stream and inherited-stack entries. The US vehicle checkpoint matches all
Mesen reference frames and passes the per-entry C++ admission requirement.


### The first load after EI

A protected load remains on the processor while the game work after it starts
at the listed continuation. This keeps the one-instruction interrupt delay;
it does not leave the later stores or long copies on the interpreter.

| Protected load | Opcode | Continuation | Purpose |
| --- | --- | --- | --- |
| `00:065A` | LD HL,nn | `00:065D` | Continue background setup |
| `00:07B2` | LD A,n | `00:07B4` | Opening frame-wait selector |
| `00:08FC` | LD HL,nn | `00:08FF` | Spaceship screen copy |
| `00:3CF3` | LD A,(nn) | `00:3CF6` | Interaction music selection |
| `00:3D15` | LD A,n | `00:3D17` | Interaction frame-wait selector |
| `00:3EF2` | LD HL,nn | `00:3EF5` | Name position update |
| `01:4045` | LD HL,nn | `01:4048` | Name-editor initialization |
| `01:40A2` | LD DE,nn | `01:40A5` | Initial name cursor |
| `01:4302` | LD A,n | `01:4304` | Opening sound request |

The name-position load also has a direct branch from the selected-key test.
That complete alternative is rendered in the caller, including the load and
both possible returns. Its EI arrival still executes the protected load on
the processor before the continuation. **ROM static; ROM run:** the decoded
direct targets, replay censuses, both selected-key outcomes and protected-load
state/cycle/hook probes establish these separate paths on both releases.


## Darkfalz and the final answer

The Darkfalz interaction selects its scene, copies sixteen palette bytes from
cartridge bank 19, starts the enemy encounter, and returns to the final
conversation. A surviving first character skips the extra dialogue. The final
yes/no answer selects one of two dialogue pointers; both answers then unwind
the interaction stack and jump into the ending. The asset copy is resumable,
so an interrupt or frame boundary can stop between its original iterations.

**ROM static:** US Rev-A and Brazilian `01:5619–569B`. The original CALLs at
`00:074D` and `01:454E` have explicit entry facades; their title initialization
and ending bodies are implemented by the following scene segments. These
facades preserve the original return address and do not absorb the callees.

**ROM run:** segment probes compare full state, cycles and port operations on
both releases, including both character-status and final-answer branches.
This establishes these segments, rather than an additional complete Mesen
playthrough of the Darkfalz encounter.


## Dialogue choices and recruiting Noah

Interaction scripts select a dialogue from the cartridge, then call or tail
jump into the shared text renderer. Their immediate pointers remain read from
the mapped cartridge. Encounter scripts similarly install the selected enemy
group before invoking the shared encounter setup.

The cake seller computes the price subtraction, checks inventory capacity,
and only then stores the new balance. Insufficient money and a full inventory
keep the old balance. Recruiting Noah checks the party size and the letter,
unwinds the interaction stack, sets his initial equipment and increases the
party size before entering the shared conversation scene.

**ROM static:** US Rev-A and Brazilian `01:510D–5156` and `01:515F–51B0`.
Other dialogue selections and encounter selectors have individual entries in
the explicit scene catalog; text, item IDs and equipment values are read from
the cartridge.

**ROM run:** US/BR segment probes compare state, cycles and ordered port
operations. Directed purchase probes cover just below, equal to and above
the price, plus inventory counts on both sides of the capacity limit. These
probes do not assert an additional complete recorded purchase or recruitment.


## Advancing conversations and exchanging items

Conversation progress flags choose a reply and advance only on the original
branch. Item exchanges preserve the inventory helper's result, keep BC across
item removal where the script requires it, and set the gate flag afterward.
Other branches set a dungeon position and direction before returning to play.
After an encounter, the script can open the field menu; the resulting game
mode decides whether to unwind two additional interaction stack words.

**ROM static:** US Rev-A and Brazilian `01:51C2`, `01:520D`, `01:53DF`,
`01:53E5` and `01:54F2`. The explicit catalog also lists every implemented
reply branch and continuation.

**ROM run:** both-release segment probes compare full state, cycles and port
operations, including directed progress states and both paths of the
post-encounter stack unwind. The probes establish these segments and their
alternatives; they do not establish an additional recorded whole quest.


## Solo encounters and destination requests

The solo-encounter script keeps the prior HP and three other party-status
bytes on the stack while their temporary status is zero. It restores those
bytes after the encounter and chooses its reply from the new HP result.
The party-status conversation separately walks a four-character bitmask,
preserving its index across each conditional dialogue call, then searches
for a surviving character before choosing its continuation.

Destination menus preserve BC and DE across shared menu/dialogue calls.
A cancelled menu returns; a declined confirmation reopens the conversation.
An accepted different destination reads the request from the cartridge table
indexed by the current and selected destinations, stores it, and restores
the original caller stack.

Hapsby's entry selects its item request and performs the shared inventory
search. An already-present item takes the existing-item dialogue; an absent
item takes the cleanup continuation leading toward the grant sequence.

**ROM static:** US Rev-A and Brazilian `01:5482`, `01:549F`, `01:5546`,
`01:56A2`, `01:56FF` and `01:573F`. **ROM run:** both-release segment probes
include zero/nonzero HP, all sixteen party-status masks, last-survivor loop
boundaries, and all six different current/selected destination pairs with
accepted and declined answers. A separate Hapsby chain executes the actual
inventory search with the item absent, in its first slot and in its last
slot; it compares complete state, every write callback and every port up to
the selected cleanup or dialogue callee. These checks establish their
specified segments and chain, rather than a complete recorded quest.

## Clearing the screen between scenes

**ROM static.** The opening, name entry and several story scenes prepare
buffers before drawing the next picture. To fill a buffer with one value,
the game writes the first byte, then copies it to the next address. Each
copy supplies the source for the one that follows. If a frame ends partway
through, the addresses and remaining count tell the game where to resume.
The first writes occur at `00:0778`, `00:0785`, `01:405F` and `01:4604`.
The associated buffer routines begin at `00:075A`, `00:08CE`, `00:08FF`,
`00:3D4E`, `01:4048`, `01:42D8` and `01:45F6`.

Two callers can select the same blank screen. At `00:3D36` the caller
compares its argument. At `00:3D47` it reads the current interaction's
picture selector. In the latter case the game either clears the buffer
and returns, or loads the palette, graphics and layout before returning.

**ROM run.** On each cartridge, 99 directed scene tests compare the whole
machine, the time and value of every memory write, and every port access
across frame and interrupt boundaries. They cover both outcomes of the
picture selector through the return to the caller. Short test interrupt
handlers isolate the timing of the copy. The opening, road, ending and
boss recordings also check scene transitions against MesenCE.
