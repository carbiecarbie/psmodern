# How it works: town and dungeon interactions

When the party talks to someone, opens a door or reaches a special spot, the
game starts an "interaction". Some interactions only show a message, some open
a shop, and some are longer scenes that remember where the story stands.

**Since milestone 14.** The Z80 is no longer in the program. Where this chapter says that a
stretch stays on the Z80, is left to the original code, runs on the emulated processor, or falls
back when an interrupt or a frame boundary is near, it describes the milestone the text was
written for. Today every such instruction is rendered C++, an interrupt is accepted at the exact
instruction even inside a piece, and a state a piece cannot render stops the game with a message
([interrupts](interrupts.md), [development](../development.md#registered-routines)).

## Choosing an interaction

**ROM static; ROM run.** The interaction entry at `01:474B` reads the number of
the event the party has reached. Zero means there is no event, and the field
menu opens instead. Any other number selects a handler from a list kept in the
cartridge. After the handler finishes, the kind of interaction decides whether
the window simply closes or whether the game also returns to the field. Some
handlers skip the normal way back at `01:4765` and wait for a button press
instead. The jumps and calls between these steps are still made by the
original processor, so the game's timing and its handling of the stack are
exactly the original's.

**ROM static; ROM run.** Six entries of the list lead to places where the
player must use or look for an item: the petrified companion in the first
cave (dungeon 0), and the places where the Polymaterial, the torch, the prism
and the nuts are used, whose item routines check for these event numbers.
Their handlers are a single call or two. Five entries first wait for a button at `01:5535` and
then fall into the sixth entry's handler at `01:5538`, which opens the field
menu so that the item can be chosen; the event's number stays in memory, and
the item's own routine checks it. These two calls had no C++ version from
1.0.0 to 1.3.0, so the game stopped on reaching any of the six places;
1.3.1 added them, each tested against the original on both cartridges.

The list of handlers lives in the cartridge, and each entry points to code in
whichever bank is currently selected. **Inference:** an entry being present in
the list does not prove that ordinary play ever reaches it.

## Simple messages

**ROM static; ROM run.** A first group of 88 handlers only picks which message
to show and hands that number to the shared dialogue routine. PSModern reads
the message number from the player's own cartridge at the moment the original
would, then lets the original processor make the final jump. No dialogue text
and no table of message numbers is copied into the program. Every registered
entry is tested on both cartridges with varied processor state and banks: the
memory, timing and the jump target must all match the original. If a Pause
signal is due during one of these short steps, the original instructions run
it instead.

**ROM static; ROM run.** Two interactions choose what to say from the size of
the party. Five others ask a yes/no question and pick one of two replies; tests
try every possible value of the flags the question depends on, on both
cartridges. The prompt and the dialogue themselves are the shared routines
described in the [menus](menus.md) and [text](text.md) chapters.

## Longer conversations

**ROM static.** Longer conversations can check for an item, prepare a group of
enemies, give a reward or change where the party is sent next. The companion
encounter updates the party and the reward around calls to the dialogue and
inventory routines. Other conversations write a story flag before returning to
the dialogue. Each answer can lead to a different continuation.

## Story interactions and purchases

**ROM static.** In the letter scene the game prepares the move to the dungeon,
checks the party and the item, and copies the scene's colours. It saves three
characters' status values and puts them back after the scene. The colour copy
and the calls keep the original moments at which interrupts may arrive. The
party check begins at `01:4C9F`.

The passport clerk asks a series of questions and charges 100 meseta. The game
checks that the party can pay before writing the new balance, and uses the
ordinary inventory search when it looks for the item. In Myau's rescue, the
game asks about the pot, keeps the values it needs safe across the inventory
check, then unlocks Myau before giving the reward.

Another conversation changes with the party's progress. At the stage that
requires a payment, the price is 1,200 meseta; later replies and item checks
depend on the stage reached. The gas shield costs 1,000 meseta, and a separate
story purchase costs 400. Each checks the balance before taking the price and
updating the item or the story flag. Item trades use the usual inventory search
and removal routines, and a reply can also move the conversation forward.

**ROM run.** These story interactions are compared with the original on both
cartridges while the value their choice depends on (a flag, a story stage or a
random seed) is varied over its range.
