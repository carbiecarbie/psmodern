# Development

Use the pinned UCRT64 toolchain in [dependencies](dependencies.md). In a
UCRT64 shell, from the repository root:

```sh
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPSMODERN_ROM_US="D:/Projetos/Phantasy Star/Phantasy Star (USA) (Rev-A).sms" \
  -DPSMODERN_ROM_BR="D:/Projetos/Phantasy Star/m1-part0/static/roms/Phantasy Star (Brazil).sms"
cmake --build build-rel
ctest --test-dir build-rel --output-on-failure
./build-rel/psmodern.exe "D:/path/to/game.sms"
```

`build-rel` uses C++20 and `-O2 -g` with assertions enabled. For other
locations set `PSMODERN_ROM_US` and `PSMODERN_ROM_BR` to the player's own
uncompressed files. `PSMODERN_ROM_TESTS` defaults to `ON`: a missing or empty
ROM path makes its test report CTest's skip code 77; an existing wrong file
fails. Set the option to `OFF` to omit ROM tests, as CI does. The cache path
`PSMODERN_REFERENCE_DIR` is reserved for external reference output. Use
`ctest --test-dir build-rel -L fast --output-on-failure` for fast tests.
See [the complete-suite protocol](../tools/AGENTS.md) before a complete run.

Keep ROMs, emulator captures, traces, saves and other working material outside
the repository, under `D:/Projetos/Phantasy Star/`. Never commit ROM-derived
data.
