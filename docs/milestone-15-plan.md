# Milestone 15 plan - Original bug fixes and the sound stalls

**Tier B. Status: planned, not started (2026-11-19).** Fifteenth milestone of
the [roadmap](roadmap.md); the first after the complete C++ reimplementation.
The maintainer asked for it on 2026-11-19 and starts it explicitly.

## Goal

1. One new option, off by default, that fixes defects of the original game.
   With it off the game stays the cartridge, as today.
2. Find the cause of the short sound stalls and remove it. This part is not
   an option: a stall that comes from PSModern is a defect of PSModern.

The two parts touch different code (`src/game/` and `src/app/`) and can be
done in either order or by two agents in separate worktrees.

## Part 1 - Bug fixes option

No community list of bugs for this game was found (2026-11-19 search); the
source is [original quirks](original-quirks.md). The lists below are the
planner's proposal; the maintainer may move entries before the start.

**Fix (each already has ROM static and ROM run evidence):**

- [ ] Revival keeps its MP when the choice is cancelled or the recipient is
      already active (`00:20BF`-`00:20D8`).
- [ ] A shop does not charge for a vehicle-class item the party already
      carries (`00:2C09`-`00:2C1D`): check first, then charge.
- [ ] Escaper and Magic Hat are not spent when their use is refused
      (`00:23E2`, `00:2419`).
- [ ] The Crystal-dependent group action cannot wrap a member's HP
      (`00:1561`, `00:1514`, `00:15C2`): the result is clamped.

**Investigate first; fix only if play can reach it (report the evidence):**

- [ ] The hospital price wrapping at 256 (`00:29E4`-`00:29F3`): compare with
      the largest missing HP plus MP the game's tables allow.
- [ ] Stairs and the pitfall that land inside a wall (`01:6715`, `01:6734`,
      `01:661C`; floors 10, 55 and 25): can the party reach them, and what
      happens next? Fix only if the player can get stuck.
- [ ] A lockup when visiting Tajim, named by a retranslation project's change
      log (Community source, about the Japanese ROM): check whether the US
      Rev-A and Brazilian code has it.

**Not fixed (intent unknown, or it changes balance or input feel):** the
strength gates that read the first records, direct death ignoring protection,
the second search place, the item list losing a confirm pressed with a
direction, traps, doors closing on reload, the experience ceiling, and every
text matter (text is read from the player's ROM).

Tasks:

- [ ] Add `bool bug_fixes=false` to `game::Options` (`src/game/options.h`), a
      `--bug-fixes on|off` flag, and a seventh Options row with its choice and
      explanation windows in English and Portuguese (cartridge font: uppercase,
      no accents). The explanation lists what is fixed in player terms.
- [ ] Save it in the options companion file as a new format, written only
      while the option is on, as widescreen did with format 3; older formats
      read as off. Read-only while recording and with `--no-routines`.
- [ ] Each fix is one guarded branch inside the routine it corrects. With
      the option off the routine runs as now, and the existing lockstep runs
      prove it. With it on, the fixed path has no original to compare with:
      state probes check its result, and that every byte it does not mean to
      change, the cycles of the untouched paths and the sound requests stay
      as the original's.
- [ ] One test runs every recording with the option on and reports where the
      run leaves the original; a recording that never meets a fixed path must
      stay equal on every frame.
- [ ] Docs: each fixed entry of `original-quirks.md` gains a line saying the
      option changes it (Maintainer decision, with the date); the matching
      how-it-works chapters, `development.md`, both READMEs, and the option
      list in the root `AGENTS.md`. `design.md` needs the maintainer's word.

## Part 2 - Sound stalls

**Maintainer report (2026-11-19).** Short audio faults heard several times,
one of them while opening the menu near the start of the game, during the
1.0.0 play-test. Earlier measurement: stalls of about 90 ms in one of three
host runs, cause Unknown ([development](development.md#audio-and-pacing)).
Which menu it was (the game's field menu or Options) is not recorded; test
both.

- [ ] Reproduce with the queue diagnostics on, logging each underrun and
      queue reset with its frame number and what the host was doing.
- [ ] Decide which of two things each fault is:
  - **The cartridge's own behavior.** Windows are drawn with interrupts off,
    so the sound driver may miss frames. Compare PSModern's sound with the
    patched MesenCE on the `field-menu` recording. If the original does it,
    it stays, and is recorded in `original-quirks.md`; smoothing it would be
    a new option and needs the maintainer's decision.
  - **The host.** A lead, not yet evidence (Inference from reading
    `Audio::submit` and `Audio::reset` in `src/app/sdl_host.cpp`): an empty
    queue, or one above 150 ms, clears the queued sound and primes 75 ms of
    silence, which would be heard as a gap of about that size. Opening
    Options, pacing after a slow frame, vsync and focus changes are the
    suspects for what empties or overfills the queue.
- [ ] Fix a host cause without clearing sound that was already queued and
      without changing what the machine produces; keep latency bounded.
      Put the queue policy behind a seam so a unit test can drive it with a
      simulated clock and queue.
- [ ] Update the audio section of `development.md` and the known gap in
      `project-status.md` (at closure).

If the fix needs changes to frame pacing in `frame_driver` or to the
machine, stop and ask the maintainer for an independent review (Tier A area).

## Acceptance

- Complete suite passing once, after the last production build
  (`tools/AGENTS.md`); option off equals the original on every recording.
- A 30-minute session log with no queue reset and no underrun, including
  opening both menus, fast-forward, Alt+Enter and losing focus.
- One maintainer play-test at the end: the Options row in both languages, one
  fix seen in play from a prepared save (the vehicle purchase or revival),
  and listening for sound faults while opening menus.
