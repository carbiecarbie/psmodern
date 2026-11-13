# How it works: reading the controller

**ROM static.** The game keeps two small records of the controller: which
buttons are held now, and which have just been pressed. The routine at
`00:0339` does this in both the US Rev-A and Brazilian cartridges. It is
the first game routine rewritten in C++; the rest of the controls still
run as original Z80 code.

The keys and buttons a player presses are turned into the console's buttons
before the game reads the port, and can be changed; see
[player controls](../player-controls.md). This chapter is about what the game
does with those buttons.

## Holding a button and pressing it

**ROM static.** The routine reads controller port `$DC`, where a zero bit
means a button is pressed. It reverses those bits so that a one means
pressed. It compares this new reading with the previous reading at `$C204`.
Then it stores the new held buttons at `$C204` and the newly pressed buttons
at `$C205`.

**ROM static.** For each button, the second record says yes only when it is
pressed now and was released before. Holding a direction across two reads
therefore leaves it in the held record, but removes it from the new-press
record on the second read. Releasing it clears both records. Pressing it
again creates another new press. Several buttons can be recorded together.
This routine does not decide what those buttons do in a menu or on a map.

**ROM static.** The port also contains the second controller's up/down bits.
The routine keeps all eight bits; it does not discard those two. This says
what the routine records, not that the game provides a second-player mode.

## Keeping the original timing

**ROM static; ROM run.** There is one path through this routine. It takes
70 processor cycles, including its return to the caller, and ten opcode
fetches. The controller read happens ten cycles after entry. Its code is
identical at the same addresses in both identified ROMs. The known direct
call sites are `00:01C5` and `00:0256`.

**ROM run.** The routine test tries all 65,536 combinations of an old
held-button byte and a new controller byte against each cartridge. It checks
the two records, the complete machine state, the read's timestamp, cycles
and fetches against the original. The twelve checkpoint recordings also
check each admitted C++ call against an original call from the same state,
and compare all frames with routines off and with the reference emulator.
The [generated coverage](../coverage.md) records how often C++ runs.

**Maintainer decision.** When an interrupt or frame boundary could fall
inside the call's 70-cycle window, PSModern leaves that call on the Z80.
This keeps the original timing while letting the other calls run in C++.
See [development](../development.md#registered-routines) for that rule.
