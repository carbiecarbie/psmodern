# Milestone 3 plan - Study base: the private disassembly and the first chapters

**Tier B. Status: planned and approved (2026-11-09).** Third stage of [design](design.md)
section 8. The game itself does not change in this milestone.

## Goal

The project gets a working study base: a private, data-free disassembly
that rebuilds the player's US ROM exactly, a catalog of what is known about
the code and the work RAM, and the first chapters explaining the game's
own systems. It prepares stage 4 (the encounter roll and experience gain),
then sets the roadmap.

## What the reconnaissance showed (ROM static, ROM run)

- The community disassembly by sung3r rebuilds the US Rev-A ROM byte for
  byte with WLA-Z80 10.5 / WLALINK 5.20 and names far more than the pscave
  one (about 730 meaningful labels against 14), including the sound driver.
- On the six US checkpoint recordings, code runs only in banks `00`, `01`
  and `0C`. Bank `0C` is the sound driver, already decoded as code by
  sung3r. The other banks executed nothing on these routes; that does not
  prove they hold no code.
- The upstream repository bundles the ROM itself, and its README forbids
  commercial use. WLA 10.7 rejects the source unchanged.
- The assembler can include byte ranges from the player's ROM at build
  time, keeping the build exact without storing data.

## Key decisions

- **Start from sung3r** (maintainer decision, 2026-11-09), pinned at commit
  `8efc4110`, imported as a fresh snapshot: no upstream history, no ROM, no
  data, no binaries. Its credits and terms are kept in the private README.
- **Private address catalog** (maintainer decision, 2026-11-09): routine
  and RAM names, addresses, evidence and confidence live in `psmodern-re`.
  This repository keeps citing original code as `bank:address` only; a
  private checker confirms each public citation exists in the catalog.
- **First chapters** (maintainer decision, 2026-11-09): random numbers, the
  party's records (including experience), and an overview of the sound
  driver.
- **No new recordings** (maintainer decision, 2026-11-09): routes the six
  recordings do not reach, such as the ending, stay outside measured
  coverage and are named as such.
- **Acceptance without a play-test** (maintainer decision, 2026-11-09):
  nothing playable changes, so the maintainer's session is reading the
  three chapters and the roadmap instead.
- **Independent check of the sanitized import** (maintainer decision,
  2026-11-09): a fresh reviewer confirms the private snapshot holds no ROM
  data before its first commit.

## Tasks

1. **A - Private repository.** Create `psmodern-re`; the private GitHub
   repository is created with the maintainer's authorization. Import the
   sanitized snapshot: every data and fill span becomes an include of that
   range from the player's ROM, labels preserved; fix the instruction that
   crosses `00:3FFF`. A build script checks the ROM's SHA-1, assembles with
   the pinned tools and compares the result. A payload checker fails if
   any tracked file contains the ROM, a run of ROM bytes or game text
   (comments included). The pinned tools are recorded by version and hash,
   not committed.
2. **B - Catalog and RAM map.** Define the catalog (routines and RAM fields:
   address, width, aliases, owner system, evidence label, confidence,
   read/write sites). Seed it with the controller (`00:0339`), the random
   number code (`00:05C8`) and its state, the sound driver's entry points
   and the party records. Keep the reconnaissance trace tools in the
   private repository; traces and dumps stay outside both repositories.
   Add the checker for public `bank:address` citations.
3. **C - Chapters.** In `docs/how-it-works/`: random numbers (who calls the
   random-number code, including the encounter roll where established),
   the party's records (stats, experience, how a level is gained where
   established) and the sound driver overview. Evidence labels throughout,
   unknowns stated; the Brazilian version is covered by its shared code.
   Names and comments go to the catalog in the same part.
4. **D - Design, roadmap, closure.** Correct design section 3 and stage 3
   with the reconnaissance facts (maintainer approval). Write
   `docs/roadmap.md` from the study base (maintainer approval). Close.

## Acceptance

- `psmodern-re` rebuilds the player's US ROM byte for byte from its own
  source and the player's ROM; its payload checker passes, and the
  independent check of the sanitized import finds no ROM data.
- The catalog holds the seeded entries with evidence labels, and every
  `bank:address` cited in this repository is in it.
- The three chapters follow `docs/AGENTS.md`: plain words, evidence labels,
  no ROM data; the documentation-only check passes for each change here.
- The maintainer reads the chapters and approves the design corrections
  and the roadmap.
