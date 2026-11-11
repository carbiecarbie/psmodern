# PSModern

> [!IMPORTANT]
> PSModern is an unofficial fan project in an early stage: the original game
> runs, a few of its routines are rewritten in C++, and the first two options
> (random encounters and experience) can be set when starting it. It ships no
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

The game runs in original mode, with its picture, sound, controls and saves.
See [project status](docs/project-status.md) for the controls and
[development](docs/development.md) for ROM test paths.

Leaving out every option plays the original game. Two optional flags change
how often random fights happen and how much experience you earn; they are off
unless you give them a percentage of the original, where 100 means unchanged:

```sh
./build-rel/psmodern.exe "path/to/your-game.sms" --encounter-rate 50 --experience-rate 200
```

`--encounter-rate` takes 0, 50, 100 or 200: 0 turns random fights off, 50 halves
them and 200 doubles them, while story and fixed fights stay as they are.
`--experience-rate` takes 100, 200 or 400 and multiplies the experience each
fight gives. Any other value is refused. 100, the default for both, is the
original game. For a pace like the Switch release, use `--encounter-rate 50
--experience-rate 400`. A value other than 100 cannot be combined with
`--record` or `--no-routines`. The options are not saved with the game.

## License

GPL-3.0-or-later. Phantasy Star is a trademark of SEGA; this project is not
affiliated with or endorsed by SEGA or Tec Toy.
