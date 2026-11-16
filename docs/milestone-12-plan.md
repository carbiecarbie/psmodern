# Milestone 12 plan - Menus and windows, then the new interface

**Tier B. Status: completed and accepted (2026-11-15).** Implemented in
`eacc0d2` and closed by `467e589`. Twelfth milestone of the
[roadmap](roadmap.md); stage 9 of [design](design.md) section 8.

## Scope delivered

- The game's menus and windows run in C++ in both versions, as 323 short
  pieces ("segments") in six groups, each verified on every call against the
  original: the window engine and the number and name printers (29: `00:3AA6`,
  `00:3A57`, `00:3A83`, `00:2FE1`, `00:356B`, `00:35C4`, `00:3439`); the
  windows, field, item, status, shop, save slots and battle (125: `00:2ED9`,
  `00:34D5`, `00:37CF`, `00:38D9`, `00:39A5`, `00:3105`); choosing with the
  cursor, the yes/no questions and the waits (25: `00:2D51`, `00:2DA5`,
  `00:2D19`, `00:2E62`, `00:01AD`); the field menu with status, magic, items,
  equip, drop, search, save and chests (63: `00:1BE1`, `00:1CA1`, `00:1CDF`,
  `00:1D4D`, `00:2168`, `00:26C8`, `00:2839`, `00:28DB`); the hospital, the
  church and the shops (58: `00:298D`, `00:2A85`, `00:2BC0`, `00:2C98`); and
  the palette fades every screen change uses (23: `01:7B05`, `01:7B20`,
  `01:7B40`, `01:7BEE`, the palette effects).
- Two new US recordings, `field-menu` and `places`, equal to the patched
  MesenCE on every frame, from game-made saves with documented edits of saved
  fields.
- The Options screen redone as three windows in the game's own style, with
  the cartridge font and borders: the six rows on one page, beside the
  selected row a window with every choice (the current one highlighted), and
  an explanation window whose last line shows the state. Left and Right change
  a value directly. Language is an ordinary row that switches at once; Controls
  and the start screen use the same windows. Portuguese ON/OFF read SIM/NAO,
  the language names stay in their own words and AUTO follows the ROM played
  (Maintainer decision, 2026-11-15, design approved from an interactive mockup
  and accepted in the play-test).
- The [menus chapter](how-it-works/menus.md), the original quirks found,
  development notes, the coverage census (twenty checkpoints) and the new
  `docs/images/options.png`. With every option off the game stays the
  cartridge.

## Key decisions

1. **No change to the registered-routine mechanism** (hence Tier B). Windows
   are drawn with interrupts off; the instructions that switch interrupts off
   and on stay on the Z80 and the pieces run between them, as the text output
   has since milestone 9 (`00:33D7`). Reading video memory to keep what is
   under a window uses the existing port access, through a kit
   (`src/game/menu_kit.h`) proved against the core.
2. **Stays on the Z80 until milestone 13:** the interrupt switches and the
   instruction next to each; the cartridge-RAM switching and the 1,024-byte
   copy of a save (`00:1D0E`-`00:1D32`); the cancel point `00:2D93` and the
   save point `00:1D2D`, which the host watches; the dispatchers and the frame
   waits. Two loads and a row end also stay native because the frame boundary
   falls on them on most dialogue scrolls and window rows (ROM run): `00:3451`
   and `00:3454` before the scroll's last window call, and the whole end of
   each row of a window drawn at once, `00:3A97`-`00:3A9E` (six instructions,
   52 cycles) (Maintainer decision, 2026-11-15).
3. **Options, Language and the automap open as before,** holding the game in
   the field menu through `00:0B14` and `00:0F48`; tests compare the
   instructions the host watches, and a hold, with routines on and off.
4. **Long window copies are cut into pieces** per row (milestone 7's loop
   exits): a whole window saved or restored crossed a frame boundary on 20 to
   47% of calls (ROM run).
5. **Fades are taken now; the other tile animators wait** for milestone 13
   (Maintainer decision, 2026-11-15).
6. **Left for milestone 13** (Maintainer decision, 2026-11-15): what items and
   spells do (the spells are shared with battle), the battle commands, the
   name entry and the title menus, and the tile animators that are not the
   map's.
7. **New US recordings only** (Maintainer decision, 2026-11-15): the menu code
   is the same in both versions (design 5.5), so no Brazilian recordings were
   made and probes run on both ROMs.
8. **The code is written from understanding.** The review required the field
   menu file to be rewritten from generated listing form (one case per
   original instruction) into one function per command with structured control
   flow and a comment on every step; the window wrappers were produced from the
   segment table and then checked, each a handful of loads between calls. A new
   look for the game's own menus is optional and undecided (roadmap).

## Results

- Tests on both ROMs: unit tests of the kit and each group, state probes of
  every piece, scenario probes from real states, the host-watched instructions
  and a hold at both waits with routines on and off, and the twenty
  checkpoints with their `_options` runs.
- Coverage: 99.8% of the menu calls run in C++ (693,170 of 694,553 on the
  twenty recordings), the lowest share of a gated entry is 93.9%, no call is
  forced; 296 of the 323 pieces are reached by a recording and the other 27 by
  probes. Automap and widescreen passivity also run on `field-menu`.
- Complete suite: **266 passed, 0 failed, 0 skipped**, both ROMs,
  in about 17 minutes at `-j 16`. The citation check passes.
- The independent implementation review (authorized by the maintainer)
  approved with required changes and no behavioural defect; applied: decision
  8, the corrected statement of what stays native, a wrong comment about a
  carry, and five of its seven recommended items (1, 3, 4, 5 and 7: quirk
  labels, test assertions, the private annotation, the save-process test's
  scheduling, line endings and wording). The suite above ran after the fixes.
- The maintainer play-tested in one US session with the kit (the redone
  screens, the field menu, a hospital, a church, the shops, chests, fades,
  saving and Continue, a battle) and approved everything with no difference
  from the original. The kit predates the review's code fixes; he decided the
  play-test need not be repeated (Maintainer decision, 2026-11-15).

## Known limits

- **Native stretches** of decision 2 remain until milestone 13.
- **Probe-only paths:** 27 pieces (among them the second kind of shop and some
  story-bound palette effects); no Brazilian recording reaches the places or
  the item list; `field-menu` ends on the save-name screen (milestone 13 code).
- **The order of video writes inside a piece is not logged** (review
  recommendation 6, not made): the verifier compares the final state, the
  cycles and the last port access, so that order rests on the kit's timing
  tests, as in milestone 11.
- **The Options page covers more of the scene** than the old window, because
  the cartridge's letters fill their tile; the maintainer accepted it.
