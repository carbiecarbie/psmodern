# How it works: the Master System

Phantasy Star was written for the Sega Master System, a home console from
the 1980s. PSModern runs the original game program on a rebuilt copy of that
console, made of software. This chapter explains the console's parts in
plain words, how they work together during one frame, and why exact timing
matters for this game.

Each statement starts with a bold evidence label, as defined in
[the documentation rules](../AGENTS.md#evidence-labels). "Reference emulator"
means an observation made while running an identified game ROM in an emulator.
A rule read only in MesenCE's source code is an inference from community source,
not that kind of observation. The [development notes](../development.md)
separate these sources where exact timing is not measured on hardware.

## Words used here

| Word | Meaning |
| --- | --- |
| Bit, byte | A bit is one on/off value. A byte is 8 bits, numbered 0 to 7, and holds a number from 0 to 255. Memory is a row of bytes, each with a number called its address. |
| `$` numbers | Addresses and bank numbers in hexadecimal (base 16): `$4000` is 16,384, `$FFFF` is 65,535 |
| KiB | 1,024 bytes |
| ROM, RAM | ROM can only be read: here, the game program on the cartridge. RAM can also be written. |
| Routine | A piece of the program that does one job |
| Frame | One complete picture on the screen; about 60 are drawn per second |
| NTSC | The television standard of North America and Japan, which sets the timing below |
| Emulator | A program that imitates a console on a computer |
| Input recording | A file listing the buttons pressed in each frame, played back so that every run sees the same input |

Original code is cited as `bank:address`: `0C:8028` means address `$8028`
while bank `$0C` (12) of the program is in view (see "The cartridge").

## The parts

**Hardware documentation.** The console has one processor, a Zilog Z80,
which runs the game. A video chip, the VDP (Video Display Processor), draws
the picture. A sound chip, the PSG (Programmable Sound Generator), makes the
sound. The processor reaches these chips and the controllers through
numbered "ports", like mailboxes it can send a byte to or read a byte from.
SMS Power describes the [memory map](https://www.smspower.org/Development/MemoryMap),
[mapper](https://www.smspower.org/Development/Mappers) and
[ports](https://www.smspower.org/Development/IOPortMap).

**Maintainer decision.** PSModern rebuilds one exact model: the first
Master System (SMS1) sold outside Japan, with the 315-5124 video chip, two
standard controllers and a Reset button, without the optional FM unit (an
extra music chip). It rebuilds only what this game uses.

## The Z80 processor

**Hardware documentation.** The processor reads the game's instructions
one at a time and carries them out: add two numbers, copy a byte, jump to
another part of the program. It keeps a few working values in "registers",
tiny storage cells inside the chip. A clock sets its pace; each tick is one
"cycle", and a short instruction takes 4 cycles. The clock runs at
3,579,545 ticks per second: Sega's
[official documentation](https://www.smspower.org/Development/SMSOfficialDocs)
gives 3.58 MHz, [Maxim's sound chip notes](https://www.smspower.org/Development/SN76489)
the exact NTSC value, and Charles MacDonald's
[measurements on an NTSC console](https://www.smspower.org/forums/8161-SMSDisplayTiming)
one third of the console's main clock of about 10.74 MHz.

### The counter R

**Hardware documentation.** One register, R, counts by itself. The
[Zilog Z80 manual](https://map.grauw.nl/resources/cpu/z80.pdf) says its
lower seven bits go up by one after each instruction fetch, the moment the
processor reads an instruction's code; the eighth bit stays as the program
set it. So the count runs from 0 to 127 and starts again. R was meant to
keep a kind of memory chip refreshed, but a program can read it. While the
processor waits in its idle state (HALT), it keeps fetching an empty
instruction, so R keeps counting. Sean Young's
[undocumented Z80 notes](https://www.smspower.org/uploads/Development/z80-documented.pdf),
section 6.1, add that instructions starting with an extra code byte (a
prefix) count two fetches, and that accepting an interrupt counts one.

**Maintainer decision.** PSModern counts R exactly this way; its tests check
prefixes, the idle state and interrupts.

## The VDP: the video chip

### What the picture is made of

**Hardware documentation.** The picture is 256 dots wide and 192 lines
tall, built from small square pieces instead of single dots, as
[MacDonald's VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
and the [VDP register list](https://www.smspower.org/Development/VDPRegisters)
describe:

| Piece | What it is |
| --- | --- |
| Tile | An 8 by 8 dot drawing, each dot one of 16 colors |
| Name table | A grid of 32 by 28 cells saying which tile goes where, flipped or not, with which palette, in front of or behind sprites; 24 rows fit on screen, the rest serve scrolling |
| Sprite | A tile placed anywhere, for moving things; up to 64 are listed, at most 8 on one line |
| Palette | 32 color slots in two rows of 16; background tiles use either row, sprites only the second |

**Hardware documentation.** The VDP has its own 16 KiB memory, VRAM (video
RAM), for the tiles, the name table and the sprite list. The palette sits in
a separate 32-byte memory, CRAM (color RAM). Each slot holds one of 64
colors, two bits each of red, green and blue
([palette page](https://www.smspower.org/Development/Palette)). The
background can scroll sideways and up and down.
In this 192-line mode it scrolls around a 224-line name table: even scroll
values `$E0` to `$FF` wrap around to the first 32 lines.

**Hardware documentation.** The first SMS has an unusual sprite enlargement
rule: all selected sprites become twice as tall, but the last four keep their
normal width. With four or fewer on a line, none becomes wider. The
[sprite documentation](https://www.smspower.org/Development/Sprites) and
[measurements](https://www.smspower.org/forums/19069-ZoomedSpritesOnSMS1)
describe this. Register R6 chooses a bank of sprite tiles independently of two
bits that can restrict the tile number. PSModern models those SMS1 restrictions;
MesenCE does not model the two R6 masks.

**Maintainer decision.** PSModern rebuilds only the 192-line display mode
this game uses, and records each finished picture as 256 by 192 colors.

### How a frame is drawn line by line

**Hardware documentation.** The VDP draws like an old television: one line
at a time from top to bottom, each from left to right. MacDonald's notes
give an NTSC frame 262 lines. Lines 0 to 191 are the picture; the other 70
are border, blanking and the beam's return to the top. Each line is 342 dots
long: 256 picture dots, borders on both sides and the beam's return. The dot
clock runs one and a half times as fast as the processor, so a line lasts
342 / 1.5 = 228 cycles.

| Quantity | Value |
| --- | --- |
| Processor cycles per line | 228 |
| Lines per frame | 262 (192 picture + 70 others) |
| Processor cycles per frame | 59,736 |
| Frames per second | 3,579,545 / 59,736, about 59.92 |

**Hardware documentation.** On each picture line the VDP reads from VRAM,
column by column, which tile goes there and then that tile's dots, and sends
the dots out in order. It prepares at most eight sprites before the line that
will display them, retaining their positions, tile numbers and drawing data.
Changing those bytes after the fetch affects a later line. MacDonald's notes,
the [sprite description](https://www.smspower.org/Development/Sprites) and the
[VRAM timing discussion](https://www.smspower.org/forums/18134-TimingOfVRAMAccessSlotsDuringActivePassiveDisplay)
describe this order. **Inference (from community source).** The exact moment
of each read follows MesenCE's source code; the development notes give the
separate background and sprite schedules. These precise dots are not hardware
measurements.

**Hardware documentation.** Each pair of overlapping opaque sprite dots can
raise the collision flag. Reading status clears it, but a later overlap can
raise it again, even on the same line
([collision documentation](https://www.smspower.org/Development/CollisionDetection)).

**Maintainer decision.** PSModern adds no dot when a palette color is written
while the picture is being drawn, matching the reference build. The palette
still receives the color. **Community source.** [Forum observations](https://www.smspower.org/forums/14997-HorizontalScrollPaletteChangeMidFrame)
report such dots on some consoles, including an SMS II. **Unknown:** what color
they have and whether SMS1 hardware behaves this way.

### Interrupts: "the picture is done"

**Hardware documentation.** An interrupt is a signal that makes the
processor pause its work, run a special routine, and then continue. The VDP
can raise a **frame interrupt**, when the 192 picture lines are finished,
and a **line interrupt**, after a chosen number of lines. The game switches
each on or off. Reading the VDP's status byte tells the game, among other
things, that a frame has ended, and clears the waiting interrupts. The Z80
has three ways ("modes") of handling an interrupt; in mode 1 it always
jumps to address `$0038`. The frame interrupt comes on line 192, the
first line after the picture. (The VDP notes number lines by the value the
chip reports just after each change, so their numbers differ slightly.)
MacDonald's measurements and the
[SMS VDPTest](https://github.com/FluBBaOfWard/SmsVdpTest) program, checked
by its author on a real SMS1, fix each event they test, such as the
interrupts and status flags, to a single cycle relative to the others and
to the processor.

**Inference.** Those sources give the order and spacing of the events, not
where the video chip stands when the console is switched on, so a starting
point has to be chosen.

**Inference (from community source).** PSModern chooses MesenCE's initial
phase: at the first cycle the
video chip is at line 0, dot 1. Counted that way, the frame interrupt comes
210 cycles into line 192: 192 x 228 + 210 = 43,986 cycles into the frame.

**ROM static.** Phantasy Star selects mode 1 at `00:0001`, so its interrupt
routine starts at `00:0038`. Its setup at `00:03AA` switches the frame
interrupt on and the line interrupt off.

### When the processor can reach video memory

**Hardware documentation.** The processor cannot touch VRAM itself: it
hands each byte to the VDP through a port, and the VDP stores it when its
memory is free. While drawing picture lines the VDP is busy reading tiles
almost all the time; between pictures it is mostly free. Sega's
documentation gives the safe spacing between two accesses: 29 cycles during
the picture, 16 outside it. So games usually make large picture changes
right after the frame interrupt.

**Inference (from community source).** PSModern lets a waiting byte through at fixed
"slots": 19 on each picture line and up to 107 on the other lines. This
SMS1 schedule follows MesenCE. The VRAM timing discussion shows a similar
pattern, but its detailed trace is from a Mega Drive, a later Sega console.

**Unknown.** What a real SMS1 does when a program goes faster than the safe
spacing. At frames 11031 and 11623 of a US input recording the real slots
are not known and a second emulator, Emulicious, does not confirm MesenCE,
so matching there shows agreement with the reference, not the hardware.

## The PSG: the sound chip

**Hardware documentation.** The sound chip is a version of the Texas
Instruments SN76489 built into the video chip. It has four channels that
play at once: **three tone channels**, each a square wave (a sound that
switches on and off at a set pitch), and **one noise channel**, a hiss or
buzz from a 16-bit scrambling pattern. Each channel has 15 loudness levels,
2 decibels apart, plus off. The game sets pitch and loudness by sending
bytes to a port. The chip counts time in steps of 16 processor cycles.
These rules are in [Maxim's SN76489 notes](https://www.smspower.org/Development/SN76489).

**ROM static.** The game's sound code in bank `$0C` writes to the sound chip
at `0C:8028`.

**Maintainer decision.** PSModern records every byte sent to the sound chip
with the cycle it arrived, and makes one sound sample (one value of the
sound wave) every 16 cycles. Turning samples into what the speakers play
happens outside the rebuilt console. Details the notes leave open are
listed in [the development notes](../development.md#psg).

## The cartridge

### The mapper: swapping pieces of the program into view

**Hardware documentation.** The processor can see only 64 KiB of memory at
a time, but the game program is 512 KiB. A chip on the cartridge, the
"mapper", cuts the program into 32 pieces ("banks") of 16 KiB and shows
three at a time, in three "windows" of the processor's view. The game picks
the bank for a window by writing its number to `$FFFD`, `$FFFE` or `$FFFF`.
The first 1 KiB always shows bank 0, so the interrupt entry points never
move.

```text
Processor's view (64 KiB)
$0000-$3FFF  window 0   a bank of the program (first 1 KiB: always bank 0)
$4000-$7FFF  window 1   a bank of the program
$8000-$BFFF  window 2   a bank of the program, or the save chip
$C000-$DFFF  work RAM   8 KiB inside the console
$E000-$FFFF  the same 8 KiB again
```

**Hardware documentation.** The console has 8 KiB of its own "work RAM".
Each of its bytes has two addresses, one in `$C000-$DFFF` and one 8 KiB
higher. The mapper's addresses sit at the very top, so a byte written there
also lands in RAM, and reading them back gives that RAM copy.

**ROM static.** The game's interrupt routine reads `$FFFC` and `$FFFF` back
at `00:0102`-`00:0109`.

### The save chip

**Hardware documentation.** The cartridge also holds a small memory chip
for saved games. A [cartridge board inventory](https://gendev.spritesmind.net/mirrors/www.nepereny.com/misc2/games/sms_phantasy_star.htm)
shows a Phantasy Star board with an 8 KiB MB8464A chip and, on the back, a
lithium coin battery that keeps its contents while the console is off. When
the game sets bit 3 of the mapper's control address `$FFFC`, the save chip
replaces the program in window 2, and the game can read and write it.

**ROM static.** The game switches the save chip on at `00:07DC`, reads it at
`00:07EE` and writes save slot 1 from `00:1D1D`.

**Inference.** From the pictured chip, and the 8,188 bytes the game used in
the reference emulator, PSModern treats the save chip as exactly 8 KiB in
both versions.

**Unknown.** The part number of the Brazilian cartridge's save chip.

**Maintainer decision.** PSModern keeps the 8 KiB in one file per game
version and writes it out at the end of any frame in which it changed; the
rebuilt console itself never touches files ([saves](../development.md#saves)).

## The controller, Pause and Reset

**Hardware documentation.** Each controller has a direction pad and two
buttons. The processor reads them through ports `$DC` and `$DD`; most bits
of those bytes are one button each, as Sega's documentation lists. The
console's Reset button resets nothing by itself: it is bit 4 of port `$DD`,
which the program can read. The Pause button is wired to the processor's
"non-maskable interrupt", which the program cannot switch off. It sends the
processor to `$0066`; holding the button gives only one interrupt.
MacDonald's measurements place the Pause interrupt as the console moves
into line 261, near the end of the frame; FluBBa's measurements in the same
discussion match a check two cycles after the chip moves on to the next
line.

**Hardware documentation; inference.** Counted from the selected starting phase, PSModern
checks the Pause button 212 cycles into line 260: 260 x 228 + 212 = 59,492
cycles into the frame.

**ROM static.** The game checks the Reset bit in its interrupt routine at
`00:010F` and, on a press, jumps back to its start-up code at `00:0084`. It
reads the controllers at `00:0339`.

**Maintainer decision.** PSModern takes the controllers, Pause and Reset
once per frame, at the first cycle of line 0; a change during a frame counts
from the next frame. Input recordings use the same point, so a recording
means the same thing in every run.

## One frame from start to end

**Maintainer decision.** In PSModern a frame starts at the first cycle of
line 0. The processor never stops between frames: the program that runs the
console frame by frame lets an instruction that crosses the boundary finish,
and notes how many cycles it went over.

Putting together the pieces above, each labeled where it is explained:

```text
cycle 0            line 0: buttons taken, last picture handed over
cycles 0-43,775    lines 0-191: the VDP draws the picture and the processor
                   runs the game; VRAM is reachable only at rare slots
cycle 43,986       line 192: frame interrupt; after the current instruction
                   the game's routine at 00:0038 runs (status read, Reset)
lines 192-261      no picture drawn; VRAM is easy to reach
cycle 59,492       Pause button checked
cycle 59,736       = cycle 0 of the next frame
```

**Unknown.** What else the game does in each frame, and in what order, is
not established here.

## Why exact timing matters for this game

**ROM static.** At `00:05C8` the game's random-number code reads R and mixes
it with part of another number it holds; the mix is the value it hands back.
That other number is kept in work RAM at `$C20C` and does not contain R.

**Reference emulator.** In the recorded sessions this was the only reading
of R that ran; two other places that looked like one are graphics data.

**Inference (high confidence).** So each random number depends on R at that
moment, which depends on how many instructions, interrupts and idle steps
the processor has gone through since it started. If one instruction took
one cycle more, the frame interrupt could cut the game's work at a
different place; if one extra instruction were fetched, R would be one
higher. This can change a draw and later draws too. It need not: if no
relevant event falls between the two timings, the same number of fetches can
reach the read of R, preserving that draw.

**ROM static; ROM run.** The callers now partly establish that connection:
the map and dungeon encounter checks, encounter-pool selection and enemy
group-size draws use this routine. The
[random-number chapter](random-numbers.md) explains those stages and the
other established uses, with the limits of the six US recordings.

**Unknown.** Indirect callers and paths outside those recordings remain
outside measured coverage.

**Maintainer decision.** For this reason PSModern is compared with the
reference emulator frame by frame from power-on, on input recordings of
real play in both versions. At every frame the comparison checks the
picture, the work RAM, the VRAM, the CRAM and the save chip, with no
allowance for differences. Later, when PSModern replaces the game's
routines one by one with new code, each replacement is checked to spend the
same cycles and leave R exactly as the original would
([design, section 5.2](../design.md#52-verification-the-heart-of-the-project)).

**Hardware documentation; inference.** The reference is not perfect either: the project
corrected three groups of faults in its copy of MesenCE, each shown against
the Zilog manual and other published measurements. MesenCE also misses
sprite collisions in the border, which VDPTest shows on a real SMS1; that
shows only in one bit of the status byte, which is not among the compared
data ([details](../development.md#reference-comparison)).

## How the console starts in PSModern

**Hardware documentation.** A Master System first runs a small built-in
program, the BIOS. Sega's documentation describes it: it looks for a game in
the card slot, the cartridge slot and the expansion slot, then jumps to the
first instruction of the one it finds, and the game cannot see it again.

**Maintainer decision.** PSModern does not run a BIOS. It starts the
cartridge directly, from one fixed, documented state, as emulators do
without a BIOS. A BIOS would also mean asking the player for a copy of
Sega's copyrighted BIOS.

**Inference.** The start state fixes the sequence of random numbers. With a
BIOS it would be another fixed sequence, not a more correct one, and on a
real console the player's own timing changes it anyway.

**Inference (from community source).** The fixed state copies the initializer
values MesenCE 2.2.1
uses when it starts without a BIOS, standing in for what a BIOS leaves
behind; they are not the chips' own power-on values.

| Part | Start state |
| --- | --- |
| Processor | starts at address `$0000`; R = `$1D`; ignores interrupts; mode 1 |
| Video chip | line 0, dot 1; VRAM all zero; a fixed palette and settings |
| Work RAM | all zero except one marker byte at `$C000` |
| Mapper | banks 0, 1 and 2 in windows 0, 1 and 2 |

The full list is in
[the development notes](../development.md#fixed-machine-start-and-frame-input).

**Maintainer decision.** The save chip starts with the player's save file,
or 8 KiB of zeros if there is none.

**Maintainer decision.** One detail differs from MesenCE. The fixed state
has a frame interrupt waiting and switched on, and PSModern raises the
interrupt signal from the first cycle, as MacDonald's VDP notes, section
12, describe; MesenCE starts with the signal down. The processor ignores it
either way.

**ROM static.** Both versions keep interrupts off from their first
instruction, and write the video chip's settings at `00:03AA` before they
first let interrupts in at `00:00AE`. MesenCE recalculates its signal at
that write (**Inference from community source**), so the two agree in time.

### The US and Brazilian cartridges

**ROM static.** To the console the two cartridges look alike. Both are
512 KiB programs with a header signature at `$7FF0`. The header identifies an
export Master System game; both begin the same way, and the code at every address
this chapter cites is identical in both.

**Reference emulator.** In the recorded sessions both versions used the
same ports and the same display settings.

**Maintainer decision.** PSModern runs both on the same console model, with
the same clock and start state. It recognizes each file by its SHA-1
fingerprint, a number computed from the whole file, and refuses any other.
Each version has its own save file.

**Inference.** The difference lies inside the program: the Brazilian version
is a translation, and text of another length takes another number of cycles
to show. That is why each version has its own input recordings.

**Unknown.** Whether Brazilian consoles run at exactly the same clock as US
ones is not settled.

**Maintainer decision.** PSModern uses the same clock for both and leaves a
possible small difference out of scope; it would affect only the playing
speed and pitch on the computer.
