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
