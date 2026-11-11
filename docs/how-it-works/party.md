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
the pace of the Switch release (together with encounters at 50): with the option off the C++ runs the same instructions
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
