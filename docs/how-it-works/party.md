# How it works: the party's records

**ROM static.** The game keeps four character records, each sixteen bytes
long. They start at `$C400`, `$C410`, `$C420` and `$C430`. The routines at
`00:170D` and `00:17BA` visit them in that order. A record has space even
before its character joins the party.

**ROM static.** The code described here is identical at the same addresses
in the US Rev-A and Brazilian cartridges. This follows the code comparison
in [design, section 5.5](../design.md#55-languages). The runs described below
are US runs only.

## What a record holds

**ROM static.** The table describes the first record. The same layout
repeats in the other records. Each field occupies a byte, except experience,
which occupies two. The flag at the start decides whether the experience
and record-refresh routines process that record (`00:1754`, `00:17BA`).

**Community source.** HP, MP, attack, defense, equipment and magic names
below come from community notes. In those names, HP means health points
and MP means magic points. These meanings have not been independently
confirmed here. Knowing where a byte is copied does not confirm its name.

| First-record location | Part and evidence |
| --- | --- |
| `$C400` | **ROM static:** processing flags; the lowest bit enables the work described here |
| `$C401`, `$C402` | **Community source:** current HP and MP |
| `$C403`–`$C404` | **ROM static:** accumulated experience |
| `$C405` | **ROM static:** level |
| `$C406`, `$C407` | **Community source:** maximum HP and MP |
| `$C408`, `$C409` | **Community source:** attack and defense |
| `$C40A`, `$C40B`, `$C40C` | **Community source:** weapon, armor and shield identifiers |
| `$C40D` | **Unknown:** meaning; **ROM static:** cleared during record refresh |
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

**ROM static.** The battle reward is a two-byte amount at `$C2D0`.
At `00:170D`, a zero reward ends the award immediately. Otherwise the game
visits each record. At `00:1754`, it skips a record whose processing flag's
lowest bit is clear. Every record that passes receives the full reward.
The game does not divide that amount among the party.

**ROM static.** The game adds the reward to the record's accumulated
experience. If that addition goes beyond what the two-byte counter can
hold, it stores the counter's largest value. It then ends processing for
that record on this award, without checking for a level gain (`00:1754`).

**ROM static.** Otherwise, the game checks the level limit. A record already
at that limit keeps the added experience but gains no level. Below the
limit, the game compares accumulated experience with the next threshold.
Reaching or passing it raises the level once. It does not check again on
the same award, even if enough experience remains for another level
(`00:1754`). Experience is kept as a total; it is not spent on leveling.

**ROM static.** Each character has a separate set of level rows in the
player's ROM. The record's current level selects the row containing its
next experience threshold. Character and current level decide that
threshold, not the monster just defeated (`00:170D`, `00:1754`). After the
award, battle code calls `00:17BA` to refresh the records for their levels.

**ROM run.** In the six existing US recordings, the award ran after a
battle in first-battle and in deep-dungeon. A level increment was reached
in deep-dungeon. Those observations establish these cases; they do not
exercise every flag state, counter overflow or level-limit case.

## Shared possessions and party size

**ROM static.** Inventory and money belong to the party rather than to
individual records. The inventory has twenty-four byte-sized item slots
starting at `$C4C0`. Its separate count is at `$C4E2`. The addition code
at `00:279F` checks capacity before appending an item; the search at
`00:282E` scans that same slot area.

**ROM static.** Money occupies two bytes at `$C4E0`. The game reads that
amount for number display at `00:35D2`. The nearby inventory count is a
separate field, not part of the money counter.

**ROM static.** Party size is stored at `$C4F0` as one less than the number
of members. Recruitment writes this field at `01:4C5C`, `00:247E` and
`01:51AB`. The encounter code at `01:606C` adds one to recover the size.
Having four allocated records therefore does not mean all four characters
have joined.

## What changing experience gained would mean

**Inference (from the award rule).** Changing the reward before it is
given would change the amount each eligible record receives. The counter
limit, the overflow exit, the level limit and the single level check would
still matter. A larger award would not automatically grant every level
whose threshold it crosses.

**Unknown.** Where an optional experience adjustment should act, and how it
should interact with those limits, remains for milestone 4
([roadmap](../roadmap.md)). This chapter describes the original rule and
chooses no adjustment.
