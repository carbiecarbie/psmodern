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

## VDP and interrupt timing

**Hardware documentation.** The [SMS VDPTest](https://github.com/FluBBaOfWard/SmsVdpTest)
reports frame IRQ at HCount `F3`, line IRQ at `F3`, V-counter change at `F4`
and readable frame status at `F5`. Its author reports testing the original
SMS1 VDP. The later [ZClock table](https://www.smspower.org/forums/10695-SMSVDPTester?start=50)
clarifies that the test's event offsets are CPU T-states relative to HCount
zero: respectively -18, -17, -16 and -15. The table maps these to pixel
offsets -27, -26, -24 and -23. The measurements and their coordinate origins
are discussed in the linked sources; the IRQ and status tests are separate.

The scheduler uses dots, 342 per line, with dot 0 at HCount 0. The four
events above are at dots 315, 316, 318 and 319 respectively. CPU cycle 0 is
VDP dot 1; each CPU T-state advances three half-dots. An event between CPU
cycle boundaries becomes visible at the next boundary: absolute dot `d` is
processed at `ceil((2*d - 2)/3)` CPU cycles. This fixed phase describes the
chosen mapping of the hardware test coordinates and the fixed starting
state. The MesenCE VDP combines its frame IRQ and readable status event at its
cycle 315; its counter-latch offset means its cycle coordinates are not
directly interchangeable with the hardware-test coordinates. In the
[MesenCE source](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsVdp.cpp),
the V-counter advances at cycle 315, the line counter is clocked at 316 on
physical lines 261 and 0–191, and the H-counter latch adds three internal
cycles before conversion. The model follows VDPTest for the reported event
separation and H-count relation, rather than assuming the reference's phase.

**Hardware documentation; inference.** The frame IRQ's internal pending flag
sets on physical line 192 at dot 315; the status bit becomes readable at dot
319. A status read in between returns bit 7 clear if it was clear already and
clears the pending IRQ. The test documentation does not establish whether a
read in this interval cancels the later status bit; the implementation assumes
it does because the read clears the underlying pending state. No status read
in that interval occurred in the two 1200-frame no-input runs. Their executed
sites were `00:0039`, `00:031B`, `00:0321` and `00:0328`.

The line counter is clocked at dot 316: it decrements on physical lines
261 and 0–191, and reloads R10 on 192–260. This is the same 0–192 decrement
window described by the [VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt)
when lines are named by the V-counter value after its transition at dot 318.
Underflow sets the line pending flag and reloads R10; a write to R10 takes
effect at the next reload, not immediately. A status read clears both pending
interrupt flags and the readable sprite and frame flags. INT is the OR of the
two pending flags gated by their enables, including when an enable is set
while a flag is already pending. The status byte has no readable line-IRQ
flag, and the hardware tests do not establish a separate delayed line flag.

For an instruction occupying CPU cycles `[S,E)`, the machine supplies the
core with the INT level sampled at `E-1`, after any earlier status read in
that instruction. An event first visible at `E` is sampled by the following
instruction. The core's prefix, EI, RETI/RETN and NMI acceptance rules also
determine whether an interrupt is accepted and the boundary hook is called.
The NTSC 192-line V-counter sequence is `00–DA, D5–FF`. H-counter reads
return the held latch; on an export SMS1 without a light gun, a rising TH
edge via port `$3F` updates it and ordinary reads leave it unchanged.
