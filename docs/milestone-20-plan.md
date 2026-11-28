# Milestone 20 plan - Optional retro interface

**Tier A. Status: Parolit pacing approved on Windows on 2026-11-28; maintainer play-test approved on 2026-11-28, local regression passed, hosted CI pending.**
Twentieth milestone of the [roadmap](roadmap.md); prerequisite for
[milestone 21](milestone-21-plan.md) (SMS Power! v2.6.1 retranslations).

## Goal and scope

Add an opt-in **Retro Interface** (`INTERFACE`: `ORIGINAL` / `ENHANCED`,
PT-BR `ORIGINAL` / `NOVA`, **ORIGINAL** by default) rendering game windows and
text in a crisp **256x192** 1x-pixel presentation layer using a proportional
pixel bitmap font (VWF: ASCII, curly quotes, ellipsis, and full upper/lowercase
Portuguese accents), keeping original menu flows, controls and visual identity.

- **Strict source fidelity:** works on US Rev-A and Brazilian Tec Toy ROMs
  using each cartridge's own item, spell, enemy, character and menu names
  verbatim (never renaming or translating ROM names). Only PSModern's own short
  labels and helper lines are localized in EN and PT-BR.
- **Original mode unchanged:** with `INTERFACE=ORIGINAL` (and in recording
  mode), cartridge state, cycles, VDP/PSG/FM writes, saves and cartridge picture
  output stay byte-identical to release 1.4.0 (host Options screens change as
  part of the 8-row redesign).
- **Approved information aids (v2.2 mockups):**
  1. VWF dialogue/lists with balanced wrapping and automatic 2-line battle
     pagination (`▼`).
  2. **Magic (field/battle):** MP costs via `00:1B87`, one-line effect
     descriptions (not repeating name/cost), current MP in field magic, and
     dynamic battle list height for up to 5 spells (Alis/Noah) with `w=92`
     (`x=4..96`, enemy name `x=100, w=94`) for >= 2 px horizontal gap.
  3. **Shops & equipping:** shop browse (`00:2BC0`) shows equippability
     (`00:26C8` table) and per-member ATK/DEF deltas (`00:17BA` byte arithmetic),
     or an effect line for consumables/tools, keeping direct purchase (updating
     Meseta/Bag before "Anything else?"). Equipping (`00:26C8`) adds the
     vertical Up/Down member selector (`00:3665`) with `ATK`/`ATQ` for weapons,
     `DEF` for armor/shields and refusal text for ineligible members.
  4. **Inventory (`ITEM`):** highlighted item description + `18/24 · 1/3`.
  5. **Status (`00:37CF`):** `NEXT`/`PRÓX.` EXP from `00:2B84` (handling the
     level-30 cap), keeping BR labels (`NV`, `PF`, `PM`, `ATQ`, `DEF`, `PE`).
- **Out of scope:** bestiary, permanent overworld HUD, menu-flow rewrites,
  16:9 side panels and SMS Power! importing (milestone 21).

## Motion and pacing

**Maintainer decision (2026-11-28).** The enhanced interface must preserve the
rhythm of opening windows and reading dialogue. The Windows play-test exposed
complete paragraphs and filled menus on their first visible frame.

Reuse the font, page layout, cartridge readers, clean scene and Options.
Rework the information passed from the native printers to the renderer.
Implement the revision in this order.

1. Trace the Parolit shop at 1x using the prepared save and recorded inputs.
   Mark the frames when the greeting box starts and finishes opening, its
   first and last glyph appear, the cursor accepts yes/no, and the list and
   money window appear. The existing side-by-side clip is the baseline.
2. Make that sequence playable in Enhanced. In `RetroTextSession`, lay
   out the entire page once and track how much the cartridge printer has
   actually emitted. Draw that prefix in its final positions. In
   `WindowState`, track rows drawn or restored for each active window and
   clip its contents accordingly. Capture both kinds of progress with the
   displayed frame. Preserve immediate redraws, original input handling,
   and the ordering of prices, helper text and the selection cursor.
3. Include menu opening in the Parolit prototype. The field commands and
   party cards follow their cartridge rows. In the host Options screen,
   unroll the list, then its values, then its explanation, at one tile row
   per game frame. Show both menus in the comparison.
4. Deliver a short Original/prototype comparison and a Windows build at
   the shop save. Check intermediate frames against the trace and confirm
   that advancing the greeting and making the purchase take the expected
   presses. Use this scene for maintainer feedback on the feel.
5. Apply the verified behavior to field menus, inventory, equipment,
   status, battles and portrait dialogue. Cover appended and substituted
   text, scrolling, page clears, nested windows and individual party cards.
   Verify 1x/4x, held confirm, Pause, Reset and fades on both cartridges.
   Then run the full regression suite and prepare the Windows/S24 play-test.

