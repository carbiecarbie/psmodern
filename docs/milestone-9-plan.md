# Milestone 9 plan - Text and language selection

**Tier A. Status: completed and accepted (2026-11-13).**
Implemented and accepted in `778aebf`, based on `966c21f`; private text
annotations are in `psmodern-re` commit `1b98bc5`. Ninth milestone of the
[roadmap](roadmap.md).

## Scope delivered

- The text system runs in C++ through the existing segment mechanism:
  message readers, dictionary expansion, control codes, dynamic names and
  numbers, character output, line breaks and waits. Both cartridges retain
  their own text, control paths and timing. Native menus/windows remain Z80.
- English/Portuguese switching is available during free exploration from
  the fifth Options entry, after dialogue, battle and other menus finish.
  The launcher also opens Language. Each language requires the player's
  corresponding verified ROM; no cartridge data is distributed.
- One PLAY entry uses shared saves, options and automap regardless of the
  available ROMs. Legacy US/BR progress is preserved and imported once.
- PSModern's launcher, Options, Controls, map, titles and messages are
  localized. ROM-font Portuguese uses uppercase without accents. Logs,
  configuration tokens, identifiers and documentation remain English.
- [The text chapter](how-it-works/text.md) explains the routines and switching;
  the private annotated study was updated alongside the implementation.

## Key decisions

1. **The safe boundary:** after native Options cleanup and host restoration,
   before the next resumed game instruction. Static residency analysis and
   directed probes cover localized RAM/VRAM and interrupt consumers, including
   the save-name operand at `01:4181`. Nothing is cleared speculatively.
2. **Only the ROM changes in the machine.** CPU, cycles, RAM/SRAM, mapper,
   video, audio, RNG and input stay intact; prior machine copies retain their
   original immutable ROM. Machine comparison stays strict. Both catalogues
   are validated before language SAVE; the equivalent registered callbacks
   read the active ROM and remain installed, without allocation at the swap.
3. **Retained game data:** equal audio/gameplay tables allow TempoAudio,
   DungeonTables and automap history to remain intact. Power cycle uses the
   active language and shared saves. Switching never repeats the opening.
4. **Global preference:** versioned `language.txt`, atomically written only
   by Language SAVE. Its CANCEL discards the preview; cancelling parent
   Options does not undo a saved language. In-game replacement waits for
   native menu closure. Write failure retains the prior choice and allows
   retry; quitting with a queued swap retains the saved preference.
5. **Available ROMs:** automatic uses US when both are present, otherwise
   the available cartridge; explicit CLI startup supplies its own origin.
   An unavailable preferred language falls back with a localized warning,
   preserving the preference. Recording and `--no-routines` retain the startup
   ROM, disable switching and never write the global preference.
6. **Shared storage:** normal, recording and raw-Z80 sessions use
   `shared/phantasy-star.sav`. Existing formats and native slots stay unchanged;
   headless replay is unchanged. Existing shared storage is authoritative,
   even when empty; malformed storage errors rather than loading old progress.
7. **Legacy import:** under root and legacy locks, snapshot complete sets of
   save, options and automap files and their backups. Import one valid set
   automatically only when the other is absent, or when both are identical.
   Different sets require a one-time choice or Cancel, independent of language.
   Unreadable, locked, nonregular or oversized artifacts are unavailable, never
   silently absent; using the other candidate then requires a choice. No
   available candidate means an error. Current saves must be 8192 bytes and
   artifacts are bounded at 16 MiB; readable companion/backup bytes are opaque.
8. **Preservation:** flush staged copies and publish the complete directory
   atomically without replacing an existing destination. Interrupted staging
   is never progress. Bounded cleanup touches only its own stage and warns if
   it remains. Legacy data, companions, backups and timestamps stay untouched;
   only lock files may be created. Later saves never synchronize old sets.

## Results and acceptance

- Independent Tier A plan and implementation reviews approved the delivery.
- Both-ROM segment/state/timing checks, original recordings, Brazilian text
  recording and directed probes passed. Switching was tested in towns, world
  map and dungeons, including subsequent localized content and power cycle.
- Persistence, missing ROMs, write failures/retry, cancellation, queued quit,
  held input/focus, restricted modes, automap and accelerated audio were tested.
  Legacy tests cover conflicting/identical/companion-only sets, locks, concurrent
  initializers, Unicode paths, crashes and unchanged original files.
- Host screens were rendered and inspected in both languages; the native
  legacy-choice dialog was also checked at actual Windows scaling.
- Complete suite: **178 passed, 0 failed, 0 skipped**, with both ROMs and all
  reference fixtures. ROM-free CI configuration: **60 passed, 0 failed**;
  `reference_driver` and `vdp_conformance` skipped without their external
  fixtures, and passed in the complete suite.
- The maintainer completed the final isolated play-test and reported everything
  correct on 2026-11-13. The shared save, lock, saved options and Portuguese
  preference were inspected afterwards. No skipped play-test item was reported.
  Accepted executable SHA-256:
  `0dd5b15d44da78e1cf449561b94228ed8e38e7a5afdb651465ba86442d5ae74c`.

New languages, community translation, merging different native save slots,
menu redesign and release publication were outside this milestone.
