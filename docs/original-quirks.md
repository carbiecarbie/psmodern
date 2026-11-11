# Original quirks

Odd behavior of the original game that original mode keeps on purpose, and
differences between the US Rev-A and Brazilian cartridges. Each entry gives
its evidence label ([documentation rules](AGENTS.md#evidence-labels)) and
where it lives.

## US and Brazilian versions

The two cartridges share their code and rules
([design, section 5.5](design.md#55-languages)). PSModern keeps every
difference below by running each version's own program and reading its own
text from the player's ROM.

### A different blank tile on the save-name screen

- **Where:** the operand at `01:4181`, the only byte of code that differs.
- **ROM static.** The instruction there fills the spacer cells of the
  name-entry screen. The Brazilian version writes a different blank tile.
  The instruction's length and cycle cost are the same.
- **ROM run.** In one sampled frame of that screen, video memory differed in
  those cells and the picture did not: the cells are black in both versions.

### Conversations that stop in different places

- **Where:** the text and its control codes, in banks `02` and `0C` of each
  ROM. The code that prints them is the same.
- **ROM static.** The Brazilian text is not the US text with other words.
  Its control codes decide where a conversation ends, waits for a button or
  runs into the next message, and in some scenes they differ. Examples: one
  yes/no question waits for a button first only in the Brazilian version;
  two conversations end one message earlier there, one of them leaving out
  a clue; one greeting is skipped there; the ending's last caption waits for
  a button only there.
- A Portuguese mode made by replacing only the words of the US text would
  therefore not behave like the Brazilian cartridge.

### Text length changes timing and random draws

- **Where:** the same text banks; the random draw reads the processor's R
  counter at `00:05C8`.
- **ROM run.** Printing takes about one frame per character, so text of
  another length takes another time. With the same input given to both
  versions, the first random draw on the map came on the same frame but a
  few cycles apart, read a different R and returned a different number. By
  the end of that input the two versions had made different numbers of
  draws.
- This is why each version has its own input recordings. See
  [random numbers](how-it-works/random-numbers.md) and
  [why exact timing matters](how-it-works/master-system.md#why-exact-timing-matters-for-this-game).

## Rules of the original game

### A total at the experience ceiling stops levelling

- **Where:** the addition and the exit that follows it, `00:1773`-`00:177F`
  (the per-character routine of [the award](how-it-works/party.md#giving-experience-after-a-battle)).
- **ROM static.** A character's experience is a two-byte total. When adding a
  reward overflows it, the game stores the largest value and returns before it
  tests the level. A character below level 30 whose total reaches the ceiling
  stops levelling for good: every later award overflows again and exits early.
  Original mode keeps this on purpose.
- **ROM static, Inference. Not reachable in play.** The largest reward one
  battle can produce is a few hundred points: the experience of any monster
  the game can name (every group of every encounter pool, every fixed fight
  set by a direct load, and every monster record up to the highest one named)
  times the largest group the setup can build, which is eight for a random
  group of a four-member party and a fixed number for a fixed fight. The
  smallest margin between the last level's threshold and the ceiling is a few
  thousand points, for one of the four characters, and far more for the
  others. A character can hold a total above a level's threshold only by
  carrying the surplus of an award that gave a single level, and the bound
  below accounts for that: starting a character with the largest possible
  surplus at every level, one award at a time, the total before an award plus
  the largest reward stays under the ceiling for every character at every
  level before 30. So no sequence of battles overflows the counter before the
  top level. The bound reads the tables through the machine in
  `tests/experience_tests.cpp` and fails if the ROM data would ever allow it.
- **Unknown.** One fixed fight takes its monster from event data rather than
  an immediate value (`01:69F7`); its identifiers were not enumerated. The
  bound above covers every identifier up to the highest the game names, and
  the records beyond that hold data of another shape (Inference from their
  contents).
- With the experience-rate option on, the ceiling is reachable (a rate of 400
  makes big rewards), and PSModern deliberately differs there: the total
  still saturates, and the character keeps gaining one level per award. This
  is a maintainer decision (2026-11-11)
  (see [party](how-it-works/party.md#the-experience-rate-option)).
