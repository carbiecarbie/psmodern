# PSModern - Agent Instructions

PSModern is an open-source, faithful reimplementation of **Phantasy Star**
for the Sega Master System, in C++, running from the player's own ROM.

**The goal is a faithful reimplementation reached by gradual replacement**:
the original ROM runs on a virtual Z80 from the first milestone, and its
routines are rewritten in C++ one at a time, each verified against the
original, until no Z80 code is left. Original mode must behave, look, sound
and feel like the Master System cartridge - rules, formulas, random draws,
timing, input, interface and text - in both the US Rev-A and the Brazilian
(Tec Toy) versions. The approved design is [docs/design.md](docs/design.md).

The second goal is **understanding**: at the end, the maintainer must know
how the game works inside. A rewritten routine is not finished until its
workings are explained in plain words (see Documentation).

Optional features (adjustable experience and encounters, automap, widescreen
on the world map and in towns, a new interface, language selection,
controller support) are opt-in and off by default. They never change original
mode. Any other deviation from the original needs the maintainer's explicit
approval and is documented where it lives.

## Specialized instruction files

This file holds the rules for every task. Not every agent loads the files
below automatically, so read each one whose trigger applies before acting.

| File | Read it before |
| --- | --- |
| `docs/AGENTS.md` | Creating or editing `README.md`, any `AGENTS.md` or anything under `docs/`: design, milestone plans, milestone closure, project status, how-it-works chapters |
| `tools/AGENTS.md` | Running or delegating the complete CTest suite, or rerunning failures |

The play-test guide (`docs/playtests.md`) is added when first needed.

## Source of truth

- Repository: https://github.com/carbiecarbie/psmodern
- Actual Git state, the working tree and current code/tests establish
  implemented behavior. Inspect them before changing interfaces or relying on
  prose.
- `docs/design.md` sets direction; the active milestone plan sets scope.
  Older plans are background, not current authority.
- Agents without local access must use a verified commit SHA as their
  baseline and report when it cannot be retrieved; do not invent local state.

## Development principles

- **The original program is the authority.** Unlike most reimplementations,
  PSModern always has the exact original program at hand: the identified ROM
  running on the validated Z80 path. When a plan, prompt or code is unclear
  about a behavior, run the original and observe it instead of guessing. Ask
  the maintainer only when observation cannot settle a question, or when
  reproducing the behavior would leave the milestone scope. Never invent
  behavior the original does not have.
- **A divergence means the rewrite is wrong.** When the lockstep run shows a
  rewritten routine differing from the Z80 path, fix the rewrite. The only
  exception is a defect in the virtual hardware itself, and that must be
  shown against an established reference emulator first.
- **Timing is behavior.** Every rewritten routine accounts for the cycles the
  original would have spent, and the lockstep comparison checks them. A
  routine that matches the state but not the cycles is not done.
- **Both versions.** Where the Brazilian ROM's code differs from the US one,
  a rewritten routine is checked against both ROMs.
- **Rewrite whole routines.** Replace a routine at its entry point with the
  same inputs, outputs, side effects and cycle cost. Early rewrites may keep
  the original RAM layout and look "Z80-shaped"; cleaning them up is later,
  separately verified work.
- **Features are layered, never mixed in.** A new feature reads or adjusts
  the behavior of a rewritten routine only when its option is on. With every
  option off, a run must stay identical to the original, and a test proves
  it.
- **No new art.** Nothing may depend on drawing new graphics.
- **No ROM data or original code in this repository.** Never commit a ROM,
  raw data banks, text, graphics, maps or music extracted from one, or any
  disassembly, assembly listing or decompiler output. ROM data is read from
  the player's ROM at run time. C++ is written from understanding, not pasted
  from a listing.
- **The disassembly is private.** The annotated disassembly lives in the
  separate private repository `psmodern-re`
  (`D:\Projetos\Phantasy Star\psmodern-re`; design, section 6). Its names,
  comments and assembly stay there; this repository and its docs cite
  original code only as `bank:address` and describe what it does.
