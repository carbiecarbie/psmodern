# PSModern

> [!IMPORTANT]
> PSModern is an unofficial fan project in its earliest stage: it does not run
> anything yet. It ships no game data: you will need your own Phantasy Star
> cartridge dump for the Sega Master System (US Rev-A, or the Brazilian Tec
> Toy release).

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

## License

GPL-3.0-or-later. Phantasy Star is a trademark of SEGA; this project is not
affiliated with or endorsed by SEGA or Tec Toy.
