# Milestone 5 plan - Options inside the game

**Tier A - saves and runtime options. Status: in progress; started by the
maintainer on 2026-11-11.**

## Goal and scope

Choose encounter and experience rates inside the game, retain them beside
the cartridge save, and make battle money follow the experience rate.
The default remains 100 percent for both; the recommended preset is
50 percent encounters and 400 percent experience and battle money.
The existing encounter choices (0, 50, 100, 200) and experience choices
(100, 200, 400) remain. Original mode stays exact in both supported ROMs.

This milestone adds an options window using the original status-menu visual
language and exploration context. It does not add remapping, walking speed,
multiple levels per battle, language selection or other roadmap features.

## Design decisions

- **Maintainer decision (2026-11-11):** Escape and controller Y open options
  only during free exploration in towns, the world map and dungeons, where
  the original field/status menu is available. Combat, title, dialogue and
  other menu contexts ignore the request rather than opening it later.
  The original button-2 menu and Enter/Start Pause remain available as before.
- The original location remains visible behind a black window with the
  original white pixel frame, cursor and font. Music continues naturally,
  as in the native status menu: no silencing, restart or short audio loop.
  Establish native menu entry, wait and exit behavior in both ROMs before
  selecting the integration mechanism; arbitrary-PC suspension is out of scope.
  Window focus loss and controller removal must not strand the player or
  leak menu input into gameplay.
- Show only ENCOUNTERS and EXPERIENCE / MESETA with percentage values.
  Each 100% value is marked (DEFAULT); encounters 50% and experience/meseta
  400% are marked (RECOMMENDED). Other values have no marker. There are no
  separate presets, Apply/Cancel rows or control-help footer. Confirm saves
  the choices and closes the window; cancel discards the draft and closes.
  Necessary error messages remain distinct from permanent button help.
- Render at the existing integer-scaled resolution using the player's ROM
  assets. Validate punctuation needed by the approved preview and the actual
  native frame/cursor before implementation. No extracted font pixels, game
  text or graphics enter Git. Closing returns to the same exploration scene;
  audio/console time advance through the native menu while gameplay waits.
- Applying options uses a proven safe game boundary. A single battle must
  not change rates between its experience display, individual character
  additions and money award. Establish the boundary from original execution;
  defer application with a visible explanation when an award is in flight.
  Never rebuild a registry during an executing routine. Apply persists the
  chosen preferences and queues activation; another Apply supersedes the
  pending choice. Quit/startup and power cycle use the latest chosen values;
  reset reaches the dispatcher before activation. Pending status remains
  visible until the active values match. No operation reloads RAM mid-award.
  The candidate boundary is the native main dispatcher at `00:00AF`, after
  the preceding mode handler returns. Prove its scope through ROM traces.
  Forward the existing instruction hook through run_frame; the host hook
  returns no PC redirect and swaps the registry only at this unregistered
  boundary. Preserve enabled/verifier behavior. No-op and absent hooks must
  be equivalent through accepted interrupts and frame edges.
- Keep the original 8192-byte save file unchanged in layout. A versioned
  companion file stores options associated with that cartridge image under
  the same exclusive lock. It is per version, matching the existing saves.
  Applying settings can persist them without an SRAM write; normal SRAM
  flushes keep the association current.
- Use a staged, atomic companion-file replacement containing a committed
  and optional pending image digest/options pair before publishing new SRAM.
  On load choose the pair matching the actual SRAM digest. Specify and test
  recovery at every write boundary, including equal digests for settings-only
  changes. Neither two renames nor a pair of backups is assumed atomic.
  Preserve retries and the existing first-write backup; companion recovery
  must also work with a restored original backup.
  Keep a persistent digest/options association for that session-first backup
  after later flushes retire older committed records. Test restoring it after
  several saves, settings-only changes and a new process.
  Digest ties select pending, then committed, then backup. Restoring identical
  SRAM bytes alone cannot identify an older settings choice; restoring the
  matching companion backup restores that choice explicitly.
- Missing metadata means original settings. Malformed, unsupported or
  mismatched metadata must never damage SRAM: report it, use original
  settings and preserve evidence until an explicit settings action resolves
  it. A raw save copied to a cartridge or another emulator retains its
  earned totals; importing a raw save without matching metadata uses 100/100.
  Loading enhanced totals in original mode does not undo earned rewards.
