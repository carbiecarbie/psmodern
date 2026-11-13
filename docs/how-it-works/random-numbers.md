# How it works: random numbers and encounters

**ROM static.** The game shares one random-number routine, entered at
`00:05B1`, across several systems. It keeps a two-byte state at `$C20C`.
Each call turns that state into the next one and hands back one byte, the
draw. The draw also depends on the processor's refresh counter R, which is
never saved into the state. The routine is the same at the same addresses
in the US Rev-A and Brazilian cartridges; the comparison in
[design, section 5.5](../design.md#55-languages) establishes that shared
code. It is the second game routine rewritten in C++, and the encounter
checks described below are rewritten around it.

## How the next state is made

**ROM static; ROM run.** Think of the state as a row of sixteen switches,
numbered 0 on the right to 15 on the left. Each call looks at four of them:
switches 15, 13, 4 and 0. If an odd number of those four are on, the new
switch is on; otherwise it is off. Then every switch moves one place to the
left, the leftmost one falls off, and the new switch enters at position 0.
The routine does this with a series of rotations and bit mixes of the two
bytes, then one addition of the state to itself; the result is the same. The
state tests ran this rule against the original for every one of the 65,536
possible states.

**ROM static; ROM run.** A state with every switch off would stay off
forever. The routine checks for that result and replaces it with a fixed
state taken from its own code at `00:05C5`. Only the all-off state leads
there: every other state has a successor with at least one switch on.

**ROM run.** In each of the four US recordings that draw numbers, the
replacement path ran exactly once; every other call took the usual path.

**Inference (from the rule above, which the tests checked for every
state).** The states do not form one long loop. They split into three
separate loops of 63,457, 2,047 and 31 states. The fixed replacement state
lies in the longest loop, so a game that starts from zero cycles through
63,457 states before repeating. This says how many different states follow
one another, not that the draws are equally likely or independent.

## The draw and the counter R

**ROM static.** After the new state is ready, the routine reads R at
`00:05C8` and mixes it, switch by switch, with the low byte of the new
state: each bit of the draw is on when exactly one of the two bits is on.
The draw goes back to the caller in the A register. The new state is stored
at `$C20C`; R is not part of it.

**Hardware documentation; ROM run.** R counts each instruction fetch in its
lower seven bits and keeps its eighth bit
([the counter R](master-system.md#the-counter-r)). By the time the routine
reads it, R has counted the 19 fetches since the call began on the usual
path, or 20 when the zero state was replaced, including both fetches of the
instruction that reads it. The draw therefore depends on how many
instructions the program fetched before calling, all the way back to the
start of the console.

**Inference (from the state update and R read).** The stored state alone
cannot predict the returned draw. Changing when the program reaches the read
can change the draw, and so later play
([why timing can change a draw](master-system.md#why-exact-timing-matters-for-this-game)).

**ROM static.** The routine borrows the HL register for its work. It saves
the caller's value on the stack first and restores it at the end, so the
caller gets back everything except A and the flags. The instruction that
reads R also copies an interrupt flag (IFF2) into one of the flags, but the
mixing step right after it overwrites all the flags, so the caller never
sees that copy.

## Keeping the original timing

**ROM static; ROM run.** A call takes 151 processor cycles and 23 opcode
fetches, including the return to the caller. When the zero state is
replaced, it takes 156 cycles and 24 fetches. The C++ version spends exactly
these cycles and fetches, reads and writes memory in the original order
(including the save and restore of HL on the stack), and leaves the return
itself to the original code on the emulated processor.

**Maintainer decision.** As for the controller, a call runs in C++ only when
no interrupt or frame boundary can fall inside its window; otherwise that
call stays on the Z80. The window is the exact length of the path the call
will take, worked out from the state before the call. See
[development](../development.md#registered-routines) for that rule.

**ROM run.** The random-number tests start from a real power-on machine of
each cartridge and run one call per probe against the original, comparing
the whole machine. They cover every state with both values of IFF2, every
value of R on both paths, R counting past 127 during the call with both
values of its eighth bit, other cartridge banks and the save chip in the
upper slot, and stacks in work RAM, its mirror, the cartridge, the save chip
and on the state itself. The twelve checkpoint recordings also check each
C++ call against an original call from the same state, and compare all
frames with routines off and with the reference emulator. The
[generated coverage](../coverage.md) records how often C++ runs.

## Who draws numbers

**ROM static.** The code inspected contains thirty-four places that call
`00:05B1` by its address. They serve encounter checks and enemy-group setup, battle
processing, interaction and object effects, and dungeon navigation.
Battle uses include choosing eligible record slots, varying arithmetic,
changing action order and selecting a response. They are not all damage
rolls. For example, the battle-order work at `00:18F2` repeatedly draws an
index and swaps entries in its order list.

**ROM run.** Twelve of those places ran on the six US routes. They
include the map and dungeon probability checks, encounter-pool selection,
enemy-group size and battle processing. The observed callers are only part
of the static inventory.

**Unknown.** A call can also choose its destination while the game runs.
A search for calls with a fixed destination would miss that kind of call.
The count does not prove that every possible route was found. Specific gameplay names
for several battle and object effects also remain unconfirmed. Neither the
code reading nor these runs establishes that draws are independent or
equally likely.

## The encounter check on the map

**ROM static.** An update on the map does not always draw a number. The
caller at `00:0B2A` requires the gate byte at `$C265` to be zero and the
pending-check byte at `$C2D2` to be nonzero. It clears the pending byte before
calling `01:5F63`. That routine rejects a map selector at `$C308` of three
or higher before any draw.

**ROM static.** At `01:5F63`, the current map selector and map position
choose a region record in the player's ROM. That record supplies a
threshold. If the byte at `$C30E` is nonzero, the game divides the threshold
by four, discarding any remainder. Otherwise it keeps the threshold as it
is. The draw at `01:5FAD` must be strictly below the resulting threshold.
An equal or higher draw fails. A zero threshold therefore accepts no draw.

**ROM static.** The record is chosen in two steps. The selector (0, 1 or 2)
picks one of three tables of 64 records, and the player's two map coordinates
pick the cell inside it. Each coordinate is a two-byte value: the game adds a
fixed offset to its low byte, adds any carry to its high byte and keeps three
bits, which gives a step from 0 to 7 along that axis. Three selectors times
eight steps times eight steps are the 192 records. Each record holds the
threshold and a second value that is used only after a pass.

**ROM static.** After a passing draw the check looks up a pool identifier:
the record's second value and a second value the game keeps select an entry in
a list of pool identifiers, in a block that depends on whether the selector is
zero. The shared monster-group choice (below) takes it from there.

**Unknown.** The precise player situation represented by `$C30E` is not
confirmed here. The reduction is established for any nonzero value, so it
must not be narrowed to an assumed vehicle or named effect.

## The encounter check in a dungeon

**ROM static.** The dungeon routine at `00:0F3C` is the dungeon's game-mode
update. It first waits while the game is paused, then waits for a frame
interrupt, calls the dungeon handler at `01:65EE` (it reads the controller and
walks the party; see [dungeons](dungeons.md)) and only then runs the encounter
check. Its path also requires the pending byte
at `$C2D2` to be nonzero, then clears it. It reads its threshold from
`$C2E3`. The draw at `00:0F58` must be strictly below that threshold. This
comparison uses the threshold directly.

**ROM static.** Even a passing draw must pass a local-space check, called
at `00:0F5F`. The helper at `01:6BCA` reads the kind of the cell directly
ahead of the party (the lowest three bits of its value). Only kind 0, open
floor, lets the encounter go on; a wall, stairs, a door or a fake wall ahead
stops the path before pool selection. A cell with the object marker (value 8)
counts as open floor here, because only the low three bits are read.

**ROM static.** The pending byte at `$C2D2` is set by the walking code, not by
the loop: every forward step, back step and turn sets it, and so does the object
check when it finds a record that is not done. So turns and back steps give a
chance of an encounter as well as forward steps. See
[dungeons](dungeons.md#how-random-encounters-hook-into-walking).

## Which monsters, and how many

**ROM static.** An encounter pool is a list of possible enemy entries.
After a passing map check, the region and further area selectors choose
a pool. The dungeon path instead supplies the
pool identifier at `$C2E4`. Both reach the shared selector at `01:5FD8`.
A zero pool identifier ends selection without another draw. Otherwise a
separate draw at `01:5FE9` chooses one of eight entries in that pool and
stores the selected enemy identifier at `$C2E6`.

**ROM static.** Enemy setup at `01:5FFE` clears its working records, then
checks the selected identifier. If its lower seven bits are zero, setup
returns without making an enemy group. The calling code still enters
battle processing, so this early return alone does not establish what the
player would see in that case.

**ROM static.** For an identifier that passes, the enemy's record supplies
the group-size rule. One form gives a fixed count and makes no count draw.
The other gives an upper bound. The game lowers that bound if twice the
party size is smaller. At `01:6075`, it keeps the lowest three bits of a
draw, retries if the result reaches or exceeds the bound, and adds one to
an accepted result to obtain the count. The fixed-count form bypasses both
this draw and the party-size bound.

**ROM static.** Battle processing has further record checks at `00:100F`.
It can take an exit path if no party record has its lowest processing bit
set, or if all the records' first current-value bytes are zero. These are
separate checks. The field names are discussed in [party records](party.md).
A passing probability comparison is therefore only one stage of starting
and continuing a battle.

**ROM run.** The recordings reached both probability-draw sites, the
pool-member draw and the group-size draw. That establishes their use on
these routes, not every region, pool, group-size form or later exit.

**Unknown.** The rejection loop has no escape for a zero group-size bound.
This study has not established whether valid enemy data can supply that
case. It also has not observed the empty-identifier case described above.

## The checks in C++

**ROM run.** The map check at `01:5F63` (with the shared group choice at
`01:5FD8` and the failure ending) and the dungeon update at `00:0F3C` now run
in C++. They cannot run as one piece, because they call original code that
waits for frames, shows text or starts battles. Each check is therefore cut
into pieces at those calls. A piece runs in C++ and ends by handing exactly
one original instruction (a return, or a call or jump into original code) to
the emulated Z80. The original code then runs untouched, the random-number
routine included, until control reaches the start of the next piece. Since the
processor core itself executes the last instruction of every piece, the
cycles, the refresh counter R, interrupt timing and the frame boundary come
out exactly as on the console (see
[development](../development.md#segments)).

**ROM run.** The map check has five pieces, at `01:5F63`, `01:5FB0`,
`01:5FB1`, `01:5FD8` and `01:5FEC`; the dungeon update has eight, at `00:0F3C`,
`00:0F43`, `00:0F4B`, `00:0F5B`, `00:0F5C`, `00:0F62`, `00:0F6D` and
`00:0F75`. Three bare instructions of the dungeon update (two calls and the
final return) hold nothing to rewrite and stay on the Z80. The group choice is
one C++ function, used both for the dungeon's call and for the map check's
fall-through into it. The failure ending is carried by each piece that can
reach it.

**ROM run.** Every piece renders the original instructions from its own
address for whatever state the machine is in. It reads registers, flags,
memory and the bank mapping from the machine and assumes nothing about the
piece that ran before. The tests start from a real power-on machine of each
cartridge and compare every piece with the original over the whole machine,
cycles and refresh count included. They cover all 192 region records with
every carry combination of the two coordinates and the reduction values, every
selector from 0 to 255, every pair of coordinate low bytes, every pool
identifier with every draw, every pair of draw and threshold at both
comparisons (65,536 each), and for the dungeon every pending byte with every
threshold, every flag byte at the conditional returns and every value of the
other bytes it reads. They also vary the bank in the upper slot (including the
save chip), the position of R in its seven-bit cycle and the place of the
stack, and run whole checks piece after piece against the original alone.
The twelve checkpoint recordings check each C++ call against an original call
from the same state; the [generated coverage](../coverage.md) records how
often C++ runs.

## The encounter-rate option

**Maintainer decision.** Optional features are off by default and never
change original mode ([design](../design.md)). **Maintainer decision
(2026-11-11).** An optional setting, the command-line flag `--encounter-rate`,
makes random encounters rarer or more frequent. It takes one of five whole
percentages, 0, 25, 50, 100 or 200, where 100, the default, is the original: the
threshold becomes the threshold times the percentage divided by 100, rounded
down and never above 255. On the map the reduction described above applies
first. At 0 no draw can pass; 25 and 50 take a quarter and a half of a threshold,
rounded down like every other value; 200 doubles it up to the cap.
**Maintainer decision (2026-11-13).** The recommended pace, that of the Switch
release's easy mode (a quarter of the encounters with four times the
experience and battle money), is encounters at 25 together with experience at
400; 25 was added to the list that day, and a saved 50 stays valid. The random draw is still consumed on every check,
whatever the percentage, and fixed and story fights are untouched. The
maintainer confirmed that the draw is consumed even at 0, so that the option
does not affect other things in the game.

**ROM run.** The option acts only in the two pieces that are a single
instruction, the comparison of the draw with the threshold at `01:5FB0` on
the map and at `00:0F5B` in dungeons. Before the processor core runs the
comparison, the register holding the threshold is given the scaled value;
afterwards it gets its original value back. The comparison itself is still
the original instruction executed by the core, it costs the same four cycles,
and the only difference from the original instruction is the flags it leaves,
which describe the scaled threshold. With the option at its default the piece
is the bare instruction and no adjustment code runs.

**ROM run.** The option leaves the draw alone. The random-number routine is
still called once per check and the random state still advances; only the
comparison's outcome changes. At 0 the random state advances once per map or
dungeon check and the check fails, on every region record and in the
dungeon. The gates before the draw (the pending bytes), the local-space check,
the pool choice and the group size are unchanged. A check that ends
differently from the original takes different time afterwards, as a failing
draw does against a passing one in the original, and later draws follow from
that.

**ROM run.** Because these two pieces add no time of their own, they are
admitted at every moment, also when an interrupt, a non-maskable interrupt or
a frame boundary falls inside the four cycles: the core executes the
comparison, so interrupts are taken exactly as for the original instruction.
The tests force those windows with the option on, and compare the machine with
the original instruction on the scaled value, also several steps later. With
the option off such a window still sends the call to the Z80.

**ROM run.** For the four allowed rates, 0, 50, 100 and 200, the
result of the comparison equals the formula above for all 65,536 pairs of draw
and threshold, nothing besides the flags differs from the original
instruction, and at 100 the piece is identical to it.

**Inference (from the strict comparison).** A draw of 255 never passes, even
when the scaled threshold has reached its cap of 255. At 200, a record whose
threshold is 128 or more reaches that cap, and every other draw passes.

**Inference (from these separate stages).** Changing a threshold changes only
which draws pass that stage; pool selection, group size and the other gates
still apply. The option keeps the draw for a reason: skipping a draw would
skip a state update, and moving it would change R, and either would reach
later random results as well as the number of battles.

**Unknown.** How the rates feel in play is a matter of playing; the tests
establish what the option does, not how it plays. The maintainer played the
build on 2026-11-11 and reported everything correct. Other ways of starting a
battle were not tested against it; the option sits only at the two
comparisons above, and this study found no other code that reaches them.
