# How it works: text

**ROM static.** The cartridge keeps its messages as compact streams of
values. Some values represent characters, some ask for a shared word, and
others control what happens next. The program reads the selected cartridge
while it runs, so the English and Brazilian text and their choices of control
values remain in the player's own ROM (`00:31FA`, `00:333E`, `00:337D`).

**Since milestone 14.** The Z80 is no longer in the program. Where this chapter says that a
stretch stays on the Z80, is left to the original code, runs on the emulated processor, or falls
back when an interrupt or a frame boundary is near, it describes the milestone the text was
written for. Today every such instruction is rendered C++, an interrupt is accepted at the exact
instruction even inside a piece, and a state a piece cannot render stops the game with a message
([interrupts](interrupts.md), [development](../development.md#registered-routines)).

## From a message to the screen

**ROM static.** A caller can supply a message directly or select one through
an index. The indexed path switches to the message bank, looks up its address
and passes that address to the dialogue reader (`01:575A`, `00:31D4`). If a
window is already open, the reader continues on its next line; otherwise it
sets up a window first. The window drawing and cleanup remain native game
code.

**ROM static.** A value with its top bit set chooses a dictionary entry. The
reader saves its place and the current bank, switches to the dictionary,
prints its characters and restores both before continuing (`00:333E`). This
allows common words to be stored once. Another reader uses the same dictionary
and character output for a different screen layout (`00:337D`).

**ROM static.** Other values can end a message, wait for a button, start a
new line or clear a page. Some insert a character name, an enemy name, an
item name or a number chosen by the game (`00:3211`–`00:32F5`). Names are
read from their current storage when displayed. A number is broken into
decimal digits; leading zeroes are omitted and zero itself still receives a
digit (`00:3296`, `00:32FB`).

**ROM static.** Each visible character is made from an upper and lower tile.
The program looks those tile entries up in the cartridge and sends them to
the video hardware, then waits before moving on (`00:33D6`). The wait affects
the rhythm of text and the time at which the game next responds. When a line
is full, the reader either advances to the next line or asks native window
code to scroll (`00:3416`, `00:342C`).

**ROM run.** On both identified cartridges, the rewritten segments are
checked against the original one segment at a time, including the video
writes and their timing. Existing first-battle recordings exercise the word
dictionary, inserted names and numbers, and the alternate reader. These runs
establish the paths they visit; they do not by themselves establish every
possible message or control sequence.

## Changing the language without restarting

**Maintainer decision (2026-11-13).** Language is a preference for the whole
program. Both cartridges must be available to choose freely between English
and Portuguese. The same saved games, options and automap are used regardless of which
cartridges are available. On first use, PSModern copies an old save set into
shared storage; if the old US and Brazilian sets differ, the player chooses
which to continue, and both originals remain intact. Names the player typed
for saved games are kept.

**ROM static; ROM run.** Options can open only while the party is freely
exploring. The game first closes its own menu and restores the background.
At that exact return, before the next game instruction, PSModern changes
which cartridge supplies subsequent reads. It preserves the processor,
memory, video, sound and clocks. Future windows and messages read the newly
selected cartridge; no conversation is translated halfway through.

**ROM static.** The text readers resolve their addresses afresh on entry.
Their active message and dictionary positions belong to calls that finish
before exploration resumes. Native window helpers pair a fresh background
copy with its restoration; old bytes left in those temporary buffers are not
reused as live translated text. The field font and border graphics are the
same in both cartridges. Different opening pictures are replaced when a
scene is loaded, rather than being kept on the exploration screen.

**ROM static; ROM run.** The map prepares the incoming row or column before
an interrupt sends it to video memory (`01:72A6`, `01:733A`, `01:73D0`). A
bounded probe checked every horizontal position, vertical ring position and
direction in both ROMs: every consumed byte had been written by that strip's
producer. Old opening bytes cannot reappear through scrolling. The retained
dungeon tables and the sound driver's reachable resources are identical, so
the map's exploration history and normal-tempo sound continue unchanged.

**ROM run.** Separate runs compare the original and C++ paths within each
language. Switching languages does not promise the same later random draws:
different message lengths and control codes spend different amounts of time,
as the original cartridges do ([original quirks](../original-quirks.md)).


### Interrupt-boundary completion

**ROM static.** The number and character printers' DI/EI boundaries now have explicit
milestone 13 registrations. The processor still executes those final
instructions itself, preserving pending interrupts and the following protected
load. Register words saved by the character printer are restored in C++
before its final EI. Alternate-layout clearing retains its own DI/EI pair.

**ROM static:** `00:3304`, `00:332A–332B`, `00:33C2–33C8`,
`00:33D6`, `00:340F–3413`. **ROM run:** both identified cartridges pass
per-entry state/cycle probes. The complete drawing chains described in
[menus](menus.md#completing-the-drawing-boundaries) also retain every observed
memory callback and ordered video/PSG port cycle. No dialogue, font or
language-selection behavior changes.


## The optional enhanced dialogue

**Maintainer decision (2026-11-28).** Choosing Enhanced lets the same
cartridge story use proportional letters. When a message starts, the host
reads its address after the cartridge has selected the bank. Both a direct
message and one chosen from an index reach the same point at `00:31D4`.
The native window flag says whether the words continue a conversation. A
portrait page that clears and starts drawing again still belongs to the
same message. Names and numbers are read when that message starts.

The cartridge continues to print, scroll and restore its original windows.
A second picture follows the video chip's drawing time and keeps the scene
underneath those windows. New scene tiles can appear underneath an open
window. The host draws the new letters over this picture, using the options,
language and game state captured with the frame. The console keeps its
own picture for reads by the game, including the copies made before a window
opens.

Each cartridge message reveals its letters as the printer finishes writing
them. The same printer handles names, and the number printer reports each
inserted digit. The proportional font can fit different words on a line.
The host lays out the complete text at the outset, so a word stays in place
as its letters appear. Portrait captions also follow the cartridge's page
clear and restart. Their terminal waits and ordinary dialogue pauses still
take the original number of button presses.

In battle there is room for two lines above the party. If proportional text
fills that space before a cartridge pause, the host holds the printer at its
next interrupt-enabled boundary. The arrow becomes actionable once the
full page has reached the screen, including at 4× speed. One fresh press
turns the page. Releasing the button re-arms the next choice. The game
continues running interrupts during this wait, so music, Pause and Reset
continue to work. The text-source interface also supports replacement scripts
with their own line breaks and endings.

**ROM run:** US and Brazilian shop and NPC routes reach the native button
waits, purchases and final restoration. The opening recordings follow all
five intermediate portrait waits on both cartridges at 1× and 4×. The
two-line overflow tests exercise a held confirmation, console Pause and
Reset in the middle of an enhanced page.
