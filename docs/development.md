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

Set the `PSMODERN_Z80_TEST_DIR` cache path to an external directory containing
`zexdoc.com`, `zexall.com`, `z80flags.tap`, `z80memptr.tap`, `z80ccf.tap`,
`woodmass.tap` and `spectrum48.rom` to run the Z80 conformance tests. The
Woodmass tests need both their TAP and the Spectrum 48K firmware. An empty
path or a missing file skips the affected tests; CI supplies the three Rak
TAPs and skips the other external suites. See
[dependencies](dependencies.md#z80-conformance-programs) for provenance and
licenses. These tests use the integrated CPU wrapper with a test bus, not
the game ROM or the upstream standalone harness.

Keep ROMs, emulator captures, traces, saves and other working material outside
the repository, under `D:/Projetos/Phantasy Star/`. Never commit ROM-derived
data.
