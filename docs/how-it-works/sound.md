# How it works: the sound driver

**ROM static.** The game's sound driver lives in bank `0C`. It is the part
of the program that turns sound requests and stored music instructions
into writes to the sound chip. Its main entries initialize the driver at
`0C:8000`, silence the chip at `0C:801F`, and advance sound at `0C:8043`.
For the chip itself, see [the console chapter](master-system.md#the-psg-the-sound-chip).

**Since milestone 14.** The Z80 is no longer in the program. Where this chapter says that a
stretch stays on the Z80, is left to the original code, runs on the emulated processor, or falls
back when an interrupt or a frame boundary is near, it describes the milestone the text was
written for. Today every such instruction is rendered C++, an interrupt is accepted at the exact
instruction even inside a piece, and a state a piece cannot render stops the game with a message
([interrupts](interrupts.md), [development](../development.md#registered-routines)).

**ROM static.** The code described here is identical at the same addresses
in the US Rev-A and Brazilian cartridges, as established by the comparison
in [design, section 5.5](../design.md#55-languages). The initial runtime
observations below came from the first six US recordings; milestone 13 added
the `ending`, `sound-flute`, and Pause recordings.

## Reaching the driver each frame

**ROM static.** The frame interrupt chooses work for the current frame.
Its regular sound-update paths call `00:02E5`. That small piece of code
makes bank `0C` visible and continues at `0C:8043`. The interrupt saves the
previous bank selection and restores it afterward (`00:0102`). The sound
bank-selection routine itself does not restore it.

**ROM static.** Advancing sound means updating timing, handling a pending
request, and visiting active sound records (`0C:8043`). It does not mean
starting a new sound every frame. Not every interrupt path advances the
driver: the pause path calls the silence routine at `00:02F5` instead. The
voice-selection loop now runs in C++, while its individual calls remain at
their original boundaries and the host still observes the update entry.

**ROM run.** In the initial six US recordings (and all later checkpoints), the
update entry was reached through
the bank-selection routine. Sound-chip writes were observed from the
driver. These observations establish the recorded routes, not every
possible frame path.

**ROM static; ROM run.** The shared timer at `0C:80C9` counts down once per
tick. When it expires, it reloads from the control byte and advances timing
fields in one of two groups of voice records. Both ROM versions are probed
across every timer value and representative control values.

**ROM static; ROM run.** A separate fade controller (`0C:80F0`) changes the
participating voices at intervals. Its two directions stop at their limits;
when the fade finishes, the driver can reset or hand control to the next
sound. Preparing a fade-in (`0C:8196`) saves the six voice levels and starts
their return after silencing the chip. Both ROM versions are probed across every phase and representative
channel levels.

## Asking for a sound

**ROM static.** The main game asks for sound by writing a request byte at
`$C004`. For example, the level-gain path at `00:1754` writes there.
The driver reads the request at `0C:82EF` and selects the corresponding
action. Effect requests (`0C:8389`) select a stored sound and configure
the channels it shares with other voices; some send commands directly to
the PSG. A helper at `0C:8420` puts an explicit idle marker back into the
request byte. The driver distinguishes that marker from a reset request.

**ROM static.** Requests can start stored sound sequences or change the
driver's state. The code at `0C:83F4` sets up the records for a selected
sequence. The setup routine copies each voice definition into work RAM, initializes
its progress and sets its starting level before moving to the next voice.
The replacement keeps the original return through the PSG writer. Later
updates advance those records. Two lookup helpers (`0C:8426` and `0C:842F`) select pointers from
tables in the player's cartridge; after handling a request, `0C:8420` marks
it idle. Requests and sound records
are therefore different things: the request asks for an action, while the
records keep an ongoing sound moving.

**ROM run.** The recordings contain game writes to the request byte as
well as frame calls into the driver. Counting only calls to the driver
would miss those sound requests.

**ROM static; ROM run.** The request dispatcher classifies the request by
its value, then selects a track, a voice effect or a command table. The host
continues to observe the original request read at `0C:82EF`; C++ handles
the following classification pieces, saved-selector handling and the voice
setup writes. The same
driver also selects individual sequence commands through a table at
`0C:8519`, retaining a return point for the selected handler. When a handler returns,
the reader advances to the next score byte.

## Initialization and silence

**ROM static.** The main program reaches initialization through `00:02ED`.
At `0C:8000`, the driver clears its shared working state and resets control
fields in its sound records. Initialization then continues into the
silence entry at `0C:801F`, which sends the chip a stored sequence of
silencing writes. These are chip commands, not a recording of quiet audio.

**ROM static.** Silence can also be reached directly through `00:02F5`,
without running the whole initialization. That entry writes to the chip;
it does not itself erase all the ongoing sound records. The driver also
uses initialization and silence internally while processing requests
(`0C:82EF`, `0C:832F`).

**ROM run.** Initialization and silence both ran on the recorded routes,
including entries from inside the driver. The main program's direct
silence routine at `00:02F5` was not reached by the initial six recordings,
and is now covered by the milestone 13 Pause recordings (`pause-gameplay`,
`pause-walk` and `pause-phase-sweep`).

**ROM static; ROM run.** The initialization and silence routines are partly
rewritten in C++: they clear the shared controls and prepare the silence
commands while preserving the caller's registers. The eight silencing commands also run in C++ when they fit between interrupt
and frame boundaries; otherwise the original CPU continues them. Their port
order, values and cycle offsets are checked on both cartridge versions.

**ROM static; ROM run.** When the shared noise voice is active, `0C:80A4`
marks the affected sound records so their channels can be restored. The
C++ replacement checks the same voice flags and control byte on both ROMs.

**ROM static; ROM run.** Sound requests can start a fade (`0C:81CD`,
`0C:81D5`, `0C:8205`) or stop a shared voice (`0C:81F0`). These paths
change the sound records and send chip commands where required. A related
request (`0C:8215`) stops both groups of shared voices and sends the stored
silence sequence. When a shared effect ends (`0C:8627`), the driver quiets
its channel and restores the pitch of voices it had interrupted. Tests check
the original order and timing of those writes on both cartridges.

## What reaches the chip

**ROM static.** The driver reads the player's ROM for sequence instructions
and keeps their progress in work RAM. Its output code sends pitch settings
to the PSG at `0C:8812` and loudness settings at `0C:8867`. A command path
at `0C:85AD` changes the chip's noise setting. The stored sequence data
itself remains in the player's ROM. A pitch lookup (`0C:8509`) selects the
period from that ROM; both pitch and loudness writers now run in C++ when
their complete ordered output fits between interrupt and frame boundaries.

**ROM static; hardware documentation.** Those writes tell the chip how to
produce its tones and noise. They are not audio samples. The distinction
between tone, noise and loudness is explained in
[the console chapter](master-system.md#the-psg-the-sound-chip).

**ROM run.** The pitch-output, loudness-output and noise-command sites
above were reached in the existing recordings. Reaching a site does not
establish every setting that site could send.

**ROM static; ROM run.** Commands in a sequence can change a voice's timing,
pitch shift and level, start a fade, or change noise and voice settings
(`0C:8554`, `0C:855E`, `0C:8566`, `0C:8595`, `0C:85A0`, `0C:85A5`,
`0C:85AD`, `0C:85C1`, `0C:85CA`). The replacement reads the command
operands from the current machine and keeps the original PSG write timing.
Other commands toggle voice flags and levels or repeat a passage of the
score (`0C:857E`, `0C:85CF`, `0C:85E7`, `0C:85ED`, `0C:8618`).
The note reader (`0C:846D`) distinguishes commands from notes, obtains
a period, reads optional pitch drift, computes or reuses note duration, and
initializes the voice's next position. Its score fetch, duration multiplication
and voice reset are split at original instruction boundaries: frame and
interrupt arbitration can resume them without forcing a call. The duration
counter wraps as an eight-bit value, including the zero-count case; both ROMs
are compared over all 256 counts and across frame boundaries.
The driver can also enter a score subroutine, return to its saved position,
and count repetitions (`0C:8687`, `0C:86A2`, `0C:86B5`). Its per-voice
records hold this progress.

## What the recordings leave open

**Unknown.** Even with the milestone 13 additions (which added coverage for
`ending`, `sound-flute`, and the Pause silence routine at `00:02F5`), the
recorded routes do not exercise every sound request or sequence command in the
driver. Other parts of the driver were present in the code inspected and are
checked by state probes rather than full gameplay recordings.

**Unknown.** This overview does not establish the meaning of every sound
request or every sequence command. A complete account of sound priorities,
shared-channel behavior and all transitions needs further study. Code
that was not reached is not assumed to be unused.

**ROM static; ROM run.** The noise voice advances its note clock and
checks whether it can play (`0C:86D0`). A pattern selector (`0C:872D`)
interprets the note's bits to choose its noise envelope and level. When a note is due, the driver reads
the score position and distinguishes note bytes from commands (`0C:86F9`).
The tonal voices follow a related clock and envelope path (`0C:8772`),
including pitch drift, a shared update marker and channel selection. The replacement leaves the
original calls and conditional transfers at their boundaries, so the
following sound and envelope handlers keep their context.

**ROM static; ROM run.** The envelope reader (`0C:8827`) uses each
voice's position to read a loudness step or a control marker. Markers can
restart or end an envelope; an ending can also quiet its PSG channel. A
second reader (`0C:843C`) applies pitch-envelope offsets and its own markers. The
replacement preserves the caller's saved word and the ordered sound write.

## Sound during fast-forward

**Maintainer decision.** Acceleration should speed up the game while music
keeps its normal tempo. The host now runs a separate copy of the player's
ROM sound driver, sound memory and chip state after the first accelerated
sound update. The gameplay machine still executes every original sound
instruction and chip write, unchanged. The listener hears the separate
copy. It continues after returning to 1x, avoiding a jump forward in a song.
A power cycle discards it. A session that never accelerates uses only the
original output.

**ROM static; ROM run.** Sound execution uses bank `0C`, sound records within
`$C001–$C1ED`, and PSG writes. Some indirect data reads also reach the fixed
ROM bank. The separate path therefore keeps the player's complete ROM mapping
and runs the rewritten C++ sound segments on a private machine and PSG chip
(with no Z80 CPU core since milestone 14). It retains
oscillator phase and the unsubmitted audio before capture. Calls
that generate samples beyond the current playback time retain those samples
until due, rather than playing them early.

**Inference from the implementation.** At 2x, two actual game sound updates
give the separate copy one update; at 4x, four give one. Fractional progress
survives speed changes. When pause or another game path omits sound updates,
the separate copy also stops advancing the sequences, while chip time still
passes. Initialization and silence are forwarded separately.

**ROM static; ROM run.** The sound driver can itself write the request byte
while finishing a fade. Forwarding those internal writes from the accelerated
game would finish a normal-speed fade early. A read-only observer distinguishes
game writes, including C++ replacements, from the sound driver's own writes.
Only surviving external requests consumed at `0C:82EF` are forwarded.
Internal fades and acknowledgements instead follow the separate sound clock.

**Maintainer decision; inference from the implementation.** Forwarded
requests run in their observed order through the rewritten sound request
handler. They
do not wait in a backlog of accelerated frames. Sequence updates remain
slower, so several effects can arrive between music updates and the ROM's
shared-channel rules can replace an earlier effect. This deliberately changes
the relationship between requests and music ticks during acceleration;
it does not claim the original event timing for that optional mode.

**Inference from the implementation.** Activation waits for a coherent sound
update boundary; before that boundary accelerated output remains muted.
Unexpected private memory/port access or a routine exceeding its execution
bound disables this path until power cycle, logs a diagnostic, and falls back
to muted acceleration and original audio at 1x. Gameplay continues unchanged.
