# Milestone 15 plan - Original bug fixes and the sound stalls

**Tier B. Status: completed and accepted (2026-11-19).** The initial play-test build
was based on `165b646` and is identified by its SHA-256 below.

## Withdrawn cartridge fixes

After the successful play-test, the maintainer decided the five defects were
minor enough to keep the original behavior. The audio change remains.
The game again uses the six-row Options screen and the original rules for
these cases. The prototype implemented and verified the following fixes:

- Cancelling RISE or choosing a living recipient returns its MP to the caster.
- A shop checks whether the party owns a vehicle before charging for it.
- A refused use of Escaper or Magic Hat leaves the item in the inventory.
- The group attack used when the Crystal is missing can reduce HP to zero
  without wrapping back to a positive value.
- The hospital charges the full cost when missing HP plus MP reaches 256.
  With Myau at 13/214 HP and 0/55 MP, both original cartridges quoted zero.

Profiles saved during the play-test retain their encounter rate, experience
rate, automap and widescreen settings. The game discards the withdrawn switch
when reading format 4 and uses format 2 or 3 on the next settings save.
The recovery patch includes the implementation and its tests. It was checked
against base `165b646`. Its local path from the repository root is
`../m15-verification/withdrawn-cartridge-fixes/cartridge-fixes.patch`.

Both supported cartridges also let the party escape
all four investigated dungeon wall landings. The reported Tajim lockup
came from a Japanese fan patch. A visit without Noah returned to dungeon
control on both supported cartridges. [Evidence](original-quirks.md).

## Audio

During a Windows fullscreen transition the old host exhausted its queue
and added 75 ms of silence. The revised host targets a 150 ms reserve,
preserves queued samples when it gets ahead, and resumes with the next
game samples after an underrun. All PSG writes matched MesenCE across
9,886 field-menu frames, including a 33 ms interval between writes
during which the chip continues playing. [Measurements](development.md#audio-and-pacing).

## Original play-test and verification

- The play-test build passed all 394 tests with zero skips. All 30 recordings
  matched the original across 227,270 frames with fixes enabled. A separate
  RISE recording verified that cancelling left Noah with 70 MP (58 on
  the original). State probes covered the other corrected paths.
- A 1,810.24-second WASAPI session covered both menus, all three speeds,
  fullscreen toggles and focus changes. Across 108,843 frames, the host
  recorded zero underruns and zero queue resets.
- The maintainer completed the play-test and reported no problems on
  2026-11-19. The saved profile had Bug fixes enabled. The last audio report
  from that session, at 130 seconds, showed zero underruns and resets.
  The tested executable's SHA-256 was
  `b7de64ee1f7dc5cf031d02dce6284b8c614d4252a0509097867ad7c24451cfa7`.

## Verification after withdrawal

The revised executable passed all 389 tests with zero skips. In the
2,057-frame RISE recording every frame matched the original, including
Noah's 58 MP after cancellation. A format 4 profile imported all retained
settings, and tests verified saving again in formats 2 and 3. A 35-second
WASAPI check of the revised executable loaded a copy of the maintainer's
Portuguese profile and recorded zero underruns and zero queue resets.
The revised Windows executable has SHA-256
`896a0950299c5575812a4245f413c45f0b80ea35a729cc9bfddb213b5a7cce90`.
