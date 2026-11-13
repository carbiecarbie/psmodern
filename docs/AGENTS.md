# Documentation rules

Read this before creating or editing `README.md`, any `AGENTS.md` or anything
under `docs/`, including milestone plans and milestone closure. The root
`AGENTS.md` still applies, including its documentation-only check and its ban
on additional workflow documents.

All documentation is in English (maintainer decision, 2026-11-04). The one
exception is `README.pt-BR.md`, the Brazilian Portuguese translation of
`README.md` (maintainer decision, 2026-11-11).

## Documents and their responsibilities

| Document | Responsibility | Target size |
| --- | --- | --- |
| `README.md` | Public introduction; how to build, run and play | Short |
| `README.pt-BR.md` | Brazilian Portuguese translation of `README.md`; any change to one updates the other in the same change | Same as `README.md` |
| `docs/images/` | README screenshots taken from the running game (maintainer decision, 2026-11-11); no other images from the ROM | Few files |
| `docs/design.md` | The approved design: goals, architecture, decisions. Changes need the maintainer's approval | Stable |
| `docs/project-status.md` | What runs and can be done now; architecture overview; rewritten vs Z80 systems; known gaps | ~100-300 lines |
| `docs/milestone-N-plan.md` | That milestone's plan; condensed at closure | Closed: ~30-100 lines |
| `AGENTS.md`, `docs/AGENTS.md` | Agent instructions: every task; documentation | Root under 240 lines |
| `docs/dependencies.md` | Toolchain, libraries and versions, build setup (authoritative) | As needed |
| `docs/project-history.md` | One short entry per completed milestone, with its closing commit | ~5-10 lines per milestone |
| `docs/playtests.md` | How to prepare a maintainer play-test | Short |
| `docs/development.md` | Developer entry modes, ROM locations, tests, recordings | As needed |
| `tools/AGENTS.md` | Complete test-suite protocol | Short |
| `docs/how-it-works/` | Chapters for a reader who does not program, with evidence labels (below): `master-system.md` for the console, then one per game system | As needed |
| `docs/coverage.md` | Generated census of rewritten vs Z80 routines (`tools/generate_coverage.py`) | Generated |
| `docs/roadmap.md` | Next milestones and longer-term direction | Short |
| `docs/original-quirks.md` | Original oddities kept on purpose, US/Brazilian differences, quality-of-life candidates; one short entry each, with evidence and location | As needed |
| `docs/player-controls.md` | Every button and key for players: defaults, the Controls screen, the `controls.txt` file | As needed |

`docs/roadmap.md` orders the next milestones; `docs/design.md` section 8
gives the stages they belong to.

## Milestone plans

| | Tier A - core | Tier B - routines and features |
| --- | --- | --- |
| Plan | Short plan (~100-150 lines): goal, design decisions, risks, acceptance | Checklist: goal, tasks, acceptance criteria (~30-60 lines) |

Tier scope, reviews and acceptance are defined in the root `AGENTS.md`.

- Open each plan with a title line `# Milestone N plan - <name>` and a bold
  status line: tier, status and date.
- Record the maintainer's formal acceptance in the plan's status line
  ("Status: completed and accepted", with the relevant commits). Never record
  acceptance before the maintainer has accepted the milestone.
- At closure, condense the plan to scope, key decisions and results. Git
  keeps everything else; a closed plan is not a transcript.

## Writing rules

- Describe capabilities in player terms ("random encounters can be turned
  off"), not as lists of contract numbers or acceptance matrices.
- Update `project-status.md` only when a milestone closes, except for narrow
  factual corrections the maintainer explicitly authorizes. It is not a
  development diary.
- Do not record in-progress work, review transcripts, temporary review state
  or commit readiness in durable docs.
- Cite original code as `bank:address` of the US Rev-A ROM (for example
  `02:8A3C`), and name the Brazilian address too where it differs. Never paste
  ROM bytes, text, graphics or assembly into the docs; the annotated
  disassembly belongs to the private `psmodern-re` repository.
- `docs/how-it-works/` explains, it does not command: it is not authority
  over code or plans. A change that alters or settles a documented behavior
  updates the affected chapter and its evidence label in the same change.

## Evidence labels

Used in `docs/how-it-works/`, `docs/original-quirks.md` and plans.

| Label | What it establishes |
| --- | --- |
| ROM run | Observed by running the identified ROM on PSModern's validated Z80 path (trace, RAM, frame, sound); only the described case is established |
| Reference emulator | Observed by running the identified ROM in an established emulator (the patched MesenCE 2.2.1, with Emulicious as second opinion) |
| ROM static | A read-only reading of the identified ROM's code, with addresses; a code rule, not an observed execution |
| Hardware documentation | Published Master System hardware documentation; what the hardware does with the program's requests, not the program's own behavior |
| Community source | A disassembly comment, guide or other community material; a lead to confirm, never settled evidence alone |
| Inference | A conclusion from the evidence it names, at its stated confidence |
| Maintainer decision | A choice the maintainer approved; it sets PSModern's behavior and is not evidence about the original |
| Unknown | An explicit gap; do not turn a hypothesis into an implementation rule |

Keep labels honest: never upgrade "Community source", "Inference" or
"Unknown" without a ROM run, a reference emulator observation or ROM static
evidence.
