# PSModern

> [!IMPORTANT]
> PSModern is an unofficial fan project in an early stage: the original game
> runs, but nothing is rewritten yet and there are no options. It ships no
> game data: you will need your own Phantasy Star cartridge dump for the
> Sega Master System (US Rev-A, or the Brazilian Tec Toy release).

PSModern is an open-source reimplementation of **Phantasy Star** (Sega Master
System, 1987/88) in C++, for modern computers. The goal is to match the
original cartridge in how it looks, sounds, plays and times. Optional
comfort features, such as adjustable experience and random encounters, a
dungeon automap and widescreen on the world map and in towns, will come as
options that are off by default.

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

## License

GPL-3.0-or-later. Phantasy Star is a trademark of SEGA; this project is not
affiliated with or endorsed by SEGA or Tec Toy.
