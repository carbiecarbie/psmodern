# How it works: text

**ROM static.** The cartridge keeps its messages as compact streams of
values. Some values represent characters, some ask for a shared word, and
others control what happens next. The program reads the selected cartridge
while it runs, so the English and Brazilian text and their choices of control
values remain in the player's own ROM (`00:31FA`, `00:333E`, `00:337D`).

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
