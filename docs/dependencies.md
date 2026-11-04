# Dependencies

## Toolchain

Validated on Windows x86-64 with MSYS2 UCRT64, GCC/G++ 16.2.0,
CMake 4.4.2, Ninja 1.13.2, SDL2 2.32.10 and Git for Windows
2.56.0.windows.4. SDL2 is linked through `SDL2::SDL2`; its DLL and the
UCRT64 compiler runtime DLLs must be on PATH. `build-rel` uses C++20,
Ninja, RelWithDebInfo, `-O2 -g`, and no `NDEBUG`.

The following package versions were obtained from `pacman -Q` on the
maintainer's machine, including the dependency closure of GCC, CMake, Ninja
and SDL2. The exact package files and SHA-256 hashes are recorded in
`.github/ci/msys2-packages.txt`; update both lists together.

```text
mingw-w64-ucrt-x86_64-SDL2 2.32.10-1
mingw-w64-ucrt-x86_64-binutils 2.47-3
mingw-w64-ucrt-x86_64-brotli 1.2.0-1
mingw-w64-ucrt-x86_64-bzip2 1.0.8-4
mingw-w64-ucrt-x86_64-c-ares 1.34.8-1
mingw-w64-ucrt-x86_64-ca-certificates 20260816-1
mingw-w64-ucrt-x86_64-cmake 4.4.2-2
mingw-w64-ucrt-x86_64-cppdap 1.65.r11.g6464cd7-1
mingw-w64-ucrt-x86_64-crt 14.0.0.r353.g6df76fa52-4
mingw-w64-ucrt-x86_64-curl 8.22.0-1
mingw-w64-ucrt-x86_64-expat 2.8.4-2
mingw-w64-ucrt-x86_64-gcc 16.2.0-3
mingw-w64-ucrt-x86_64-gcc-libs 16.2.0-3
mingw-w64-ucrt-x86_64-gettext-runtime 1.0-1
mingw-w64-ucrt-x86_64-gmp 6.3.0-2
mingw-w64-ucrt-x86_64-gnutls 3.8.13-3
mingw-w64-ucrt-x86_64-headers 14.0.0.r353.g6df76fa52-2
mingw-w64-ucrt-x86_64-isl 0.28-1
mingw-w64-ucrt-x86_64-jsoncpp 1.9.8-1
mingw-w64-ucrt-x86_64-libarchive 3.8.9-5
mingw-w64-ucrt-x86_64-libb2 0.98.1-3
mingw-w64-ucrt-x86_64-libffi 3.8.0-1
mingw-w64-ucrt-x86_64-libiconv 1.19-1
mingw-w64-ucrt-x86_64-libidn2 2.3.8-4
mingw-w64-ucrt-x86_64-libpsl 0.21.5-3
mingw-w64-ucrt-x86_64-libssh2 1.11.1-2
mingw-w64-ucrt-x86_64-libtasn1 4.21.0-1
mingw-w64-ucrt-x86_64-libunistring 1.4.2-1
mingw-w64-ucrt-x86_64-libuv 1.52.1-1
mingw-w64-ucrt-x86_64-libwinpthread 14.0.0.r353.g6df76fa52-2
mingw-w64-ucrt-x86_64-lz4 1.10.0-1
mingw-w64-ucrt-x86_64-mpc 1.4.1-1
mingw-w64-ucrt-x86_64-mpfr 4.2.2-3
mingw-w64-ucrt-x86_64-nettle 4.0-1
mingw-w64-ucrt-x86_64-nghttp2 1.70.0-1
mingw-w64-ucrt-x86_64-nghttp3 1.18.0-1
mingw-w64-ucrt-x86_64-ngtcp2 1.25.0-1
mingw-w64-ucrt-x86_64-ninja 1.13.2-1
mingw-w64-ucrt-x86_64-openssl 3.6.4-1
mingw-w64-ucrt-x86_64-p11-kit 0.26.5-1
mingw-w64-ucrt-x86_64-pcre2 10.48-3
mingw-w64-ucrt-x86_64-pkgconf 1~3.0.5-1
mingw-w64-ucrt-x86_64-rhash 1.4.6-1
mingw-w64-ucrt-x86_64-tzdata 2026c-1
mingw-w64-ucrt-x86_64-vulkan-loader 1~1.4.357.0-1
mingw-w64-ucrt-x86_64-windows-default-manifest 20260815-1
mingw-w64-ucrt-x86_64-wineditline 2.208-1
mingw-w64-ucrt-x86_64-winpthreads 14.0.0.r353.g6df76fa52-2
mingw-w64-ucrt-x86_64-xz 5.8.3-1
mingw-w64-ucrt-x86_64-zlib 1.3.2-2
mingw-w64-ucrt-x86_64-zstd 1.5.7-2
```

## Z80 core

