# How it works: the sound driver

**ROM static.** The game's sound driver lives in bank `0C`. It is the part
of the program that turns sound requests and stored music instructions
into writes to the sound chip. Its main entries initialize the driver at
`0C:8000`, silence the chip at `0C:801F`, and advance sound at `0C:8043`.
For the chip itself, see [the console chapter](master-system.md#the-psg-the-sound-chip).

**ROM static.** The code described here is identical at the same addresses
in the US Rev-A and Brazilian cartridges, as established by the comparison
in [design, section 5.5](../design.md#55-languages). The runtime observations
below come from the six existing US recordings.

## Reaching the driver each frame

**ROM static.** The frame interrupt chooses work for the current frame.
Its regular sound-update paths call `00:02E5`. That small piece of code
makes bank `0C` visible and continues at `0C:8043`. The interrupt saves the
previous bank selection and restores it afterward (`00:0102`). The sound
bank-selection routine itself does not restore it.

**ROM static.** Advancing sound means updating timing, handling a pending
request, and visiting active sound records (`0C:8043`). It does not mean
starting a new sound every frame. Not every interrupt path advances the
driver: the pause path calls the silence routine at `00:02F5` instead.

**ROM run.** In the six recordings, the update entry was reached through
the bank-selection routine. Sound-chip writes were observed from the
driver. These observations establish the recorded routes, not every
possible frame path.

## Asking for a sound

**ROM static.** The main game asks for sound by writing a request byte at
`$C004`. For example, the level-gain path at `00:1754` writes there.
The driver reads the request at `0C:82EF` and selects the corresponding
action. A helper at `0C:8420` puts an explicit idle marker back into the
request byte. The driver distinguishes that marker from a reset request.

**ROM static.** Requests can start stored sound sequences or change the
driver's state. The code at `0C:83F4` sets up the records for a selected
sequence. Later updates advance those records. Requests and sound records
are therefore different things: the request asks for an action, while the
records keep an ongoing sound moving.

**ROM run.** The recordings contain game writes to the request byte as
well as frame calls into the driver. Counting only calls to the driver
would miss those sound requests.

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
silence routine at `00:02F5` did not run in any of the six recordings.

## What reaches the chip

**ROM static.** The driver reads the player's ROM for sequence instructions
and keeps their progress in work RAM. Its output code sends pitch settings
to the PSG at `0C:8812` and loudness settings at `0C:8867`. A command path
at `0C:85AD` changes the chip's noise setting. The stored sequence data
itself remains in the player's ROM.

**ROM static; hardware documentation.** Those writes tell the chip how to
produce its tones and noise. They are not audio samples. The distinction
between tone, noise and loudness is explained in
[the console chapter](master-system.md#the-psg-the-sound-chip).

**ROM run.** The pitch-output, loudness-output and noise-command sites
above were reached in the existing recordings. Reaching a site does not
establish every setting that site could send.

## What the recordings leave open

**Unknown.** The six routes do not cover the whole driver. In particular,
they did not reach the main pause-to-silence routine at `00:02F5`, and they
do not include the ending. Other parts of the driver were present in the
code inspected but did not execute on these routes.
Their behavior has no runtime confirmation from these recordings.

**Unknown.** This overview does not establish the meaning of every sound
request or every sequence command. A complete account of sound priorities,
shared-channel behavior and all transitions needs further study. Code
that was not reached is not assumed to be unused.

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
`$C000–$C1FF`, and PSG writes. Some indirect data reads also reach the fixed
ROM bank. The separate path therefore keeps the player's complete ROM
mapping and uses its original CPU and chip implementations. It retains
oscillator phase and the unsubmitted audio before capture. Native calls
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
requests run in their observed order using the ROM request handler. They
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
