# How it works: the party's records

**ROM static.** The game keeps four character records, each sixteen bytes
long. They start at `$C400`, `$C410`, `$C420` and `$C430`. The routines at
`00:170D` and `00:17BA` visit them in that order. A record has space even
before its character joins the party.

**ROM static.** The code described here is identical at the same addresses
in the US Rev-A and Brazilian cartridges. This follows the code comparison
in [design, section 5.5](../design.md#55-languages). The experience runs described below
are US runs only; the recovery checks described at the end run on both cartridges.

## What a record holds

**ROM static.** The table describes the first record. The same layout
repeats in the other records. Each field occupies a byte, except experience,
which occupies two. The flag at the start decides whether the experience
and record-refresh routines process that record (`00:1754`, `00:17BA`).

**Community source.** Equipment and magic-count names below started with
community notes. Those meanings remain provisional.

**ROM static.** The recovery and damage paths now establish current HP,
maximum HP, current and maximum MP, attack and defense: HP is health, and MP is the
resource a spell spends. The counter at offset `$0D` blocks a character's
turn while it is nonzero. These roles are explained in
[the battle chapter](battle.md).

| First-record location | Part and evidence |
| --- | --- |
| `$C400` | **ROM static:** processing flags; the lowest bit enables the work described here |
| `$C401`, `$C402` | **ROM static:** current HP and MP; recovery uses these fields as described below |
| `$C403`–`$C404` | **ROM static:** accumulated experience |
| `$C405` | **ROM static:** level |
| `$C406` | **ROM static:** maximum HP, the recovery cap |
| `$C407` | **ROM static:** maximum MP, copied into current MP by revival |
| `$C408`, `$C409` | **ROM static:** attack and defense, used by the damage calculations |
| `$C40A`, `$C40B`, `$C40C` | **Community source:** weapon, armor and shield identifiers |
| `$C40D` | **ROM static:** turn-blocking counter; cleared during record refresh |
| `$C40E`, `$C40F` | **Community source:** counts of battle and map magic |

**ROM static.** When enabling a character record, `00:16F1` sets its
processing flag and starting level. It refreshes the record, then copies
the fields called maximum HP and MP into the fields called current HP and
MP. At `00:17BA`, refresh uses a row chosen by character and level. It copies
some values and adds adjustments selected by the fields called equipment.
It leaves the current-value pair alone.

**Unknown.** The full meaning of the processing flags, including all the
conditions they represent, remains open. The experience rule below is
therefore stated in terms of the flag, rather than assuming that it means
a character is alive.

## Giving experience after a battle

**ROM static.** The battle reward is a two-byte amount at `$C2D0`, worked
out when the battle is set up. The award at `00:170D` first copies it to the
place the message reads, so that the number on screen is the reward. A zero
reward ends the award there and then. Otherwise the game shows the reward
message and visits the four records in order (`00:171C` to `00:1754`).

**ROM static.** Who receives it: every record whose processing flag's lowest
bit is set (`00:1754`). Each of them receives the **full** reward. The game
does not divide it among the party, and the number in the message is the same
number every one of them gains.

**ROM static.** The reward is added to the record's accumulated experience, a
two-byte counter (`00:1773`). The counter has a ceiling of 65,535. If the sum
goes beyond it, the game stores the ceiling and ends the work for that record
on the spot, without looking at its level (`00:1774`). In the original, a
character whose total has reached the ceiling below the top level therefore
never gains another level, because every later award overflows again. This is
kept in original mode; see [original quirks](../original-quirks.md).

**ROM static.** Otherwise the game looks at the level. A record already at the
top level (30) keeps the added experience and gains nothing more
(`00:1774`). Below it, the game compares the total with the next threshold,
which a table in the ROM gives for each character and level. Reaching or
passing it raises the level by **one**. The comparison is not repeated on the
same award, so a very large reward still gives a single level, and the
surplus is not spent: experience is a running total, not a currency
(`00:1774`, `00:179D`).

**ROM static.** A gained level comes with the game's own messages and sound.
Just before the level is raised, the game asks for a short sound and shows the
level-up message (`00:1774`). After it, it raises the level and compares the
two magic counts of the new level's row with the ones the record holds (the
names are the [community's](#what-a-record-holds)); if either differs, a
second message follows (`00:179D`).

**ROM static.** Each character has a separate set of level rows in the
player's ROM. The record's current level selects the row containing its
next threshold. Character and current level decide that threshold, not the
monster just defeated (`00:1754`). After the award, battle code calls
`00:17BA` to refresh the records for their levels.

**ROM run.** The award and its per-character routine now run as C++
segments, checked against the original on every call. The text, the sound
and the waiting between messages stay on the original code, which resumes the
C++ at the next segment (`00:1710`, `00:171C`, `00:172A`, `00:1739`,
`00:1748`, `00:1754`, `00:1773`, `00:1774`, `00:179D`). State probes in both
versions cover every possible reward, every level and eligibility state of
the helper, every total around each level's threshold and every outcome of
the two magic comparisons, with the whole machine, cycle count included,
compared with the original. In the six existing US
recordings, the award ran after a battle in first-battle and in
deep-dungeon, and a level increment was reached in deep-dungeon; both runs
are identical with the C++ on and off. Those recordings do not exercise every
flag state, counter overflow or level-limit case; the probes do.

**ROM static, Inference.** The ceiling cannot be reached in the original game:
see the quirk entry for the evidence.

## Shared possessions and party size

**ROM static.** Inventory and money belong to the party rather than to
individual records. The inventory has twenty-four byte-sized item slots
starting at `$C4C0`. Its separate count is at `$C4E2`. The addition code
at `00:279F` checks capacity before appending an item; the search at
`00:282E` scans that same slot area.

**ROM static.** Money occupies two bytes at `$C4E0`. The game reads that
amount for number display at `00:35D2`. The nearby inventory count is a
separate field, not part of the money counter.

## Money after battle

**ROM static.** When a battle is prepared, `01:5FFE` works out its money
reward from the enemy and the number in the group. The result is kept in two
bytes at `$C2DD`. After the fight, the game can show the reward in a chest.
The chest and other ways of receiving money use the same addition at
`00:297A`: it adds the amount to the party's money, stopping at 65,535 if
the sum would be larger. The amount shown is the reward, which can be more
than the space remaining in the counter. Selling an item also uses this
addition; money found elsewhere and item sales are not battle rewards.

**ROM static.** Some scripted encounters deliberately replace the prepared
money with zero. A separate item may be present even when the money is zero.
The battle setup and addition code are the same in the US and Brazilian
cartridges. **ROM run:** both versions passed original-mode state probes for
the setup and money addition, including an empty reward and overflow. A
recorded chest with its reward raised to 50,000 or 65,535 displayed all five
digits in both versions, and the total stopped at 65,535.

**Maintainer decision.** Battle money follows the experience rate: 100, 200
or 400 percent. At 200 or 400, the game multiplies the prepared battle reward
and limits it to 65,535 before the chest displays and adds it. Zero stays
zero. The normal total still stops at 65,535. At 100 the original calculation,
timing and side effects are unchanged. This setting does not multiply money
from map chests or sales.

The setup is rewritten in segments around calls to the original graphics,
random and other helpers. A few stack, call and store instructions between
segments still run on the original processor. The money multiplier changes
the value passed through the original reward-store instruction; it does not
add a new memory write or change the original battle's multiplication.

**ROM static.** Party size is stored at `$C4F0` as one less than the number
of members. Recruitment writes this field at `01:4C5C`, `00:247E` and
`01:51AB`. The encounter code at `01:606C` adds one to recover the size.
Having four allocated records therefore does not mean all four characters
have joined.

## The experience-rate option

**Maintainer decision.** Optional features are off by default and never change
original mode ([design](../design.md)). **Maintainer decision (2026-11-11).**
The option multiplies the experience gained. The command-line flag
`--experience-rate` takes one of three whole percentages, 100, 200 or 400, and
100, the default, plays the game as the cartridge does; 400 is the value for
the recommended pace, that of the Switch release's easy mode (together with encounters at 25): with the option off the C++ runs the same instructions
and no adjustment code runs.

**What it changes.** The reward is scaled once, rounded down, kept to at
least 1 when the original reward is not zero, and at most 65,535. That one
scaled value is what the message shows and what every eligible character
gains, so the number on screen always equals each character's gain. The
reward stored in memory is never altered, so nothing is scaled twice if the
game reuses it.

**What it does not change.** The thresholds, the level table, the rule that
a record gains one level per award, the top level, who receives the reward,
the messages and the timing. The option runs at two single instructions that
the original Z80 still executes itself, so it adds no time: it works inside
interrupts and frame boundaries as well.

**The ceiling with the option on.** Maintainer decision (2026-11-11): when the
scaled reward overflows the counter, the total is still set to 65,535 as in the
original, but the game then carries on to the level test instead of leaving
early. A character below level 30 therefore still gains one level per award,
up to 30, even when large rewards push the total to the ceiling; at level 30
nothing changes. The maintainer confirmed one level per award, including for a
character already at the ceiling; several levels per award was declined.

**Five-digit rewards.** With the option a reward above 9,999 is possible (a stored reward of 12,500 at 400 shows 50,000). The
text code prints five digits in both versions: the US and the Brazilian
message boxes stayed intact and showed all five (ROM run: a recorded award
with the stored reward raised to a five-digit value, drawn by the original
text code, in each version; the pictures were inspected by eye).

**A saved game.** The option is not stored in a save. A save made with it on,
loaded later without it, holds totals the cartridge alone might not produce.

## Recovering health

**ROM static.** Two healing amounts are selected by the entries at
`00:1E27` and `00:1E2B`: 20 or 80 points. Those entries ask for a recipient;
`00:1E46` and `00:1E4A` use the recipient already selected. Cancelling the
choice or choosing someone whose record is inactive leaves the recovery
unperformed. The routine at `00:1B87` looks up a spell's cost in the player's
ROM and computes the caster's remaining MP. It reports insufficient points
to its caller without storing a new balance. On a successful healing path,
`00:1E53` stores that balance before the recovery message.

**ROM static.** The shared effect at `00:1E5F` displays the message, requests
the healing sound and adds the amount to the recipient's current HP. A sum
above 255 first becomes 255, then the result is capped at that character's
maximum HP. This establishes the roles of the record's offsets 1 and 6.
It can lower an already inconsistent HP value above the maximum; it does
not assume that saved state is consistent.

**ROM static.** The food entries at `00:2333` and `00:2337` supply 10 and 40
points to the same effect. In the field they ask who receives the food;
in battle they use the chosen character. The inventory removes the food
only after the target check and the shared recovery effect. Cancellation
or an inactive recipient leaves the item in the inventory.

**ROM run.** The recovery probes compare every replacement with the original
on both cartridges, including every HP byte and amount byte, all four caster
slots and every possible MP balance. Whole calls from an initialized town
state compare the original and C++ paths through messages, sounds and
inventory removal, including inactive recipients. These checks establish
those cases; they do not measure every recovery route in a complete game.

**ROM static.** Stat refresh (`00:17BA`) visits all four records. A record
whose activity bit 0 is clear is preserved. An active record resets flags to
1 and blocking to 0, copies maximum HP/MP and the two spell counts from its
eight-byte level row, and adds equipment boosts to attack and defense with
the original byte arithmetic. Current HP/MP, level, experience and equipment
are preserved. Actor lookup (`00:187A`, `00:187D`) preserves the actor number
and returns its record and bit-0 activity result. The warning helper
(`00:188E`) also preserves its caller's other registers while showing the
inactive-member message.

**ROM run.** Both cartridges pass 16,384 segment/activity probes, 1,920
complete refresh calls spanning all thirty levels and all sixty-four boost
table indices, and eight complete active/inactive warnings. Expected stats
are computed directly from ROM table rows and boost bytes, separately from
the rendered instruction sequence. Non-equippable table indices in these
prepared cases probe arithmetic; they are not claimed as ordinary equipment.
