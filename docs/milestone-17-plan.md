# Milestone 17 plan - Japanese FM soundtrack

**Tier A. Status: completed and accepted (2026-11-22).** The maintainer
passed all seven play-test items on Windows and on the Galaxy S24 Ultra and
accepted the milestone.

## What shipped

- A player who also owns the Japanese cartridge can hear the game with its
  FM music and sound effects, on Windows and Android. The game played stays
  the US Rev-A or Brazilian one, with its own timing and saves.
- The Japanese image is a sound source, never a game. It is identified by
  its full digest (SHA-1 `c9a40ddd217c58dddcd6b5c0fe66c3a50d3e68e4`), added
  with ADD ROM like the other cartridges, and remembered in its own file so
  that release 1.2.0 still reads `roms.txt`. Alone it starts no game; as the
  command-line ROM it is refused.
- Options has a seventh row, SOUND. PSG, the sound of the US and Brazilian
  releases, is the default. Left or Right switches to FM and back, heard and
  saved at once, with the music at the same point. Without the Japanese ROM,
  or after a failure, the row stays on PSG and says what FM lacks.
- With the Japanese ROM registered, a private engine runs the Japanese sound
  driver on the whole Japanese image from power on and receives every
  request the game makes. Its 191 pieces are C++ with the original cycle
  costs; only its FM branches are covered. The listener hears either the
  game's PSG or this engine's FM chip; FM mode is the FM chip alone, as on
  the Japanese console.
- The FM chip is Nuked-OPLL, vendored unmodified under `third_party/`. Its
  licence and notices ship in the Windows zip and the APK.
- An unexpected access, an exceeded cycle bound or a bad return stops the
  engine until power cycle and the listener returns to PSG. Recording uses
  the playable cartridge alone and ignores a saved FM choice.

## Key decisions

Maintainer decisions of 2026-11-22: a copy for the listener only; music and
effects both from the FM chip; the Japanese driver always running for
instant switching; Nuked-OPLL as the most faithful chip emulator; Windows and
Android together; SOUND always shown and saved at once, as Language is; the
mockups' seven-row layout; "FM OFF UNTIL POWER CYCLE." for a stopped engine;
design sections 5.1, 7 and 9 updated. At normal speed the tick and the
surviving request reach the Japanese driver together; during fast-forward
the existing policy holds.

## Verification

- **Driver against the original.** Every piece matched the Japanese driver
  running on the Z80 in the tests: 1,528 seeded instruction comparisons and
  6,126,709 piece boundaries over every music, effect and control request,
  with registers, flags, memory, mapper, timed port writes and cycles.
- **Reference emulator.** On the opening, the Z80 rig agreed with the
  patched MesenCE on all 3,266 FM writes over 398 updates, including an
  injected effect and a fade.
- **FM changes nothing in the game.** With FM on and off, the same inputs
  gave identical gameplay state, cycles, picture, PSG and save bytes at every
  frame, and FM samples reached the host's playback. The production
  scheduler is checked against an expected stream at 1x, 2x and 4x, and a
  forced live failure falls back to PSG.
- **Phone.** On the S24 (ARM64), 4x held 239.70 frames per second with FM
  and 239.65 with PSG, with zero underruns. 32-bit phones are unmeasured.
- Independent reviews approved the plan, the backend (four findings fixed)
  and the Options row (three findings fixed). The complete suite passed
  430/430 with zero skips.
- The maintainer's play-test passed: FM music and effects, loudness against
  PSG, switching during play, 2x and 4x, pause, and the phone.

## Known limits

Effect `AF` ends about half a second later in FM than in PSG, as on the
Japanese cartridge. Switching can sound an already-held note again. FM gain
is fixed and was confirmed by ear in the play-test. Not in this milestone: playing the
Japanese game or its text, the Japanese driver's PSG mode, FM music with PSG
effects, a sound test screen.