[redcode/Z80](https://github.com/redcode/Z80) is vendored at commit
`dcc3f28d3a344b3080e7ae6f1e7b694b45b370d9`, LGPL-3.0-or-later, compatible
with this project's GPL-3.0-or-later license. `third_party/redcode-z80`
contains its API header, C implementation, and GPL and LGPL license texts.
The required headers from [redcode/Zeta](https://github.com/redcode/Zeta)
are vendored at commit `42830b0e735cc3a83c7630e2dcf21949e8441c6d` under
`third_party/zeta`, with its GPL and LGPL license texts. Zeta headers are
LGPL-3.0; the applicable license is stated in each header.

The Z80 files are `API/Z80.h`, `sources/Z80.c`, `COPYING` and
`COPYING.LESSER`. The Zeta header subset under `API/Z/` is:

- `constants/{boolean,pointer}.h`, `types/{bitwise,integral}.h`;
- `formats/data_model/{I16LP32,ILP32,ILP64,IP16L32,LLP64,LP32,LP64,SILP64}.h`;
- `inspection/{C,C++,compiler,data_model,ISA,language,OS,platform}.h`;
- `inspection/C/modules/{C11,C23,C78,C89,C95,C99}.h` and
  `inspection/C++/modules/{C++03,C++11,C++14,C++17,C++20,C++23,C++98}.h`;
- `inspection/compiler/{completion,detection}.h`,
  `inspection/compiler/modules/{cc65,Clang,GCC,generic,MSVC,TinyCC}.h`,
  `inspection/data_model/{deduction,detection}.h`, and `detection.h` in
  each of `inspection/ISA`, `inspection/OS` and `inspection/platform`;
- `keys/{C,C++,compiler,data_model,endianness,fundamental,ISA,language,OS,platform}.h`;
- `macros/{bitwise,casting,language,structure,T,token,version}.h`.

## Z80 conformance programs

These programs and their test firmware remain outside the repository. The
test runner drives the same CPU wrapper as the machine with a flat test bus.
Each missing file is reported as a CTest skip; a present test must pass.

- Frank Cringle's [ZEXDOC and ZEXALL](https://github.com/agn453/ZEXALL),
  67 cases each; the ZEXDOC source grants GPL-2.0-or-later. Check the exact
  origin and notices of a supplied binary before redistributing it.
- Patrik Rak's [Zilog Z80 CPU Test Suite](https://github.com/raxoft/z80test),
  version 1.2a (2023-12-02), MIT: flags, MEMPTR and CCF variants, 160 cases
  each including four deliberately skipped alternative CPU variants. CI
  downloads these three TAPs and their MIT notice from the
  [pinned mirror](https://github.com/rejunity/z80-from-schematics/tree/6b1ffceab57561881e9cae429182dc3d4fc28038/tests/z80test),
  verifies SHA-256 and keeps them in the runner's temporary directory.
- Mark Woodmass's [Z80 Test Suite (2008)](https://zxe.io/depot/software/ZX%20Spectrum/Z80%20Test%20Suite%20%282008%29%28Woodmass%2C%20Mark%29%5B%21%5D.tap),
  flags (47 cases) and MEMPTR (58 cases). Its distribution license is not
  established; use a local copy. These tests also require the external 16 KiB
  Spectrum 48K firmware; its copyright is retained by Amstrad. An
  [account of its permission terms](https://github.com/z00m128/zxs-rom/blob/master/LICENSE.md)
  is available, but neither the firmware nor the Woodmass test is fetched by CI.

**Hardware documentation:** The [Zilog Z80 CPU User Manual, UM008011-0816,
figure 7](https://www.zilog.com/docs/z80/um0080.pdf), shows the I/O strobe
beginning in T2, one T-state after the start of the I/O machine cycle. The
wrapper timestamps the start of this strobe; the core's helpers give the start
of the machine cycle.

## Reference emulators

Part 0 used [MesenCE 2.2.1](https://github.com/nesdev-org/MesenCE/releases/tag/2.2.1)
(commit `20ba206c`, Windows) with Lua and `--testRunner`. The second opinion
is [Emulicious 2026-03-27](https://emulicious.net/downloads/), configured as
Master System 1, export/NTSC, without BIOS or FM. Part 0's detailed setup and
observations are in the [milestone plan](milestone-1-plan.md#2-reference-emulator).

## Hardware documentation

- [SMS Power development index](https://www.smspower.org/Development/Index),
  [memory map](https://www.smspower.org/Development/MemoryMap),
  [mapper registers](https://www.smspower.org/Development/Mappers), and
  [I/O port map](https://www.smspower.org/Development/IOPortMap): memory and ports.
- [Charles MacDonald's VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt): display and interrupts.
- [Maxim's SN76489 notes](https://www.smspower.org/Development/SN76489): Sega sound.
- [Zilog Z80 CPU User Manual](https://www.smspower.org/uploads/Development/z80cpu_um.zip): CPU bus and interrupt timing.

## Continuous integration

The GitHub Actions workflow builds on hosted Windows 2025 with MSYS2 UCRT64.
It installs the exact package files above from the MSYS2 repository after
verifying SHA-256, configures `build-rel` with `PSMODERN_ROM_TESTS=OFF`,
builds and runs CTest. Actions use full commit pins, checkout does not retain
credentials, the token is read-only, and no secrets or ROMs are used. The
hosted run covers data-free tests only and does not replace the local complete
suite. Old package files or the pinned Actions may become unavailable; update
the package list and hashes together when maintaining the toolchain.
