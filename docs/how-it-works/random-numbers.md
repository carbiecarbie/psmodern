# How it works: random numbers and encounters

**ROM static.** The game shares one random-number routine, entered at
`00:05B1`, across several systems. It keeps a two-byte state at `$C20C`.
Each call shifts that state and mixes selected parts of its old value to
make the next state. If the result is zero, it substitutes a fixed recovery
value. At `00:05C8`, it mixes the new state's low byte with the processor's
refresh counter R to produce the returned draw. R is not saved into the
state. That address is an internal step of the same routine.

**ROM static.** The code described here is identical at the same addresses
in the US Rev-A and Brazilian cartridges. The comparison in
[design, section 5.5](../design.md#55-languages) establishes that shared code;
the runtime observations below are from the six existing US recordings.

## Why the moment of a draw matters

**Inference (from the state update and R read).** The stored state alone
cannot predict the returned draw. The value of R at the moment of the read
matters too. The console chapter explains
[what changes R](master-system.md#the-counter-r) and
[why timing can change a draw](master-system.md#why-exact-timing-matters-for-this-game).
Changing when the program reaches the read can therefore change later play.

**ROM run.** Both the normal state-update path and the zero-recovery path
ran in the existing US recordings. This does not test every possible state
or every value of R.

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

**Unknown.** The precise player situation represented by `$C30E` is not
confirmed here. The reduction is established for any nonzero value, so it
must not be narrowed to an assumed vehicle or named effect.

## The encounter check in a dungeon

**ROM static.** The dungeon path at `00:0F3C` also requires the pending byte
at `$C2D2` to be nonzero, then clears it. It reads its threshold from
`$C2E3`. The draw at `00:0F58` must be strictly below that threshold. This
comparison uses the threshold directly.

**ROM static.** Even a passing draw must pass a local-space check, called
at `00:0F5F`. The helper at `01:6BCA` uses position and direction to select
a nearby dungeon cell. The lowest three bits of that cell must all be
clear. A nonzero result stops the encounter path before pool selection.

**Unknown.** The cell check is established as a code rule. Calling it a
specific wall, door or corridor rule would need further evidence about the
dungeon data.

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

## What changing encounter frequency would mean

**Inference (from these separate stages).** Changing a probability
threshold would change which draws pass that stage. Pool selection,
group size and the other gates would still apply. Avoiding a draw would
also avoid a state update. Adding retries could consume more draws, and
changing timing could change R. Those choices could therefore affect later
random results as well as the number of battles.

**Unknown.** The place and method for an optional encounter-rate adjustment
remain for milestone 4 ([roadmap](../roadmap.md)). This chapter chooses
neither. The original
thresholds and encounter lists stay in the player's ROM.
