# Milestone 20 plan - Optional retro interface

**Tier A. Status: completed and accepted on 2026-11-28. Implementation commit
`151d7b8`.** The maintainer approved the Windows/S24 play-test and the
cartridge-paced presentation first tested in the Parolit armory.

## Scope and decisions

The Options menu offers Original and Enhanced. Original is the default and recordings
use the cartridge presentation. Enhanced uses a proportional pixel font and
keeps the cartridge's menu commands, text, character names, prices and rules.
The `INTERFACE` preference persists with the save. Existing save companions
still load, and the cartridge SRAM retains its format.

- The game continues drawing its native windows. A second scanout preserves
  the scene behind them, so the interface can follow the original opening
  order, progressive row writes, restorations, fades and cursor.
- Normal dialogue and portrait scenes reveal each printed glyph, including
  inserted names and numbers. A two-line battle page leaves the party
  visible. If the new layout needs another page, the printer waits for a
  new button press after the completed page has reached the screen.
- Party cards and the enemy HP column follow the values actually printed.
  Inventory page changes replace names in sequence. Status, equipment and
  target pickers keep their parent windows visible during opening and closing.
- The shop shows prices, bag capacity and per-character equipment changes.
  Magic shows costs and effects for up to five spells. Status shows experience
  to the next level, with labels appropriate to each cartridge.
- Host Options opens its list, values and explanation in order. Continue
  preserves its two instruction rows and displays the names and cursor
  through the title screen's direct save-list entry point.

## Verification and acceptance

The plan and implementation received independent Tier A reviews. The
implementation review covered frame pairing, native wait ownership, save
compatibility and Original-mode passivity. It also verified the corrected
row timing in nested selectors, both columns of the status spell list and
the final Continue fixes for instruction rows and save selection.
The two-pixel border used in the play-test was retained.

The corrected Windows build passed 450/450 regression cases and 378/378
playthrough cases with both cartridges configured and zero skips.
The no-ROM audit verified all 325 required ROM skips. Recorded US and
Brazilian title sessions selected and loaded the second save. The signed
Android test package contains ARM64 and ARMv7 libraries from the same source.

The [player guide](player-controls.md) covers the interface setting. The
[roadmap](roadmap.md) lists the milestones and the next approved plan.

Hosted CI passed on Windows and macOS for the implementation commit. The
macOS job built the universal application, ran the no-ROM tests and verified
both the arm64 and x86_64 slices. [GitHub Actions run](https://github.com/carbiecarbie/psmodern/actions/runs/46155599061)