- Explicit command-line rate flags override their corresponding saved
  fields; omitted fields use saved settings. Recording and --no-routines
  enforce original settings and cannot overwrite stored enhanced options.
  Non-original explicit flags retain the existing refusal in those modes.
  Recording ignores the options shortcut, since the injected native menu
  call is not recorded input. A raw-Z80 session may show a read-only options
  window only through the same verified native menu path. Headless replay
  stays deterministic and explicit.
  Retain configured preferences separately from effective session options:
  CLI overrides remain session-only until explicit Apply. Restricted sessions
  bind configured preferences to new SRAM digests instead of writing their
  forced 100/100 session values into the companion file.
  In-game Apply supersedes the launch flags for the current session too.
- Battle money uses the experience multiplier with a wide intermediate:
  zero stays zero; clamp the reward at 65,535, then saturate the party total
  at 65,535. Current rates require no fractional rounding or minimum.
  Show the same reward that feeds the addition. As in the original, the
  displayed amount can exceed the remaining room in the party total.
  At 100 percent preserve all original state, timing and side effects.
- **ROM static:** battle rewards feed a shared chest path, while the common
  addition at `00:297A` also serves sales. Rewrite the complete battle-setup
  unit at `01:5FFE` and common addition, using the segment mechanism. Scale
  the original 16-bit reward result at its native store at `01:60BC`, before
  display; never multiply map chests or sales. Later scripted zeroing remains
  original. Prove other chest paths replace the reward and the dispatcher
  cannot apply a new rate between setup and the complete reward transaction.
  Permit the original absolute work-RAM store as a forced single-instruction
  exit only under an explicitly validated narrow contract: transform HL
  before native execution and restore it afterwards, without custom bus
  writes. Prove delayed NMI, halt, interrupt and frame fallback cannot lose
  the transform when that store actually executes. Keep native stack bridge
  instructions between registered chunks where needed, preserving the
  existing segment stack invariant; stitched-path tests cover those bridges
  as part of the whole setup unit. Verify actual
  instruction boundaries rather than trusting imported disassembly labels.
  Runtime probes must prove zero/item-only rewards, refused/trapped chests,
  scripted cases, interrupts and reentry cannot multiply a reward twice.

## Work and verification

1. Confirm money control flow, safe option-change boundaries and glyph
   mapping from the identified ROMs. Record static/run evidence honestly.
2. Independent Tier A plan review before production implementation; resolve
   findings within this scope. Independent implementation review follows
   integration, with findings fixed before the final complete test run.
3. Implement persistence, runtime selection and the host options screen in
   one isolated checkout; implement money routines, tests and explanation
   in another. Each has its own build. Integrate reviewed diffs without
   commits until the maintainer authorizes them.
4. Test storage parsing, compatibility, precedence, locking, backups,
   settings-only writes and every injected publication failure/restart.
   Test the actual app path, including restricted recording/raw-Z80 sessions,
   runtime changes, power cycle and restart, not just standalone helpers.
5. Test menu navigation, confirm/cancel, repeat/held input, focus and controller
   removal, and positive exploration-only availability. Compare continuous
   native sound and the return to exploration with the original field menu;
   rejected requests must not open later. Preserve the current background
   outside the options window in town, world-map and dungeon captures. Render
   and inspect both versions, including the smallest window. Recording ignores
   the options shortcut because native menu injection is not recorded input.
6. Verify every rewritten segment against original Z80 state and cycles,
   including flags, R, memory accesses, interrupts and frame boundaries.
   Probe reward extremes and shared nonbattle callers in both ROMs; verify
   displayed amounts and additions in full paths, with options on and off.
   Run all existing US/BR recordings and add coverage where the new paths
   need it. Check all multiplier input values and saturation boundaries.
7. Update the party explanation, developer/player instructions, generated
   coverage and private annotations/catalog in the same work. Keep ROM
   material private and working captures outside both repositories. Update
   project status/history/roadmap only at actual milestone closure.
8. Build final production code and run the complete configured suite per
   tools/AGENTS.md: a Luna runner, -j 16, no skipped tests. Do not modify the
   checkout during that run. Investigate failures before any rerun.

## Acceptance

All tests and both independent reviews pass. A fixed isolated US play-test
build and Portuguese guide cover only human-dependent readability, feel and
real keyboard/controller use; the agent first verifies automated behavior
and inspects rendered frames. Hold one maintainer session after all parts.
The maintainer accepts that result before this plan records completion.
Commit/push authorization and the final acceptance are requested together
at the end when practical; no commit, push or acceptance is implied by
starting implementation. Both repositories must retain their intended scope.