- **Analysis stays outside.** Emulator traces, RAM dumps, REA/Ghidra
  projects and other working material stay outside both repositories, under
  `D:\Projetos\Phantasy Star\`.

## Milestone workflow

Each milestone is classified in its plan as Tier A or Tier B. Acceptance for
both is tests plus a maintainer play-test. Plan formats are in
`docs/AGENTS.md`.
- **Tier A - core** (virtual hardware, the Z80 scaffold and the
  registered-routine mechanism, the verification harness, cycle accounting
  and timing, saves, the random number generator, architecture, removing the
  Z80): an independent review of the plan and of the implementation is
  mandatory. These scheduled reviews are part of the workflow and need no
  separate authorization.
- **Tier B - routines and features** (rewriting game routines verified by
  the lockstep run, how-it-works chapters, optional features, interface,
  controller): no mandatory independent review. If a significant risk
  appears, ask the maintainer to authorize one.
- Any other, unplanned mid-task review needs the maintainer's authorization.

Steps:

1. Read `docs/project-status.md` and the milestone plan; inspect relevant
   code and tests.
2. Implement within the milestone scope. Planning and implementation may be
   one task for Tier B. If the scope proves wrong, stop and say so instead of
   silently widening or narrowing it.
3. Add or update tests for changed behavior: unit tests for the virtual
   hardware and pure logic, and lockstep runs on input recordings for
   rewritten routines.
4. Build and run the complete test suite. Do not declare completion with
   failing tests.
5. The maintainer plays the result and accepts it. Only after that
   acceptance, record it in the plan's status line (see `docs/AGENTS.md`).

The design's order of stages does not authorize implementation; the
maintainer starts each milestone explicitly.

**Play-tests.** Maintainer play-time is the scarcest resource. Settle every
change first with lockstep runs, reference emulator comparisons, and frames
or audio the agent renders and inspects itself. Play-test only what those
cannot settle (input feel, how a feature plays, real devices), saying why for
each item. Hold one session per milestone, after its last part.

**Maintenance tasks.** Small fixes (a defect, a flaky test, a doc correction)
may be done without a milestone plan: a scoped prompt, tests for a code or
test fix, the complete suite if production code changes, and the mandatory
independent review when the fix touches Tier A areas. If a fix turns out to
be larger or recurring, stop and propose a milestone instead.

## Building and testing

- **Completion requirement.** A production-code change is never complete
  without a passing complete test suite.
- Tests that need a ROM must skip cleanly without one, so that CI (which has
  no ROM) runs everything else. The ROM-dependent tests run on the
  maintainer's machine; a green CI run never replaces them.
- **Documentation-only changes** (no production code or tests changed): check
  the diff and links and run `git diff --check`; do not build or run tests.
- Build directories, the toolchain and the suite protocol are set by the
  first milestone and documented in `docs/dependencies.md` and
  `docs/development.md`.

## Documentation

Do not create additional workflow or work-in-progress documents (progress
reports, temporary notes, redundant plans or duplicated instructions) beyond
the files listed in `docs/AGENTS.md`.

**How it works.** Rewriting a routine updates, in the same task, the matching
chapter in `docs/how-it-works/` here, written so a reader who does not
program can follow it, and its names and comments in the annotated
disassembly in `psmodern-re`. Each repository gets its own commit, each with
the maintainer's authorization.

**Original quirks.** When a task deliberately keeps an oddity of the original
(a bug kept on purpose, a difference between the US and Brazilian versions),
record it with evidence and location in `docs/original-quirks.md` in the same
change, even outside milestone scope.

## Git

- `main` is the stable branch. Prefer focused commits with clear messages.
- Never force-push or rewrite history. Do not commit build directories, ROMs,
  ROM data or disassembly.
- Do not commit, push or tag without the maintainer's explicit authorization.
- When two agents work at the same time, give each its own `git worktree` and
  build directory; at most one agent writes to a given checkout.
- After an authorized push, report branch, `git rev-parse HEAD`,
  `git rev-parse origin/main` and `git status --short`. If the tree is dirty
  or diverged before new work, stop and report it; do not repair it silently.

## Efficiency

Priorities: correctness, then credit use, then speed. Nearly all cost is the
conversation re-read on every model call.
- Fewer, larger tool calls: batch related reads and commands; never poll.
- Keep large output out of the conversation: send anything over ~100 lines
  (build, tests, disassembly, traces, dumps, diffs) to a file under the
  build directory and read targeted ranges. Do not re-read unchanged files.
- Keep context small: start each milestone part in a fresh session from the
  plan and the last report. Never stop a task because of context; compact and
  continue.
- Delegate mechanical, read-heavy work (disassembly sweeps, trace
  comparisons, inventories, the complete suite) to one lightweight subagent
  without conversation context that returns a short summary. The main agent
  keeps design decisions and code edits.
- Do not re-derive what the docs already settle; reference the plan instead
  of restating it; reports ~100 lines, detail in side files.

## Project language

English is canonical for identifiers, comments, diagnostics, command-line
output, tests, documentation and commit messages (maintainer decision,
2026-11-04). Chat reports and play-test guides for the maintainer are in
Portuguese. Original game text stays ROM-driven.
