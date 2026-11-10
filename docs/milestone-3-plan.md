# Milestone 3 plan - Study base: the private disassembly and the first chapters

**Tier B. Status: completed and accepted (2026-11-09).** Planned and
approved in `76ebada`; chapters in `24410ee`; private study repository
from `8994b57` to `3013c10`; closed by `1d4c6a1`, which condensed this
plan. Third stage of [design](design.md) section 8. The game itself did not
change.

## Scope delivered

- **Private study repository** (`psmodern-re`): a sanitized snapshot of the
  community disassembly by sung3r. Every data span is read from the
  player's ROM at build time, and the build is checked to be byte-identical
  to the US Rev-A ROM. A payload checker refuses ROM bytes, game text and
  binaries in any tracked file; an annotation tool admits comments and
  renames only when the build stays identical.
- **Catalog and first RAM map:** routines and work-RAM fields with evidence
  labels, confidence and access sites. Every RAM byte is listed; most stay
  Unknown. A checker confirms that each `bank:address` cited in this
  repository is cataloged.
- **Chapters:** [random numbers and encounters](how-it-works/random-numbers.md),
  [the party's records](how-it-works/party.md) and
  [the sound driver](how-it-works/sound.md).
- **[Roadmap](roadmap.md)** and the design corrections from the study.

## Key decisions (maintainer decisions, 2026-11-09)

- Start from the sung3r disassembly, imported fresh: no upstream history,
  no ROM, no data, its credits and terms kept.
- Names live in the private catalog; this repository cites addresses only.
- First chapters: random numbers, the party's records, the sound driver.
- No new recordings; unreached routes are named as such.
- Acceptance by reading the chapters and the roadmap instead of a
  play-test, since nothing playable changed.
- An independent check of the sanitized import before its first commit.

## Results

- **Code outside banks 0-1:** on the six US recordings only the sound
  driver in bank `0C` runs, and sung3r had already decoded it. The design's
  stage 3 and section 3 were corrected accordingly.
- **Encounters:** the map and dungeon checks each compare one draw with a
  threshold; later stages pick the monsters and their number. Found and
  explained, with the gates that can still stop a battle.
- **Experience:** every eligible character receives the whole reward, and
  at most one level is gained per award. Found and explained.
- **Party field names** such as HP and MP remain community names; their
  positions are established, their meanings not yet confirmed.
- **Checks:** the private build is byte-identical, the payload checker and
  its planted cases pass, an independent reviewer found no ROM data in the
  import, and every public citation is cataloged.
- **Acceptance:** the maintainer read the chapters and approved the roadmap
  and the design corrections on 2026-11-09.