The first Windows prototype and comparison are in
`../m20-parolit-windows/`. In the recorded US route the greeting box opens
on frames 583–588 and the yes/no box on 645–649. The money window starts
on 659 and the stock list opens on 702–709. In the Brazilian recording the
corresponding frames are 580–585, 632–636, 646 and 688–695. Both routes
purchase one item for 75 Meseta. The maintainer approved the opening rhythm
in the Windows prototype on 2026-11-28. The field commands lost their last
rows behind the member picker, and ordinary map conversations still appeared
as complete pages. The follow-up build tracks field commands independently of the member picker
and follows the regular map conversation dispatcher, including nested
scripts. Four US/BR NPC recordings checked 7,900 frames against native
printing, button acceptance and closing.

The rollout also covers the portrait printer, two-line battle text, item
page redraws, equipment, status, save slots, shops and battle windows.
Enhanced keeps a parent visible while a child opens or restores individual
rows. The two columns in the status spell list follow the times at which the
native single column prints each spell. Cursor input stays with the active
selector. US/BR focused tests include intermediate pictures, opening captions,
held confirm, Pause and Reset. The independent implementation review
rechecked the split status column and nested selection regressions after
their fixes.

The final Windows regression covered 450 cases. Three UI tests checked
Options labels before their animated rows were visible. All three passed
after the test scripts were updated, followed by all 202 fast tests.
All 378 playthrough cases passed with both cartridges configured. The
separate no-ROM run passed the Windows skip audit. The frozen Android
APK includes ARM64 and ARMv7. The package, source and executable hashes,
guide and notes are in `../m20-playtest-final/`. On 2026-11-28 the maintainer reported that all other play-test checks passed.
The two defects were in Continue. Its load/delete labels now follow the
cartridge's two rows and scroll out when the slot instructions replace them.
The title's direct save-list entry now draws the slot names and the moving
cursor. US and Brazilian recordings selected and loaded the second save
with the corrected renderer. The maintainer subsequently confirmed approval
of the play-test. On the corrected build all 450 regression cases and all
378 playthrough cases passed with zero skips; Windows also passed the
separate no-ROM audit. Hosted CI for this working tree is pending.

## Key design decisions

1. **Design amendment (`docs/design.md`):** narrow exception to "No new art"
   for the VWF pixel bitmap font (10 px cell, clean `l`, 1 px gap under
   uppercase PT-BR accents, curly quotes `‘ ’ “ ”` and ellipsis `…`).
2. **Canonical VDP preserved; presentation-only `CleanBackground`:**
   - Never suppress native VDP writes or alter `$D700`-`$DEAF` buffers. Every
     VRAM readback (`00:3AA6`), dialogue scroll (`00:3439`), cursor IRQ
     (`00:2E62`) and opening row draw (`01:436C` via `00:3A68`) runs
     canonically inside the machine.
   - Capture retro UI snapshot state at the **frame observer** alongside
     `WideView` (never reading live RAM after `run()`), verifying exact pairing
     between each snapshot and the displayed picture. `CleanBackground` tracks
     scene pixels beneath active native windows and composites the VWF overlay
     (2 px outer rounded borders, 1 px dividers) over it, applying `01:7B40`
     fades and `01:7BEE` flashes cleanly, keeping Options/Automap on frozen
     backgrounds, and resuming native rendering on title, name entry and
     scripted credits.
   - **Tall battle enemies:** 4-hero strip fixed at `y=150..188`; temporary
     2-line paginated battle messages sit at `y=114..147` (covering only tall
     boss feet such as Medusa `y=144` or Lassic while preserving all 4 heroes).
   - **Mandatory prototype gate (Part B0):** before screen rollout, prove
     `CleanBackground` + VWF compositing on: (1) nested window restore,
     (2) scene pixels changing underneath an open window then exposed by a
     partial redraw or close (checking revealed pixels, fade/flash phase, first
     frame after reset and canonical VRAM/window buffers at matching boundaries),
     (3) single-member redraw (`00:2F93`), (4) cursor blink (`00:2E62`),
     (5) dialogue scroll (`00:3439`), and (6) opening row draw (`01:436C`).
