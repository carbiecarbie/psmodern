# Interrupts, reset and frame waits

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
`00:00AF`, and the processor's stack is set up at `00:0085`. PSModern lets the
original instructions run at exactly these points, because that is where the
application looks at the game between frames (to read the controller, to
fast-forward, to compare against the original). Everything in between runs in
C++, and whenever the C++ version cannot keep the exact timing at that instant
the original instructions take over.

## Pause

**ROM static.** The Pause button uses a separate, higher-priority signal
received at `00:0066`. The first instruction there saves one working value and
runs on the original processor. The C++ version then starts at `00:0067`:
it checks which part of the game is running and, where pausing is allowed,
flips the pause flag. Both paths restore the saved value, and the return to the
game is left to the processor so that it keeps its original timing. While the
game is paused it checks the controller once per pass and can still be
interrupted by the next frame.

**ROM run; Reference emulator.** Three US recordings check Pause against the
patched MesenCE on all seven frame components (picture, sound, memory and the
other captured parts): one pauses and resumes during play in a dungeon; one
pauses while walking, where the pause flag changes at frames 1691 and 1745 and
the sound is muted; and one presses and releases the button 48 times at
different moments of the frame. An earlier recording that presses Pause in a
part of the game where it changes nothing also still matches.

## Reset

**ROM static; ROM run.** Reset prepares the stack, selects the cartridge banks
and clears the console's work memory. Each bank selection is still executed by
the original instruction, with its original timing. The memory clear runs in
C++ in bounded slices, so it can stop at a frame boundary and continue
afterwards exactly like the original. When the game returns from an
interrupt, the bank in the third slot is restored before the next saved value
is read, so a stack kept in that slot sees the right bank. The last cartridge
setting and the instruction that re-enables interrupts run on the original
processor, so the one-instruction delay before the next interrupt is kept.

## Waiting for the video chip

**ROM static; ROM run.** Some routines wait by reading the video chip's status
again and again. PSModern makes each of those reads one at a time, with its
original side effects and cost, and lets the original processor run the
jumps of the loop. A small display helper ends by calling the routine that
writes the video chip's two-byte address; the original processor makes that
call and C++ continues inside it. The sprite table is cleared with the first,
faster fill only when no interrupt can arrive in the meantime; otherwise the
original instructions do it.

## How this is checked

**ROM run.** On both cartridges every one of these paths is compared with the
original instructions for memory, processor state, timing and the order of
everything sent to the video and sound chips. The tests include a pending
Pause signal, bank changes while the stack lives in a banked slot, a video
interrupt that is already waiting, work memory switched off, and a memory
clear crossing a frame boundary. **Unknown:** these tests cover the listed
paths and states; they do not prove that no other path through this code can
occur.
