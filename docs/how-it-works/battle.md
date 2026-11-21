# How it works: battles, magic and items

**ROM static.** A battle collects a command for each active character before
executing the actions. The command handlers described here are separate from
the turn scheduler and damage effects. The same code and addresses serve the
US Rev-A and Brazilian cartridges (see [design](../design.md#55-languages)).

**Since milestone 14.** The Z80 is no longer in the program. Where this chapter says that a
stretch stays on the Z80, is left to the original code, runs on the emulated processor, or falls
back when an interrupt or a frame boundary is near, it describes the milestone the text was
written for. Today every such instruction is rendered C++, an interrupt is accepted at the exact
instruction even inside a piece, and a state a piece cannot render stops the game with a message
([interrupts](interrupts.md), [development](../development.md#registered-routines)).

## Timed battle sprites

**ROM static.** An attack object selects a six-byte record at `01:5B87`.
It requests the record's sound, installs its frame delay and frame range,
copies the attack coordinates and uploads the graphics from the cartridge.
The related object at `01:5E2E` chooses between two records using one flag
and takes its screen coordinates from a different pair of work-RAM bytes.

**ROM run.** The signed timer advances the frame when its decremented value
has the high bit set. On the final frame the object clears its active byte
and discards the intermediate return address. The second object also
selects a flash or sound using the pending effect state. Both cartridges
are checked across all 19 attack records, both alternate records and timer
and frame values on either side of wraparound.

**ROM static.** The shared frame reader at `01:5DB1` follows the object's
cartridge sequence. The multi-phase effect at `01:5EEA` uploads its graphics
and then calls the same reader. The active target and pending effect decide
whether to flash the target or play the miss sound.

**ROM run.** The treasure object at `01:5C63` compares a random byte with
the threshold taken from the enemy descriptor. Zero always shows frame 3Dh.
For values 1 through EFh the alternative frame appears when the draw is at
most the threshold. Values F0h through FFh always select the alternative.
An even threshold selects 3Eh and an odd one selects 43h. Both ROMs are
checked for all 65,536 threshold/draw combinations. The object's later
timer and status bits control when that frame is revealed.

## Rounds and turn order

**ROM static.** Starting a battle (`00:100F`) selects the appropriate music
and writes the twelve actor IDs into a work table: four party slots and eight
enemy slots. At the beginning of each command round (`00:1055`), the sixteen
action-record bytes are cleared. The game skips a party member who is inactive,
has zero HP or carries the blocking status, then asks the eligible members for
their commands. Cancelling backs up to the previous eligible member.

**ROM static.** Once commands are collected, `00:18F2` shuffles the twelve
actor slots. For each current slot it draws a low nibble until the value is
below twelve and swaps that slot with the chosen one. It performs twelve
such swaps. This preserves the cartridge's particular order distribution;
it is not replaced with a different shuffle algorithm.

**ROM static.** The execution phase (`00:10D1`) visits that table in order.
IDs below four go to the party executor and the other eight to the enemy
executor. After each action it checks the eight enemy HP fields and the four
party activity fields. With no enemy HP left it takes the victory path;
with no active party it takes defeat. An escape result leaves the phase;
otherwise the next actor runs, and a finished twelve-slot table begins the
next command round. The loop position and count remain on the stack across
the original action calls.

**ROM run.** Both cartridges pass state probes of every round segment,
including command clearing, actor eligibility, cancellation and post-action
outcomes. Complete shuffle calls preserve all twelve IDs and match the
original's random state, registers, memory and elapsed cycles. This does not
by itself establish every enemy or party action effect.

## Remembering the choice

**ROM static.** The routine at `00:1BB9` writes three bytes into that
character's action record, at a four-byte stride. They describe the command,
its item or spell number, and its recipient. The fourth byte is untouched.
`00:1BCE` reads those three fields for the later execution stage. The attack
choice (`00:191C`) records the ordinary attack and advances the character
index. Item selection (`00:1B9E`) either cancels with that index unchanged or
queues the chosen item, then advances it. Queuing an item does not consume
it; the item effect decides that later.

## Magic and recipients

**ROM static.** The magic choice at `00:19DE` rejects the third party member,
who has no magic command. For the others it reads the number of known battle
spells from the character record, draws the list through the existing window
routines, and reads the selected spell from a table in the player's ROM.
The fourth character uses the third display row. A cancellation closes the
list without queuing an action. The [MP lookup](party.md#recovering-health)
checks the cost without spending it yet; insufficient MP produces a message.

**ROM static.** The spell's command table decides whether to ask for a
recipient (`00:1AB1`, `00:1ADE`), queue an action without a recipient
(`00:1AD0`), or use the current recipient (`00:1AFD`). A cancelled recipient
choice or a recipient rejected by the active-character check leaves the
actor on the same command. A successful queue advances to the next actor.
These are command choices, not the spell effects themselves.

## Talking and running

**ROM static.** Talking (`00:192B`) first checks a flag of the encounter and
compares the first party member's attack against the first enemy's attack. A rejected attempt
ends the command phase with its failure flag. An accepted one displays a
reply chosen from a ROM table. Random values outside that table's range are
rejected and another draw is made; a reply is never chosen by reducing the
out-of-range value modulo the table's length.

**ROM static.** The communication spell entries at `00:1B0D` and `00:1B31`
spend MP and then share the talk outcomes, with their respective permission
checks. The first variant also compares the fixed attack fields. Their improved reply table also uses rejection draws.
The improved-message branch begins at `00:1B43` in the verified listing.

**ROM static.** Running (`00:199C`) compares one random draw against the
encounter's escape threshold. A passing draw can still be refused by the
local-space check in the relevant interaction mode. Success requests the
escape sound and sets the escape result; failure names the actor, displays
the refusal and marks the command phase failed. These steps preserve the
original number and order of random draws.

**ROM run.** On both cartridges, state probes compare every new command
segment against the original, including cancellations, permission flags,
threshold outcomes, stack words and random-table rejection. Complete command
calls begin with an initialized town frame and compare the original and
replacement machines, including ordered sound writes and their cycles.
Action-record probes verify all four character slots and that the fourth
record byte is preserved. These probes establish the cases they exercise;
they do not establish coverage of every battle in the game.

## Offensive magic and group weapons

**ROM static.** The effect at `00:1EA7` spends MP once and applies its shared
strike twice: after calling the strike helper, execution falls directly into
that helper again. The effect at `00:1F25` spends MP once and applies the helper
three times. Neither effect charges separately for each hit.

**ROM static.** The shared strike (`00:1EB2`) first checks whether any of the
eight enemy records is active. With none it returns without a target draw.
Otherwise it shows the effect and draws an enemy index until an active one
is selected. It draws again for the small damage variation. The two-hit
variant subtracts 7 to 10 HP per hit and the three-hit variant 9 to 12. A
later hit repeats the active-target search, so an enemy eliminated by an
earlier hit cannot be selected again as active.

**ROM static.** The group effect at `00:1EE6` spends MP once, then visits
enemy records in their fixed order. Its shared entry `00:1EEE` is also used
by special weapons, which do not spend MP through this entry. Inactive
records are skipped. The group spell subtracts 25 to 40 HP from each active
recipient; the shared weapon path uses the amount supplied by its caller.
Encounter ID `$49` stops the group effect after the first active recipient.
The original damage, animation and window routines still perform their
own work; these effect segments select their inputs and preserve every
random draw and its order.


**ROM run.** Both cartridges pass probes of all twenty offensive segments
and thirty-two complete effects from an initialized battle frame. Those
calls cover absent and sparse targets, a full enemy group, both strike
counts, the group-stop exception and a fixed-damage weapon. They match the
original's resource changes, random state, elapsed cycles and ordered sound
writes. The effect-selection and damage-variation rules above are therefore
checked in execution as well as read from the ROM.

## Physical damage and death

**ROM static.** The party's physical attack (`00:121D`) reads the attacker's
attack and the recipient's defense. Each is varied by `00:1279`: it takes
half the stat, a quarter of it, and a random part smaller than that quarter,
with the cartridge's integer rounding. For example a stat of 40 produces 30
to 39. Attack-boost flag 7 increases attack by half, capped at 255, before
that variation. The enemy's physical path uses the same stat variation.
This establishes offsets 8 and 9 as attack and defense in the battle records.

**ROM static.** When the varied attack beats the varied defense, their
difference becomes damage. Otherwise the parity check can report a miss
or use a small level-bounded hit: draw 0 to 31 until the value is no greater
than the relevant level, then deal one point more than that draw. Rejected
draws are real calls to the game's random-number routine.

**ROM static.** Applying damage (`00:125E`) first requests the sound and
waits through the battle flash, preserving the amount on the stack. It
subtracts that amount from current HP and clamps an exhausted result to
zero. Zero HP clears both the record's activity flags and its turn-blocking
counter; a survivor retains them. The same sixteen-byte record layout is
used by the eight enemy slots for these fields.

**ROM static.** Effect and enemy animations (`00:18A9`, `00:18CE`) keep their
frame waits and call the object engine once per update. Their activity
markers decide whether to wait again; finishing also waits for the final
frame before restoring the saved index register. These helpers do not
replace the battle object types themselves.

**ROM run.** Both cartridges pass every byte combination of HP and encoded
damage, every byte combination of level and draw, all attack-boost values,
and complete stat-variation calls checked against an independent arithmetic
formula using the original draw. Per-call checks include the entire machine,
cycles and ordered port events.


## Protection, status and escape magic

**ROM static.** The protection entries (`00:1E8A`, `00:1E8E`) spend the ROM's
spell cost and initialize a shared protection field to six, with a high
flag distinguishing the second variant. Enemy actions consume and interpret
that field as attacks proceed.

**ROM static.** The blocking effect (`00:1F36`) needs the encounter's
permission flag. A favorable comparison of the first party member's attack
against the first enemy's attack sets its counter to three. Otherwise one
draw's low two bits give zero to three: zero fails. The first active enemy
whose blocking counter is zero receives the new counter. The attack-boost
effect (`00:1FC0`) sets flag 7 of its active recipient's record.

**ROM static.** Weakening (`00:1FDF`) uses the same fixed attack fields for
its strength gate, then requires a draw below 178. It scans for the first
active enemy without flag 6 and sets that flag. The enemy physical path uses
flag 6 to reduce its attack by a quarter before the stat variation.
These checks use the first records, even when someone else casts or another
enemy will receive the effect (see [original quirks](../original-quirks.md)).

**ROM static.** The escape spell (`00:1F80`) spends MP, then shares the gate
at `00:1F89` with item effects. It needs a nonzero encounter escape field and,
in the relevant interaction mode, the local-space check. It makes no escape
random draw of its own. Success plays its sound and message, then sets the
battle result to escape; a refusal chooses a different message for an item.
The item caller decides whether its item is consumed.

## Revival

**ROM static.** Revival (`00:20BF`) spends MP before asking for a recipient.
Cancelling therefore leaves the recipient unchanged but keeps the MP debit.
An already active recipient is refused after that same debit. An inactive
recipient's activity byte becomes one, and its maximum HP and maximum MP
are copied into current HP and MP. The latter copy establishes offset 7 as
maximum MP. Other flags in the old activity byte are cleared by the write.

**ROM run.** Both cartridges pass the strength gates for every pair of byte
attack values while the caster's stat differs, every revival HP value with
four MP edge values in each party slot, and all draw and boost-flag bytes.
Eighteen complete effects from a battle frame cover permission failures,
already affected enemies, active and inactive recipients, successful and
refused escape, item entry without an MP debit, and revival cancellation.
They match the original's state, cycles and ordered sound writes; scenarios
read the actual spell IDs and costs from the player's ROM tables.

## Magic on the field

**ROM static.** Field effects use the same nineteen-entry MP cost table as
battle effects. Their conditions follow the place the party is in, and the
menu acts on a requested follow-up after the effect returns.

**ROM static.** Trap removal (`00:201C`) distinguishes a chest from a dungeon
cell. A chest's trap field is set to the safe value, then the existing chest
opening continues. For a cell, the dungeon helper distinguishes a pitfall
from a scripted object: only an unassigned pitfall cell becomes ordinary
floor. Inspection (`00:20F8`) reports the corresponding condition without
clearing a pitfall; for a chest it then hands back to the ordinary chest
question. Shared item entry `00:2102` skips the spell's MP debit.

**ROM static.** Dungeon exit (`00:2064`) requires the dungeon interaction
mode; it requests the map-loading mode and ends the field menu. The shared
item path starts at `00:2078`. The door effect (`00:2091`) requires an
unopened magic-door cell ahead and leaves the door-opening request for the
existing menu and dungeon code; it does not open the cell itself.

**ROM static.** The transfer effect (`00:2140`) requires a non-dungeon
interaction and asks the menu to apply its location's transfer record. Its
shared item path starts at `00:2159`. Shared entries preserve the caller's
choice of resource consumption: they do not subtract an additional spell
cost on top of an item effect.

**ROM run.** Both cartridges pass every dungeon-cell byte for the door gate,
every cell index for the pitfall helper result, and all field-effect
segments. Seventeen complete effect chains start from an initialized dungeon
frame with documented state edits: valid and invalid terrain, safe and trapped
chests, exit and transfer conditions, and all three shared item entry paths.
The entire machine, elapsed cycles and ordered sound output match original
execution. These are probes of those prepared cases, not new input recordings.

**ROM static, ROM run.** All fifty-five slots in the battle command, battle
effect and field-effect spell tables are accounted for on both cartridges.
Fifty-two slots point to registered effect families. Three slots point to
two explicit self-loop placeholders (`00:1AAE`, `00:1E24`), registered as C++
joining segments since milestone 14.

## Executing a party action

**ROM static.** The party executor (`00:1148`) refuses inactive, dead or
cancelled actors. A blocked actor loses one or two counter units and displays
a recovery or still-blocked message instead of acting. Otherwise the queued
record selects a normal attack, spell or inventory item. Normal attacks draw
until they select a living enemy. Weapon IDs 09h, 0Bh and 0Dh use the shared
group effect with fixed damage 5, 10 and 20. Spells preserve the queued caster;
items are looked up again at execution time, so an item consumed by an earlier
actor produces the missing-item message.

**ROM run.** Both cartridges compare 15,360 segment states with the original
CPU. Complete action chains exercise inactive and dead actors, cancellation,
two blocking states, normal and three group weapons, queued magic, missing
items, an empty action and a carried food item. These prepared battle states
use the existing first-battle recording; they are not new recordings.

**ROM static.** Incoming physical damage (`00:1573`) first checks the
protection field. Enemy attack applies the saturating flag-7 boost before
the flag-6 weakening (subtract one quarter). Attack and party defense then
use the shared variance helper. The minimum-damage fallback draws until its
low five bits do not exceed the target's level and deals half that draw plus
one. Exhausted HP clears activity and blocking status; `C2ED` reports a hit
or miss to the caller.

**ROM run.** Incoming damage adds every HP/delta and level/draw combination,
all attack-byte/boost/weaken combinations, and 128 complete native comparisons
with protection and hit/miss cases. The damage family totals 277,632 cases
per cartridge, including the earlier outgoing-damage and variance cases.

## Enemy action control

**ROM static.** Enemy actions (`00:128C`) check activity and blocking before
dispatching from the low three metadata bits. A blocked enemy loses one or
two units instead of acting. Ordinary attacks choose a living party target,
preserve that target across animations, and report protection, miss or death.
If enemy 46h kills party actor 1, the aftermath clears all four party activity
bytes; the additional message appears only when at least one was nonzero.

**ROM static.** Protection consumption (`00:12D4`) requires metadata bit 4.
It subtracts a draw in 0..3 from the low seven units and retains the high
variant bit only while units remain. Expiry clears the entire byte.

**ROM run.** Both cartridges compare 82,944 states: all counter/draw pairs,
all enemy-ID/actor pairs for the special death aftermath, and all segments.
Eight complete physical-action chains cover inactivity, two blocking states,
a likely miss, low and high protection variants, expiry and a lethal attack.

## Special enemy actions

**ROM static.** The eight metadata choices select ordinary physical attacks,
blocking (`00:1389`), self-healing (`00:13CC`), self-boost (`00:1402`), double
strike (`00:1424`), a group spell (`00:14A0`), direct death (`00:151A`), or the
Crystal-dependent group path (`00:1561`). Blocking, double strike and the group
spell choose their special branch on a low-two-bit zero draw; boosting uses
four bits. Otherwise they use the ordinary physical action. Self-healing is
unconditional below 30 HP and otherwise uses a low-three-bit zero draw; it adds
80 with saturation and then clamps to the record's maximum HP.

**ROM static.** Double strike calls its shared hit routine and then falls into
it again, for two independent living-target draws and 7..10 damage per hit.
The group spell visits the four party records in order and varies a base of
40 down to 25..40 per recipient. High protection blocks these effects and is
consumed independently on each visit. Direct death instead checks Odin's
activity and Mirror Shield specifically, falls back to a physical attack
when that shield applies, and otherwise clears one living target's activity
and HP after consuming protection.

**ROM static, ROM run.** Crystal presence selects the shared physical group
path; absence sends positive amounts 1..16 into the carry-based HP writer.
That clears a prepared 100-HP party, but a byte overflow can leave a small
nonzero HP value. An additional complete case starts at 250 HP, captures
each recipient's draw, and independently checks the resulting carry/clamp
formula.

**ROM run.** Both cartridges pass 121,856 state and arithmetic probes,
including every current/maximum HP combination for self-healing and every
shield byte with four activity states for the Odin check. Eighteen complete
chains cover blocking, three healing limits, boost plus existing weakening,
double strike, empty/sparse/full group targets, protection, direct death,
Mirror Shield, and both Crystal branches.

## Battle endings and restoration

**ROM static.** Defeat (`00:1602`) preserves scripted encounters 31h and
4Ah; ordinary defeat reports game over and requests title mode 2. Victory
(`00:1656`) preserves encounter 31h, otherwise restores the field and awards
experience through the existing reward routine. Encounters 48h and 49h wait
180 frames before the victory message. Both endings reset the sixteen-byte
palette shadow for encounter 46h. Escape restoration preserves the action
code across field restoration before its terrain follow-up.

**ROM static.** Treasure setup (`00:16B2`) selects bank 20h, copies eight
descriptor bytes and thirty-two palette bytes, loads graphics, clears the
object shadow by overlap, and requests object 0Dh. The object engine resolves
it to chest 0Eh during the following wait. The field and treasure clears end
their setup on a native zero seed; four exact ED B0 entries use the reviewed
adaptive-copy contract, with at most sixteen transfers and a native last
transfer. Party unlock (`00:16F1`) enables a level-one record, refreshes stats,
and fills current HP/MP from their maxima.

**ROM run.** Both cartridges pass 28,160 state/ending probes plus 12,962
copy/seed admission cases. The latter cover overlap and mirrors, count edges,
mapper refusal, every relevant frame/IRQ/NMI offset, and equality at the
window boundary. Twenty complete chains cover ordinary/scripted defeat,
late victories, escape with and without dungeon backstep, field restoration,
treasure construction, money and item awards, and all four party unlocks.

## Field and battle items

**ROM static.** The existing use dispatcher (`00:2201`) reads one of
sixty-four effect pointers. Ground vehicles (`00:22B7`) require world index
below 4 and non-dungeon interaction, then ask the existing terrain helper for
a spot. Ice Digger first requires world 2. Hovercraft uses its separate spot
search. Success requests vehicle 4, 8 or 12 and a field refresh; refusal keeps
the vehicle and leaves the request clear.

**ROM static.** Wand delegates to shared battle escape. Escaper first removes
itself in battle, even when escape is refused; field refusal keeps it. Flute
uses the shared dungeon exit without removing itself. Flash is removed only
for a dark dungeon that accepts its light/refresh request. Transfer is removed
only for an accepted non-dungeon field context. Magic Hat is removed before
checking context, including a field refusal.

**ROM run.** Both cartridges pass 38,656 segment and independent vehicle-gate
probes. Twenty-nine complete calls enter through the original item dispatcher
from initialized map, dungeon or battle frames. They cover successful and
refused terrain searches, world limits, shared effects, existing light, item
consumption, and escape refusal before or after consumption. Terrain-cache
edits are explicit prepared cases rather than new input recordings.

## Story items and dungeon tools

**ROM static.** Alsulin (`00:2435`) and Polymaterial (`00:2497`) gate by
battle state, interaction scene and an OR of the full Alis/Odin/Noah record
bytes. Alsulin restores Odin's equipment, unlocks his level-one record and
requests the scene follow-up. Polymaterial delegates to the existing Hapsby
interaction after consuming the selected item. Sphere removes itself before
delegating to the shared battle communication or field inspection effect.

**ROM static.** The dungeon tools distinguish unopened door kind 5 from
magic-door kind 6. Dungeon Key accepts only the former; Miracle Key requests
3 or 4 for either. These handlers check interaction type and door state,
retain the keys, and leave opening to the existing dungeon/menu consumer.
Compass chooses one of four direction messages from the low two facing bits.
Hapsby and the unused-use handler select context-dependent messages.

**ROM static.** Eclipse Torch (`00:253D`) requires field scene AFh, flashes
the palette, and checks for Laconia Pot. It consumes the selected Torch, keeps
the Pot, and adds Nuts only when they are absent. Aero Prism requires scene
B0h with its auxiliary byte different from FFh and requests the existing
palette sequence. Nuts requires a nonzero Myau record; its castle-location
route checks field context, while its scene-B0h/auxiliary-FFh route does not
repeat the battle check. The handler requests the existing transformation
follow-up without removing Nuts itself.

**ROM run.** Both cartridges pass 56,640 segment and independent gate probes,
plus forty-eight complete calls through the original item dispatcher. Cases
include Odin restoration, Hapsby already present or newly added, Torch reward
with and without Nuts, every key cell byte, all compass direction bytes,
refused and accepted story scenes, and the distinct Nuts routes. All
sixty-four item-table slots resolve to registered effect families or the two
explicit native selected-item-removal calls that start Magic Hat and Sphere.
These calls carry the selected inventory pointer to the existing M12 owner.