3. **`RetroTextSession` exact observation, single wait owner & caller resume:**
   - **Observe each dialogue event exactly once:** split `00:31CF` before a
     shared observed dispatch boundary at `00:31D4` (avoiding inline execution
     of an observed entry inside `00:31CF` that trips the verifier), classify
     `fresh` vs. `append` (`00:31CB`) from the native window-open flag
     (`$C2D3` tested at `00:31D8`), distinguish alternate-layout re-entry
     (`00:33C9` jumping back to `00:337D` after page clear) from a fresh
     `alternate_layout` event, and observe `close` (`00:3464`, covering story
     tail-jumps and `01:4765` return-address discard). Specify exact ownership
     of continuation PC, SP, slot-2 mapper, `$C2D3` and dynamic values via a
     resumable top-level wait (never redirecting beneath a suspended routine).
   - Expose a pure `TextSource` interface keyed by game event + dynamic
     arguments so milestone 21 packs never touch CPU state.
   - **Single input owner per wait & resume actual caller:**
     - *Visual VWF page advance & authored `<wait more>`/`<wait>`:*
       `RetroTextSession` consumes one fresh button edge and requires release
       before acting on the next page or menu. When a script's wait differs
       from the native printer wait (`00:3215` or `00:33CC`, including US/BR
       yes/no wait quirks), `RetroTextSession` owns the player press and lets
       the native printer complete without requesting a second press.
     - *`<end>` / end-before-prompt / close:* terminates immediately and
       resumes the **actual caller** (e.g., `00:2D19`, chest `00:28E1` ->
       `00:37A3` preserving answer at `00:28E4`, shop, hospital or story).
     - *Interruptible timed wait (`00:2D33`/`00:2D37`) and fixed delay
       (`00:2D47`/`00:2D49`):* keep original frame countdowns (fresh edge only
       for interruptible; ignored for fixed).
     - Never reuse the host Options hold (console Pause `00:0066` and Reset
       stay live); run normal frames so PSG and FM (`0C:8043`, `0C:82EF`) tick.
4. **State separation, `OptionsMenu`/`LanguageScreen` geometry & format 6:**
   - Separate playable ROM, host UI language, script variant and saved
     `interface_preference` from runtime `effective_interface` (SMS-forced
     `ENHANCED` never writes into `.options`). Apply live switches atomically
     at the verified return to `00:0B14` or `00:0F48`.
   - `OptionsMenu` tracks logical row + first-visible row for 8-row vertical
     scrolling, clamps the values popup inside 32 columns
     (`min(values_column, 32 - width)`), and budgets inline row values inside
     the 18 interior tiles (using concise inline labels such as `SMS EN` /
     `SMS LIT` in M21 while keeping full popup choices so `LANGUAGE` is never
     overwritten). Refit `LanguageScreen` vertical spacing so up to 5 choices,
     Save, Cancel and status fit inside 192 px.
   - `SaveStore` reads formats 1–6 (accepting `FILL` in 6, consuming format 4's
     retired switch, defaulting older records to `ORIGINAL`), emits 2/3/5
     whenever no retained record (`committed`, `pending` or `backup`) needs 6,
     and carries `interface_preference` through lazy load, `choose`, staging,
     backup, retry and recovery. On POSIX `publish_bytes`, distinguish failure
     *before* rename from uncertain durability *after* rename (reconciling live
     in-memory state with the visible file and reporting a durability warning).
   - Confirm 2 px vs. 1 px borders on the S24 in portrait during play-testing.

## Implementation parts

- **Part A — VWF font, layout primitives & helper strings (ROM-free):**
  font (curly quotes, ellipsis, PT-BR accents), anti-orphan wrapper, 2-line
  paginator, 2 px box/1 px divider renderer and EN/PT-BR spell/item helpers.
- **Part B0 (Gate) & Part B — `CleanBackground` gate, ROM readers &
  `RetroUiState`:** pass all 6 B0 gate cases first; add cartridge stat/MP/level
  readers (`00:1B87`, `00:17BA`, `00:2B84`, `00:26C8` table) and frame-observer
  `RetroUiState`.
- **Part C — `RetroTextSession`, `TextSource` & all game screens:** split
  `00:31CF`/`00:31D4` dispatch, wire all four text events, single-owner waits
  and all US/BR screens (native title, name entry and scripted credits).
- **Part D — Scrolling `OptionsMenu`, `LanguageScreen`, format 6 & docs:**
  8-row Options + inline budget + clamped popup, 5-choice `LanguageScreen`,
  `SaveStore` format 6 + POSIX rename/flush reconciliation, ON/FILL widescreen,
  Android/macOS builds and `docs/design.md` amendment.

## Verification and acceptance

- **Independent Tier A reviews:** plans approved; implementation review covers
  `CleanBackground`, `RetroTextSession` hooks/waits, `SaveStore` format 6 +
  POSIX durability reconciliation and original-mode passivity.
- **Automated suite:** complete Windows suite (0 skips); CI (Windows + macOS,
  no ROM) with `check_rom_skips.py`. Tests prove exact event counts + preserved
  PC/SP/mapper across direct, indexed, append and alternate re-entry; physical
  press counts and resulting choices/money/inventory/story flags at 1x and 4x
  (including chest `00:28E1`, held confirm, Pause `00:0066`, Reset, power cycle,
  hospital, church, Road Pass, `01:4765` tail-call); VWF/5-spell/boundary cases;
  scrolling Options + inline/popup geometry + 5-choice `LanguageScreen` on
  controller/Android; formats 1–6, 1.4.0 downgrade and post-rename flush retry.
  Enhanced ROM routes drive semantic events; enable `PSMODERN_PLAYTHROUGH_TESTS`
  if claiming playthrough coverage.
- **Maintainer play-test (Windows + Android S24):** shop, equip member choice,
  battle magic/target/2-line pagination, S24 portrait borders, ON/FILL and save
  compatibility with `ORIGINAL`.
