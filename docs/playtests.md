# Preparing a maintainer play-test

The maintainer's play time is the scarcest resource. Settle everything you
can first with tests, recordings and reference comparisons. Then ask for
a play-test only of what those cannot show: how the result looks, sounds
and feels to a player, and the paths through real devices and files.

## The build

- Test one fixed build. Copy `psmodern.exe` and its runtime DLLs (`SDL2.dll`,
  `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` from UCRT64)
  into a play-test folder outside the repository, so later rebuilds do not
  change what is being tested. Note the commit and the executable's SHA-256.
- Play-tests use the US version by default ([design](design.md#55-languages),
  section 5.5): add one launcher that passes the US ROM path. Add a Brazilian
  launcher only when a Brazilian difference is being tested. Set `APPDATA` to
  the absolute path of a `saves` folder next to the launcher, so the test
  starts with no save and never touches the player's own saves.
- Open each launcher once yourself and check that the window title names the
  expected language (or cartridge version on older builds). Empty the isolated
  save folder afterwards, retaining only the remembered ROM list needed to play.
- Keep a playable copy of the reference emulator ready, with the settings
  of [the reference driver](development.md#reference-driver), so the
  maintainer can compare the same moment of the game side by side.

## The guide

Write the guide in Portuguese, for a player rather than a programmer:

- what the test is for and how long it takes (and that it also covers the
  Brazilian version, when it does);
- how to open the program, with a table of keys and controller buttons;
- one numbered item per thing to check, saying what to do, what should
  happen and **why it needs a person** rather than a test;
- a short notes file with one line per item, to fill in with "OK" or what
  looked wrong;
- what to report when something fails: what was done just before.

## Afterwards

Read the notes. Check the files the test should have produced, such as the
shared save, its `.bak` copy and the lock file, before
treating an item as done. Record the result, including any part the maintainer chose to
skip, in the milestone plan at closure.

## Release play-test

Before a release, play-test the exact zip, not a development build:

- Extract the zip into a clean folder whose name has a non-ASCII character
  (for example `ção`), outside the repository. Note the commit and the zip's
  SHA-256.
- Rename the player's `%APPDATA%\PSModern` to `PSModern.backup` first, so the
  test starts with no remembered ROM and no save. Afterwards delete the
  test's `PSModern` folder and rename the backup back.
- Start by double-clicking `psmodern.exe`; no launcher script is used. The
  guide covers what only a person can judge: no console window, the Open
  dialog filter, the wrong-file message, a ROM in a non-ASCII folder, the
  start screen text, adding the second version, keyboard and controller,
  Alt+Enter carrying into the game, Open saves folder, Options and saves, and
  quitting closing the program.
