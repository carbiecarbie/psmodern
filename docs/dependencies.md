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

[redcode/Z80](https://github.com/redcode/Z80) is selected at commit
`dcc3f28d3a344b3080e7ae6f1e7b694b45b370d9`, LGPL-3.0-or-later, compatible
with this project's GPL-3.0-or-later license. It is not yet vendored; Part B
will bring in the pinned source and its license.

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
