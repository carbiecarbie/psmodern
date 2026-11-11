# PSModern

> [!IMPORTANT]
> PSModern is an unofficial fan project in an early stage: the original game
> runs, a few of its routines are rewritten in C++, and the first two options
> (random encounters and experience/battle money) can be set inside it. It ships no
> game data: you will need your own Phantasy Star cartridge dump for the
> Sega Master System (US Rev-A, or the Brazilian Tec Toy release).

PSModern is an open-source reimplementation of **Phantasy Star** (Sega Master
System, 1987/88) in C++, for modern computers. The goal is to match the
original cartridge in how it looks, sounds, plays and times. Optional
comfort features are off by default: adjustable experience and random
encounters exist already, and a dungeon automap and widescreen on the world
map and in towns will come later.

The original game runs on a virtual Z80 from the start, and its routines are
rewritten in C++ one at a time, each checked against the original, until no
original code is left. Along the way, the project documents how every part
of the game works.

- [Design](docs/design.md)
- [Project status](docs/project-status.md)

## Build and run

With the [pinned MSYS2 UCRT64 toolchain](docs/dependencies.md), in a UCRT64 shell:

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rel
ctest --test-dir build-rel --output-on-failure
./build-rel/psmodern.exe "path/to/your-game.sms"
```

New profiles use original rules, with the cartridge's picture, sound, controls and saves.
See [project status](docs/project-status.md) for the controls and
[development](docs/development.md) for ROM test paths.

Press Escape or controller Y to open Options. Arrows or the d-pad select a
row; Left/Right change a rate; Z/A saves both rates and closes;
X/B or Escape/Y cancels. Enter/Start still press the original Pause button.
Options is available while exploring towns, the world map and dungeons,
with the current scene behind its original-style black and white window.
The music continues. Each rate marks 100% as DEFAULT; encounters 50% and
experience/meseta 400% are marked RECOMMENDED. Confirming saves the choice
for that version's whole cartridge save image, shared by its original slots.
Changes take effect when the current game operation finishes, including the
entire battle/reward sequence. The title shows active
rates and any waiting change.

Two optional flags override saved choices for the current session:

```sh
./build-rel/psmodern.exe "path/to/your-game.sms" --encounter-rate 50 --experience-rate 200
```

`--encounter-rate` takes 0, 50, 100 or 200: 0 turns random fights off, 50 halves
them and 200 doubles them, while story and fixed fights stay as they are.
`--experience-rate` takes 100, 200 or 400 and multiplies the experience each
fight gives and battle money. Any other value is refused. 100, the default for both, is the
original game. For a pace like the Switch release, use `--encounter-rate 50
--experience-rate 400`. A value other than 100 cannot be combined with
`--record` or `--no-routines`; those sessions use original rules and keep saved
preferences. Omitted flags load saved choices, and explicit 100 overrides a
saved enhancement. Flags stay session-only until confirmed in Options.

The raw save stays compatible with the cartridge and other emulators. Settings
live beside it in a companion file; copying only raw SRAM carries earned
totals but no options. A raw image without matching settings uses original
rules. Turning enhancements off keeps the totals already earned.

## License

GPL-3.0-or-later. Phantasy Star is a trademark of SEGA; this project is not
affiliated with or endorsed by SEGA or Tec Toy.
