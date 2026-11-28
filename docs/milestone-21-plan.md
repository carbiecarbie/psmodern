# Milestone 21 plan - SMS Power! v2.6.1 English retranslations

**Tier A. Status: approved plan (2026-11-28); implementation not started.**
Twenty-first milestone of the [roadmap](roadmap.md); builds on
[milestone 20](milestone-20-plan.md)'s `RetroTextSession`, `TextSource` and
256x192 VWF interface.

## Goal and scope

Allow players who supply the official **Source code (zip)** of the
[SMS Power! Phantasy Star Retranslation v2.6.1](https://github.com/maxim-zhao/psrp/releases/tag/v2.6.1)
to play PSModern with **either** English variant inside that single archive:

- **English (SMS Power!):** localized script edited by Frank Cifaldi (`en`).
- **English (SMS Literal):** literal translation variant (`literal`).

- **Zero third-party text in Git or releases:** no scripts, tables, caches or
  translated golden screenshots enter the repository or packages, preserving
  `GPL-3.0-or-later` cleanliness and respecting `psrp`'s disclaimer.
- **Text only; saves and original mode untouched:** gameplay rules remain
  PSModern's; `phantasy-star.sav` stays 100% cross-compatible across US, BR and
  both SMS variants; recording mode starts with its native source even when a
  pack is installed.
- **Works on US-only, BR-only or dual-ROM installations:** selecting an SMS
  pack keeps the active playable cartridge, sets host UI labels to English and
  derives `ENHANCED` at runtime without altering the saved `INTERFACE` choice.

## Key design decisions

1. **Bounded, recoverable ZIP importer (`miniz` MIT, reader-only):**
   - Vendor a pinned, reader-only `miniz` (MIT) under `third_party/miniz/` for
     Windows, macOS and Android. Ship its license notice in
     `tools/package-release.ps1`, `tools/package-macos.sh`,
     `android/build.gradle`, and **`tools/package-android-release.ps1`**
     (adding `third_party\miniz` to its `-UseWorkingTree` copy list and its
     notice to the APK required-entries and `THIRD-PARTY` audit).
   - Build a dedicated ZIP picker/importer (`TranslationImport`) separate from
     the 512 KiB ROM picker (`PSModernActivity.java` currently returns 0 bytes
     for `.zip`). Import runs from the launcher (or a safe non-playing screen)
     so it never blocks Android's game SDL thread or background save flush;
     handle cancellation, late results and activity destruction cleanly.
   - Beside the picker, offer a platform `open_url` button pointing to the
     fixed official `v2.6.1` GitHub release URL (Windows, macOS, Android).
   - **Explicit numeric bounds** sized for the real 40,189,452-byte / 870-entry
     v2.6.1 archive: archive stream <= 64 MiB (counted directly on Android
     content-provider reads even when reported size is absent), directory
     entries <= 2,000, single allowlisted inflated member <= 4 MiB, total
     allowlisted inflated bytes <= 8 MiB, and **parsed output budget <= 1 MiB
     total across both prepared variants (<= 600 script records per variant,
     <= 64 typed tokens per entry)**. Stream into private staging with lock;
     reject duplicate/ambiguous paths, path traversal (`..`, absolute,
     symlinks) and inconsistent metadata.
   - **Pinned member SHA-256 digests:** validate actual inflated sizes and
     SHA-256 digests of the required allowlisted v2.6.1 members (including each
     variant's `plurals.asm`). Validate both `en` and `literal` in memory
     before atomically publishing; preserve any prior valid import on failure.
2. **Exact parser contract for `en` and `literal`:**
   - In `v2.6.1`, `psrp/literal/{script,menus}.yaml` are two-line placeholders;
     literal text lives in shared `psrp/{script,menus}.yaml`. Exact allowlisted
     member manifest and merge/precedence rules:
     - Scripts: base `psrp/script.yaml`, merged with `psrp/en/script.yaml` by
       `offsets` for `en` (applying upstream's nonempty-`offsets` active rule).
     - Menus: base `psrp/menus.yaml`, merged with `psrp/en/menus.yaml` by
       `name` for `en`, applying upstream precedence **selected language first,
       then `all`**; decode tile-layout characters before glyph coverage checks.
     - Data lists/grammar: parse supported ASM declarations strictly as data
       from `psrp/{en,literal}/{lists,articles,pronouns,plurals}.asm` (handling
       item/enemy sentinels and list article markers separately from script
       tags).
   - **Complete typed token set:** `<end>`, `<player>`, `<item>`, `<number>`,
     `<monster>`, `<article>`, `<Article>`, `<s>` (plural rule controlled by
     preceding `<number>` for 0 and > 1, cross-checked against `plurals.asm`),
     pronouns, wrap hints, `<delay>`, `<line>`, `<wait>`, `<wait more>`. Fail
     load loudly on any unknown token or unsupported glyph (relying on M20's
     VWF font including curly quotes `‘ ’ “ ”`).
   - Verify spell pixel fit on each variant's own rows (`Power Boost`, `Magic
     Waller`, `Super Heal` in `en`; `Powma`, `Muwarla`, etc. in `literal`).
3. **Two independently verifiable text inventories & caller-faithful waits:**
   - `01:575A` is lookup code over the cartridge's 341-slot pointer table, while
     direct messages bypass that table and upstream has 459 active records
     including grouped references and upstream-only patch prompts (e.g., save
     deletion).
   - Maintain and verify **(a)** a disposition for all 459 active upstream
     records (`indexed`, `direct`, `alternate`, `shared`, `split`, or
     `upstream-only` deliberately unreachable with an individual reason —
     Maintainer decision, 2026-11-28) and **(b)** a verified `TextSource`
     binding for every reachable in-scope US and BR cartridge text event.
   - Prove representative bindings before building importer UI; keep
     upstream-only prompts out of cartridge gameplay. Bind `<wait more>`,
     `<wait>` and `<end>` to M20's single-owner wait table so every message
     resumes its actual caller (including chest `00:28E1` -> `00:37A3`) with
     exact physical press counts on US and BR.
4. **Cold-start publication, live switching, `language.txt` and ending credits:**
   - **Cold start (before frame zero):** both launcher Play and direct-ROM CLI
     resolve the playable ROM, validate any saved pack and install `TextSource`
     plus runtime `effective_interface` before frame zero. Keep the guarded
     return to `00:0B14`/`00:0F48` exclusively for live in-game switches.
   - **Options & `LanguageScreen`:** expose `SMS POWER!` and `SMS LITERAL` in
     the popup (with concise inline row labels `SMS EN` / `SMS LIT` inside
     Options' 18-tile budget) and in M20's 5-choice `LanguageScreen`.
   - **`language.txt` & POSIX durability:** write version 1 for
     `native`/`en`/`pt-br`, version 2 only for SMS choices (1.4.0 warns and
     preserves v2); apply M20's post-rename flush reconciliation to `language.txt`.
   - **Missing/corrupt pack at launch (Maintainer decision, 2026-11-28):**
     start in the available cartridge's native language, show one localized
     notice, retain the saved SMS preference for reimport and keep the player's
     saved `INTERFACE` choice.
   - **Ending attribution:** keep the native SEGA credits sequence intact
     through its transition from `01:4668` to `01:4676`, and display a brief
     host translator-credits card **after** that native sequence completes (in
     addition to READMEs and the translation info screen), crediting each
     contributor by upstream role (`SMS Power!`, `Maxim`, `Z80 Gaiden`, `Paul
     Jensen`, `Satsu`, `Popfan`, and `Frank Cifaldi` for `en`).
5. **Design amendment (`docs/design.md`):** update sections 5.5 and 7 for
   player-supplied external translation archives beside the three ROMs.

## Implementation parts

- **Part A — `miniz`, `psrp` v2.6.1 parser & two-inventory bindings (before UI):**
  vendor reader-only `miniz`, enforce all numeric/stream/parsed bounds + pinned
  member SHA-256 digests, implement merge/precedence/token/plural parsers for
  `en` and `literal`, and prove representative bindings + 459-record disposition.
- **Part B — `TranslationImport` & platform `open_url` (Windows/macOS/Android):**
  dedicated ZIP picker (fixing Android `.zip` and lifecycle/stream caps),
  atomic staging/storage, fixed release URL helper and credits screen.
- **Part C — Cold-start & live `FrameDriver` wiring:** pre-frame-zero pack
  publication (launcher + CLI), live `00:0B14`/`00:0F48` switch, BR-only and
  missing-pack recovery, `language.txt` v1/v2 + POSIX durability reconciliation,
  and post-`01:4676` ending attribution.
- **Part D — Packaging hygiene, docs & acceptance:** `miniz` notices in all
  packagers including `tools/package-android-release.ps1` (`-UseWorkingTree` +
  APK audit), package cleanliness checks, READMEs, `docs/design.md` and tests.

## Verification and acceptance

- **Independent Tier A reviews:** plans approved; implementation review covers
  ZIP/parsed bounds, parser/merge/plural rules, two-inventory bindings,
  cold-start + BR-only state separation, ending transition and package hygiene.
- **Automated suite:** complete Windows suite (0 skips); public CI (Windows +
  macOS, no ROM, synthetic ZIP/text fixtures only) passes; local tests verify
  the real v2.6.1 ZIP (`en` and `literal`), every variant's spell fit + 5-spell
  height, physical press counts on US/BR (`<wait more>`, `<wait>`, `<end>`),
  cold-start BR-only (launcher + CLI), missing-pack notice, post-rename flush
  retry, recording passivity, `01:4668`->`01:4676` credits transition, save
  round-trips and clean packages (both git-archive and `-UseWorkingTree` APK).
  Record macOS picker/link smoke test separately from macOS CI build.
- **Maintainer play-test (Windows + Android S24):** direct link + ZIP picker
  import, opening/shops/battle/save-continue in both **SMS Power!** and
  **SMS Literal**, BR-only + missing-pack recovery and ending credits.
