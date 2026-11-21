# How it works: interrupts, reset and frame waits

Sixty times a second the video chip tells the processor that it has finished
drawing a picture. The processor stops what it was doing, runs a short routine
that keeps the game state, the picture and the sound in step, and then carries
on exactly where it stopped. This "frame interrupt" is the game's heartbeat.
The Pause button and the console's reset are handled nearby, in the part of the
cartridge that is always visible to the processor (the fixed bank).

## The frame interrupt

**ROM static.** When the frame interrupt arrives, the game saves the
processor's working values and which cartridge banks are currently selected,
tells the video chip the signal was received, checks the reset button and the
pause state, and then runs the routine chosen for the current part of the game
(walking, battle, menus and so on). That routine clears the "a new frame is
due" request before the saved banks and values are put back.

**ROM static; ROM run.** The game's main loop waits for that request by
checking a flag over and over at `00:0055`; the next routine is chosen at
`00:00AF`, and the processor's stack is set up at `00:0085`. Since milestone 14
every one of these instructions is C++, as is everything else the game runs:
there is no Z80 in the program. The frame wait is cut into one-instruction
pieces so that the usual interrupt arrives between two of them. An interrupt
that arrives in the middle of a longer piece is accepted at the exact
instruction the original would have been executing: PSModern puts the working
values back, pushes the return address, runs the interrupt routine, and then
carries on with the piece at that same instruction. (About 14,800 of the
221,595 frame interrupts in the 27 recordings arrived inside a piece.) The
application still looks at the game between frames (to read the controller,
to fast-forward, to compare against the original), at the points where the
frame ends.

## Pause

**ROM static.** The Pause button uses a separate, higher-priority signal
received at `00:0066`. Its first instruction saves one working value and the
next checks which part of the game is running and, where pausing is allowed,
flips the pause flag; both are C++ now, as is the return to the game, which
keeps the original timing. The Pause signal can arrive inside the frame
interrupt's routine, and PSModern accepts it there exactly as the original
did (11 of the 52 Pause signals in the recordings arrived inside a piece).
While the game is paused it checks the controller once per pass and can still
be interrupted by the next frame.

**ROM run; Reference emulator.** Three US recordings check Pause against the
patched MesenCE on all seven frame components (picture, sound, memory and the
other captured parts): one pauses and resumes during play in a dungeon; one
pauses while walking, where the pause flag changes at frames 1691 and 1745 and
the sound is muted; and one presses and releases the button 48 times at
different moments of the frame. An earlier recording that presses Pause in a
part of the game where it changes nothing also still matches.

## Reset

**ROM static; ROM run.** Reset prepares the stack, selects the cartridge banks
and clears the console's work memory. Each bank selection is C++ with its
original timing. The memory clear runs in bounded slices, so it can stop at a
frame boundary and continue afterwards exactly like the original. When the
game returns from an interrupt, the bank in the third slot is restored before
the next saved value is read, so a stack kept in that slot sees the right
bank. The instruction that re-enables interrupts keeps the one-instruction
delay before the next interrupt. If Reset is noticed inside the frame
interrupt's routine, the game jumps back to its start-up code, which sets the
stack again: every piece that the interrupt had interrupted is dropped and
never resumed, as on the console.

## Waiting for the video chip

**ROM static; ROM run.** Some routines wait by reading the video chip's status
again and again. PSModern makes each of those reads one at a time, with its
original side effects and cost, and each jump of the loop is C++ too. A small
display helper ends by calling the routine that writes the video chip's
two-byte address; the call and the routine are C++. The sprite table is
cleared with the first, faster fill only when no interrupt can arrive in the
meantime.

## How this is checked

**ROM run.** On both cartridges every one of these paths is compared, instruction
by instruction, with the original running beside it in the test programs
(registers, cycles, memory, and the order and time of everything sent to the
video and sound chips), and the 27 recordings match the reference emulator on
every frame. The tests include a pending Pause signal, bank changes while the
stack lives in a banked slot, a video interrupt that is already waiting, work
memory switched off, a memory clear crossing a frame boundary, and an
interrupt of each kind forced at every instruction of sample pieces (at every
cycle in the optional full sweeps). **Unknown:** these tests cover the listed
paths and states; they do not prove that no other path through this code can
occur.
