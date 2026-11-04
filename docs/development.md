# Development

Use the pinned UCRT64 toolchain in [dependencies](dependencies.md). In a
UCRT64 shell, from the repository root:

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPSMODERN_ROM_US="D:/Projetos/Phantasy Star/Phantasy Star (USA) (Rev-A).sms" \
  -DPSMODERN_ROM_BR="D:/Projetos/Phantasy Star/m1-part0/static/roms/Phantasy Star (Brazil).sms"
cmake --build build-rel
ctest --test-dir build-rel --output-on-failure
./build-rel/psmodern.exe "D:/path/to/game.sms"
```

`build-rel` uses C++20 and `-O2 -g` with assertions enabled. For other
locations set `PSMODERN_ROM_US` and `PSMODERN_ROM_BR` to the player's own
uncompressed files. `PSMODERN_ROM_TESTS` defaults to `ON`: a missing or empty
ROM path makes its test report CTest's skip code 77; an existing wrong file
fails. Set the option to `OFF` to omit ROM tests, as CI does. The cache path
`PSMODERN_REFERENCE_DIR` is reserved for external reference output. Use
`ctest --test-dir build-rel -L fast --output-on-failure` for fast tests.
See [the complete-suite protocol](../tools/AGENTS.md) before a complete run.

Set the `PSMODERN_Z80_TEST_DIR` cache path to an external directory containing
`zexdoc.com`, `zexall.com`, `z80flags.tap`, `z80memptr.tap`, `z80ccf.tap`,
`woodmass.tap` and `spectrum48.rom` to run the Z80 conformance tests. The
Woodmass tests need both their TAP and the Spectrum 48K firmware. An empty
path or a missing file skips the affected tests; CI supplies the three Rak
TAPs and skips the other external suites. See
[dependencies](dependencies.md#z80-conformance-programs) for provenance and
licenses. These tests use the integrated CPU wrapper with a test bus, not
the game ROM or the upstream standalone harness.

Keep ROMs, emulator captures, traces, saves and other working material outside
the repository, under `D:/Projetos/Phantasy Star/`. Never commit ROM-derived
data.

## Fixed machine start and frame input

**Reference emulator.** The fixed state follows MesenCE 2.2.1 with no BIOS,
SMS1 export/NTSC, FM off and zero-filled RAM, except for its initial IRQ line
as described below. At CPU cycle 0 the VDP starts at scanline 0, VDP cycle 1.
The CPU starts with PC=`0000`, AF=`AB42`, BC=`0002`, DE=`A51B`, HL=`0000`,
IX=IY=`0000`, SP=`DFF0`, alternate AF/BC/DE/HL=`0000`/`0200`/`C700`/`C75F`,
I=`00`, R=`1D`, IFF1=IFF2=0 and IM=1. WZ and other core internal fields start
at zero. The mapper control is zero, its banks are 0, 1 and 2, memory control
port `$3E` is `AB`, and I/O control is `0F`. Work RAM is zero except `$C000`
=`AB`; an absent save supplies 8 KiB of zero cartridge RAM. VRAM is zero.

The representative VDP register bytes R0–R10 are `36 60 0F 00 00 7E 03 00
00 00 FF`; the reference initializes the underlying fields directly, so these
bytes reconstruct their effective values rather than read back write-only
registers. CRAM starts as two 16-byte rows:

```text
00 3F 3E 3F 30 30 38 3F 37 3F 00 00 00 00 00 00
00 03 30 0F 07 16 3F 02 00 0F 00 00 00 00 00 00
```

The VDP address is `3D6A`, code 1, read buffer 0, command latch clear,
line-counter reload `FF` and current counter `FC`; VBlank and sprite overflow
are pending, and line IRQ and collision are clear. The H-counter latch is 0.
The source is the [MesenCE VDP initializer](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsVdp.cpp)
and the Part 0 readback described in the [plan](milestone-1-plan.md#2-reference-emulator).

**Hardware documentation; maintainer decision.** INT is derived from the frame
and line pending flags and their R1 bit 5 and R0 bit 4 enables. The
[VDP notes, section 12](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
describe the enabled pending flag as asserting the line. Consequently INT is
active in the fixed initial state. MesenCE instead initializes the pending
flag and enable without recalculating IRQ, then initializes the CPU with no
active IRQ source. Its next R0/R1 write, status read or relevant event
recalculates the line; see `InitSmsPostBiosState` and `UpdateIrqState` in the
linked VDP source.

**ROM static.** In both ROMs IFF1 and IFF2 are clear at startup. The path
from `00:0000` disables interrupts, then `00:0084` disables them again, sets
up the mapper and clears RAM. It calls `00:02ED`, which initializes sound in
bank 0C with RAM and PSG writes, no interrupt enable or VDP access. The block
output at `00:03AA` in the routine at `00:03A4` writes R0 first, causing
MesenCE to recompute its IRQ before the first interrupt enable at `00:00AE`.
An early Pause NMI returns while IFF2 remains clear. Thus the different initial
IRQ level cannot be accepted by the CPU before the models agree.

Frame N starts on the first CPU cycle of line 0. The host supplies pad, Reset
and Pause state for that frame before the boundary; the machine latches it at
the boundary. An input change during a frame takes effect at the following
boundary. **Hardware documentation:** the [SMS display measurements](https://www.smspower.org/forums/8161-SMSDisplayTiming)
place Pause NMI at the transition into line 261; a held button produces only
one NMI edge. The frame runner stops at the first complete instruction boundary
at or after the next frame start and reports the cycle overshoot.

`Machine::run_frame(next_input)` supplies the pad, Reset and Pause state for
the boundary at which that call ends. This makes frame N's input available
while frame N-1 is running. A Pause edge reached after an instruction's
`E-1` sample point is deferred to the next instruction, including a host
scheduled edge. NMI and INT use the same instruction sample point.

## VDP and interrupt timing

The scheduler uses dots, 342 per line. CPU cycle 0 is VDP dot 1 and each CPU
T-state advances three half-dots, so every 228-cycle line has the same phase:
at line cycle `c` the VDP is at dot `floor(1 + 1.5c)`, and an event at dot `d`
becomes visible at line cycle `ceil((2*d - 2)/3)`. For an instruction
occupying CPU cycles `[S,E)`, the machine supplies the core with the INT
level sampled at `E-1`, after any port access in that instruction. An event
first visible at `E` is sampled by the following instruction. The core's
prefix, EI, RETI/RETN and NMI acceptance rules also determine whether an
interrupt is accepted and the boundary hook is called.

**Hardware documentation.** The [Zilog Z80 CPU User Manual, UM008011-0816,
figure 7](https://www.zilog.com/docs/z80/um0080.pdf), shows the I/O machine
cycle as T1, T2, an automatic wait state and T3; read data is taken in T3,
before /RD goes inactive. Port reads and writes take effect at the start of
T3, three T-states after the machine cycle starts. For `IN A,(n)` and
`OUT (n),A` this is also the INT sample point `E-1`. Charles MacDonald's
logic-analyser measurements on an NTSC SMS, in the
[SMS display timing thread](https://www.smspower.org/forums/8161-SMSDisplayTiming)
(February 2006), put the frame /INT 607 master clocks after pixel 0 of line
192 and the line /INT at 608, 4 and 3 clocks before the /HSYNC rise. An `IN`
whose I/O cycle ends at that /CSYNC rise reads the new V counter; one ending
four CPU cycles earlier reads the old value. With INT sampled in the last
T-state, the first instruction that takes the frame IRQ and the first `IN`
that sees the new V counter end at the same cycle, and the line IRQ is taken
one cycle later. In the same thread (March 2008), FluBBa's IRQ and NMI
handlers latch H with their first instruction after a HALT. He reports lowest
values `06` for the frame IRQ, `07` for the line IRQ and `06` for the Pause
NMI, among four values with `08` repeated.

**Hardware test.** The [SMS VDPTest](https://github.com/FluBBaOfWard/SmsVdpTest),
checked by its author on an SMS1 VDP, first synchronizes the CPU to the TH
latch. It then times the V-counter change, the readable frame bit, sprite
overflow and collision, and, after HALT, the frame and line IRQs (`82`/`84`
and `88`/`8A`) against that latch. The model passes all 55 automated SMS1
results. Each event is fixed by its own test to one CPU cycle.

The model therefore has one VDP event at dot 315, line cycle 210. On physical
line 192 it sets the frame flag, which is pending and readable at once. On
every line it advances the V counter and records sprite overflow for the next
line. The frame IRQ's absolute CPU cycle is unchanged from the earlier model:
43986 cycles into a frame, taken by an instruction ending at line cycle 211 or
later. The line counter is clocked at dot 317, line cycle 211, one CPU cycle
after the frame event. It decrements on physical lines 261 and 0–191 and
reloads R10 on 192–260. This is the 0–192 decrement window of the
[VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
when lines are named by the V-counter value after its transition. Underflow
sets the line pending flag and reloads R10; a write to R10 takes effect at the
next reload, not immediately. The Pause NMI sample at line 260, cycle 212,
two cycles after the frame event, reproduces FluBBa's NMI value.

A status read clears both pending interrupt flags and the readable sprite and
frame flags. INT is the OR of the two pending flags gated by their enables,
including when an enable is set while a flag is already pending. The status
byte has no readable line-IRQ flag. The NTSC 192-line V-counter sequence is
`00–DA, D5–FF`. H-counter reads return the held latch. On an export SMS1
without a light gun, a rising TH edge via port `$3F` updates the latch, and
ordinary reads leave it unchanged.

**Reference emulator; inference.** The H latch takes the dot three dots after
the T3 port write that raises TH. The latched value is `p/2` for `p<296`
and otherwise `(p-343)/2` with signed division toward zero, where `p` is
that dot. Latches at line cycles 209, 210 and 211 read `F3`, `F4` and `F5`.
[MesenCE](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsVdp.cpp)
adds the same three internal dots in `InternalLatchHorizontalCounter`. Its V
counter, frame flag and sprite overflow share its cycle 315, and its line
event follows one CPU cycle later. Our own HALT and NOP-loop test program
gives the same H values in this model, MesenCE 2.2.1 and Emulicious, with
HALT and NOPs alike. VDPTest, MacDonald and FluBBa fix only the total offset
between the T3 access and the latched dot. How the three dots divide between
the I/O chip's write edge and the VDP's TH input is not established.

**Inference.** A model that keeps port accesses at T2 is equivalent. It must
add six dots to the latch, move the V counter and overflow to dot 312, and
make the frame bit readable at dot 313, before its pending point at 315. The
observations above cannot separate the two. T3 follows the Zilog figure and
keeps the frame flag a single event. These sources also fix the VDP events
only relative to each other and to the CPU sample points, not to power-on.
Moving every interrupt four cycles later and keeping the other events would
satisfy them equally. The power-on phase, VDP dot 1 at CPU cycle 0, follows
MesenCE as described under the fixed machine start. This keeps the frame IRQ
at the absolute cycle where the B1b comparison matched MesenCE.

## Mode 4 picture

**Hardware documentation.** The [VDP register map](https://www.smspower.org/Development/VDPRegisters)
and [MacDonald's VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
describe the 315-5124 mode 4 background: 32 by 28 name entries, 4-bit planar
patterns, horizontal and vertical tile flip, palette select, opaque
background priority, and CRAM color zero as transparency. R2 bits 1-3 select
the name table with bit 0 controlling the row mask. R7 selects the
sprite-palette backdrop entry, R1 bit 6
enables display, and R0 bit 5 blanks the left eight pixels.

**Inference.** The VDP notes say R3 and R4 should be set for mode 4, but do
not specify the behavior of unset bits. The renderer treats them as address
masks on the low and high pattern planes, respectively; the byte-array tests
cover this model, which remains to be confirmed on hardware.

**Hardware documentation.** The same sources specify sprite Y as one less
than the first covered line, Y=`D0` as the list terminator, up to eight
sprites on a line, 8 by 8 or 8 by 16 patterns, vertical zoom for all eight
and horizontal zoom for only the first four, R0 bit 3's eight-pixel
left shift, R5's attribute table address bits and R6's pattern address bits.
Sprites use the second CRAM row; an opaque priority background pixel covers
a sprite without suppressing sprite collision. The first sprite at an
overlapping opaque pixel owns its color. The [VDPTest source](https://github.com/FluBBaOfWard/SmsVdpTest)
tests overflow, collision, offscreen-Y and terminator behavior on SMS1.
The renderer records overflow for the next line at the dot-315 event
described under VDP and interrupt timing, including when display is
disabled. It records a collision when the second opaque sprite pixel reaches
dot `9+x`; transparent pixels never collide. A status read clears both flags.
**Hardware test.** VDPTest's overflow and collision tests fix both points to
one CPU cycle relative to the H latch.

**Hardware documentation.** R0 bit 6 holds horizontal scroll at zero for
the top two tile rows, and bit 7 holds vertical scroll at zero in the right
eight tile columns. R8 is latched at dot 313 for the following line.
**Hardware documentation; reference emulator.** The [VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
say an active-display R9 write is stored for the next active-display period;
[MesenCE](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsVdp.cpp)
copies R9 into its vertical-scroll latch when the scanline wraps to zero.
The machine latches R9 at the start of line 0, before that line is drawn, so
writes during the preceding vertical blank apply to that frame. The picture
samples VRAM, CRAM and drawing registers once per visible line at dot 7;
pixel zero is at dot 9. Mid-line writes therefore affect a whole later
line rather than individual pixels.
**Inference.** The line-snapshot and R8 dots are the model's approximation to
the [VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
and [VDPTest](https://github.com/FluBBaOfWard/SmsVdpTest) timing. They keep
their positions relative to the H latch for port writes at T3, and VDPTest's
visual scroll test 90 draws the same picture with them. MacDonald's /INT
timing puts pixel 0 of the video output about 2.5 dots after dot 9. The
collision flag is assumed to lead the output by that much; this is not
measured. Each
completed output is 256 by 192 bytes of 6-bit CRAM colors. At the start of
frame N, `Machine::picture()` exposes frame N-1. The machine library does
no rendering I/O.

## PSG

**Hardware documentation.** The PSG follows [Maxim's SN76489 notes](https://www.smspower.org/Development/SN76489),
sections "SN76489 register writes", "Tone channels", "Noise channel" and
"Volume/attenuation", for Sega's integrated 16-bit variant. All writes to
`$40`–`$7F` reach it; reads still return V/H counters. There is no Game Gear
stereo register or FM unit.

- A latch byte (bit 7 set) selects one of eight registers and replaces its
  low bits. Registers 0/2/4 are 10-bit tone periods, 1/3/5/7 are 4-bit
  attenuations, and 6 is 3-bit noise control. A data byte retains the selection:
  it replaces tone bits 4–9, or the low four/three volume/noise bits. Ignored
  high bits never spill into another register. Each byte takes effect at once.
- Each CPU/16 tick decrements a nonzero tone counter. At zero it reloads the
  current period and flips its output bit. A period write leaves the counter
  running. Periods 0 and 1 contribute constant +1 to the mixer, while the
  internal divider continues to run, including the noise divider using tone 2.
- Noise has its own counter and toggled clock bit; rising edges shift the
  LFSR right. White noise inserts bit 0 XOR bit 3 into bit 15; periodic noise
  recirculates bit 0. Output is bit 0 after the shift, following the notes'
  implementation. Any noise-control write, including a data byte or an
  unchanged value, resets the LFSR to `8000`. Counter reloads are 16, 32, 64
  or tone 2's current period: shifts repeat at CPU/512, /1024, /2048 or
  CPU/(32*Tone2), respectively. A zero period clocks like one here.
- Attenuations 0–14 step down by 2 dB; 15 disconnects the channel.

**Reference emulator.** The fixed state in `initial_state.h` follows the
[MesenCE 2.2.1 PSG constructor](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsPsg.cpp)
and its zero-initialized [state structures](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsTypes.h):
tone periods/counters/output bits zero, attenuations all 15, selected register
zero, noise control/counter/clock/output zero and LFSR `8000`. Output queues
start empty. MesenCE's Game Gear panning state has no SMS counterpart here.

**Inference.** Noise-control writes reset only the LFSR, retaining the counter,
clock bit and output latch until the next rising edge. Maxim specifies the
LFSR reset without specifying resets for those other fields; MesenCE retains
them too. The noise divider reads tone 2's register at its own reload, rather
than borrowing tone 2's output edge, following Maxim's counter model.

**Inference.** CPU cycle 0 precedes the first PSG tick at 16. Subsequent ticks
and samples are at absolute multiples of 16, independently of frame or
instruction boundaries. Maxim fixes the divider ratio, not its power-on
phase. `Machine::advance` and CPU steps call the same PSG scheduler. A tick
and its sample precede a write stamped at the same cycle; that write first
affects the next sample. This total ordering is a deterministic model choice,
not a measured power-on or bus-edge phase. Actual port writes retain the
T3 timestamp described under VDP and interrupt timing.

### Write log and samples

The machine owns the PSG, its divider phase and its pending outputs; copying
or assigning it copies all of them. Equality includes pending writes, samples
and write-budget accounting. `Psg::write(byte, cycle)` appends the `(cycle,
byte)` event and consumes it through the register decoder. Both live I/O and
log replay use this path; `sync(target)` makes the samples between events.
Replay requires the same starting PSG state, events and target cycle, with
the same draining needed to stay within the output bound. Repeated syncs or
different advance sizes do not change any sample.

The log permits **8,192 writes per absolute frame** (`cycle / 59736`), and
at most **8,192 unread writes** in total. The shortest CPU output instruction
costs 11 cycles, so even an unrealistically continuous stream cannot reach
the per-frame bound. It also covers direct test-side port writes. The host
drains using `Machine::take_psg_writes()`, normally after each frame; draining
does not restore the current frame's budget. Nothing is discarded at a frame
boundary. A full log or exhausted budget throws `std::overflow_error` before
that access advances any device or changes the PSG. All accepted entries
remain available. If this exception escapes a CPU instruction callback, the
host must abort that run or restore a snapshot: instruction execution itself
is not transactional. Unit tests cover both bounds, retained data and drains.

**Inference (integer representation).** Samples are mono signed 16-bit values
at the exact rational rate **3,579,545/16 Hz** (223,721.5625 Hz), with no
resampling, filtering, host clock, audio device or file I/O. The integer table
is `round(8191 * 10^(-a/10))` for attenuation `a=0..14`, then zero:

```text
8191 6506 5168 4105 3261 2590 2057 1634
1298 1031  819  651  517  411  326    0
```

Mixing sums each channel's 0/1 output times its table entry, without division,
clipping or DC removal: range 0–32764. `Machine::take_samples()` returns the
first absolute sample cycle and contiguous values spaced 16 cycles apart,
then empties the pending sample vector. There is no cycle-0 sample. Hosts
drain samples per frame (3,733 or 3,734 samples at the exact boundary);
the vector retains all unread samples until drained. This is an internal
machine output; Part C handles device-rate conversion.

### Reference comparison

**Reference emulator.** MesenCE 2.2.1 flips the mixed tone output at periods
0/1 instead of holding +1, unlike Maxim's stated rule. Its integer volume
table also differs, including entry 7=1642, and its `Run` loop advances while
`masterClock + 16 < target`, rather than the inclusive grid above. These do
not override the hardware rules or this model's declared phase and mix.
Its resampled/filtered audio is for listening, not sample equality.

The 1,200-frame no-input comparison uses SMS1 NTSC, no BIOS, FM off, empty
save/firmware directories, zero-filled RAM and the same initial VDP phase.
Each capture must use a **new, previously nonexistent home directory** and
verify that cartridge RAM is all zero before the first instruction. MesenCE
writes battery RAM when a no-input run stops, so a directory created empty
for an earlier run is not an empty-save setup for a later run. Reusing it
changes the game's initialization path and its sound sequence. Capture helpers
reject an existing output/home directory rather than deleting saved data.
Compare byte values and order within `floor(cycle/59736)` for each frame;
keep cycle differences separate. **Reference emulator; hardware documentation.**
The earlier instruction comparison found six MesenCE duration differences
against the [Zilog manual's instruction tables](https://www.zilog.com/docs/z80/um0080.pdf):
`INC (HL)` and `DEC (HL)` take 10 instead of 11 T; `LD (HL),n` takes 12
instead of 10; indexed `INC/DEC (IX+d)` take 22 instead of 23 and
`LD (IX+d),n` takes 24 instead of 19. MesenCE also samples INT at instruction
end and invokes its port-write callback after four I/O cycles; our write is
at the start of T3. These explain why equal write streams need not have
equal timestamps. No CPU or PSG timing is adjusted to match the reference.

**Reference emulator (capture API limitation).** In CE 2.2.1,
[`ScriptingContext::InternalCallMemoryCallback`](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/Debugger/ScriptingContext.cpp)
converts non-relative callback domains through
[`SmsConsole::GetAbsoluteAddress`](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsConsole.cpp),
which discards the port domain. A direct `smsPort` write callback receives
nothing. External capture scripts observe the resulting bank-0 `smsPrgRom`
addresses `$40`–`$7F` instead, with an earlier relative `smsMemory` write
callback over `$0000`–`$FFFF` excluding actual memory writes at those cycles.
An external synthetic program validates this method against all 1,611 known
writes, their order and their cycles: immediate OUT with varying A, OUT (C),D
with varying B, OUTI/OUTD and OTIR/OTDR, including B=0's 256 repetitions,
on `$7F`, `$7E` and `$40`. Writes to memory at the alias addresses are excluded.
An independent instruction-boundary OUT-family trace gives the same events.
CE's SMS CPU discards the upper port byte before notifying its memory manager;
varying A/B therefore does not lose writes in this version. This routes the
original port-write notifications without decoding game instructions or
changing the emulator. The reference WAV is captured through CE's native
`WaveRecord` API, using its unmodified audio engine.

**ROM run; reference emulator.** With independently verified zero initial
cartridge RAM, both ROMs produce 5,873 writes over 1,200 no-input frames,
at most 36 in one frame. Every frame's bytes and order match MesenCE 2.2.1.
Matched write timestamps differ by -16 to +27 CPU cycles (PSModern minus CE).
The first IRQ entry is 462953 in PSModern and 462963 in CE. A reused,
initialized CE save instead gives its first IRQ at 342682 and 5,877 writes;
this is a different starting machine value, not a PSG timing discrepancy.
