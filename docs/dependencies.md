# Dependencies

## Toolchain

Validated on Windows x86-64 with MSYS2 UCRT64, GCC/G++ 16.2.0,
CMake 4.4.2, Ninja 1.13.2, SDL2 2.32.10 and Git for Windows
2.56.0.windows.4. SDL2 is linked through `SDL2::SDL2`; its DLL and the
UCRT64 compiler runtime DLLs are copied next to `psmodern.exe` after the
build: `SDL2.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll` and
`libwinpthread-1.dll`. `build-rel` uses C++20,
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

## Android family build

The standalone Gradle project under `android/` builds one APK for ARM64 and
ARMv7, with Android 7.0 (API 24) as its minimum. It uses JDK 21, Gradle 9.3.1
(the wrapper verifies the distribution SHA-256), Android Gradle Plugin 9.1.0,
Android SDK Platform 36 revision 2, Build Tools 36.0.0, CMake 3.22.1 and NDK
r28c (28.2.13676358). The pinned [AGP release](https://developer.android.com/build/releases/agp-9-1-0-release-notes)
lists the matching Gradle, Build Tools and NDK versions.

Extract the [SDL2 2.32.10 source archive](https://www.libsdl.org/release/SDL2-2.32.10.tar.gz)
outside the checkout after verifying SHA-256
`5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`.
The build uses both its C sources and its Android Java classes. `android/build.gradle`
reads `versionName` directly from `CMakeLists.txt` and derives `versionCode` as
`major * 10000 + minor * 100 + patch`. Set `ANDROID_HOME` to the SDK and
`PSMODERN_SDL2` to this extracted source directory.

- **Local debug build:** from `android/` on Windows, run `.\gradlew.bat assembleDebug`.
  The APK is `android/build/outputs/apk/debug/PSModern-debug.apk` (signed with the
  local Android debug key).
- **Signed release build:** set `PSMODERN_KEYSTORE`, `PSMODERN_KEYSTORE_PASSWORD`,
  `PSMODERN_KEY_ALIAS` (default `psmodern`) and `PSMODERN_KEY_PASSWORD` (defaults to
  the keystore password) and run `.\gradlew.bat assembleRelease`, or use
  `tools/package-android-release.ps1` to build from a clean export of `HEAD` and
  run all release checks automatically. Keep the release keystore and its passwords
  backed up safely outside version control; changing keys prevents in-place updates.

Every APK bundles `assets/PSModern-LICENSE.txt` (GPL-3.0-or-later),
`assets/Nuked-OPLL-LICENSE.txt` (GPL-2.0-or-later),
`assets/SDL2-LICENSE.txt` (zlib), `assets/LIBCXX-LICENSE.txt` (LLVM `libc++_shared.so`,
Apache-2.0 with LLVM Exception from the pinned NDK), and `assets/THIRD-PARTY.txt`
recording the version and source commit.

Verify a packaged APK with the pinned Build Tools (automated by
`tools/package-android-release.ps1`): run `apksigner verify --verbose --print-certs`
(confirming the release certificate, never `CN=Android Debug`),
`zipalign -c -P 16 4`, and `aapt2 dump badging` (confirming package name, version
and absence of `application-debuggable`). For each ABI, use the NDK's
`llvm-readelf -n` to match the build ID of the packaged `libmain.so` to its
unstripped counterpart under `android/build/intermediates/cxx/<Release|Debug>/`,
and inspect the unstripped library with `llvm-nm --demangle` for `SDL_main` and
the absence of the Z80 interpreter and test oracle.

## macOS build

Validated on macOS 27.2.1 (Apple silicon) with Apple clang 21.0.0 from the
Xcode Command Line Tools, CMake 4.5.1 and Ninja 1.13.2 (Homebrew). The app is
universal (`arm64` and `x86_64`) with a minimum of macOS 11.0; the `x86_64`
slice was also run under Rosetta. SDL2 2.32.10 is built from the same source
archive as the Android build (SHA-256 above) as a static universal library, so
`PSModern.app` links only libraries that ship with macOS (AppKit,
UniformTypeIdentifiers and the frameworks SDL2 uses). A static SDL2 exports
only `SDL2::SDL2-static`; `CMakeLists.txt` aliases it to `SDL2::SDL2`.

`src/app/platform_macos.mm` is the platform layer: saves under
`~/Library/Application Support/PSModern`, the AppKit Open panel for `.sms`
files, Finder for the saves folder, and `~/Library/Logs/PSModern.log` for
diagnostics when the app is started from Finder (whose standard streams lead
to `/dev/null`). Objective-C++ also builds without `NDEBUG`.

Development build and data-free tests, with SDL2 installed to `<sdl2-prefix>`
(as `tools/package-macos.sh` does):

```sh
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPSMODERN_ROM_TESTS=OFF \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PREFIX_PATH=<sdl2-prefix>
cmake --build build-macos
ctest --test-dir build-macos -j "$(sysctl -n hw.ncpu)" --output-on-failure --no-tests=error
open build-macos/PSModern.app
```

`tools/package-macos.sh <output-folder>` (outside the checkout) downloads and
verifies SDL2, builds it and the `psmodern` target, strips the app, signs it
ad hoc, checks both slices with `--version` and that nothing outside macOS is
linked, and writes `PSModern-<version>-macos-universal.zip` with
`PSModern.app`, `LICENSE.txt`, `README.txt` (`tools/release/README-macOS.txt`),
`THIRD-PARTY.txt` and `licenses/` (Nuked-OPLL and SDL2). A dirty tree adds
`-dirty` to the name. The app is not notarized. The repository holds no icon
(no art from the game, see the root `AGENTS.md`); `PSMODERN_ICON=<png|icns>`
embeds a local one into the packaged app.

## Windows libraries and version header

`psmodern.exe` also links the Windows system libraries `comdlg32` (Open
dialog), `shell32` (command-line arguments, opening the saves folder) and
`ole32`; they ship with Windows. It is built as a GUI-subsystem program.
CMake generates `PSModernVersion.h` from `cmake/PSModernVersion.h.in` using
the version in `project(PSModern VERSION ...)`.

## Release package

The zip made by `tools/package-release.ps1` contains `psmodern.exe`
(stripped of debug information), the DLLs `SDL2.dll`, `libgcc_s_seh-1.dll`,
`libstdc++-6.dll` and `libwinpthread-1.dll` found with MSYS2 `ldd` and copied
from `ucrt64\bin` only, `LICENSE.txt`, `README.txt`, `THIRD-PARTY.txt` and a
`licenses\` folder. The notices come from these sources:

| Notice | Source |
| --- | --- |
| `Nuked-OPLL.txt` | `third_party/nuked-opll/LICENSE` |
| `SDL2.txt` | MSYS2 `share\licenses\SDL2\LICENSE.txt` |
| `winpthreads.txt` | MSYS2 `share\licenses\winpthreads\COPYING` |
| `GCC-runtime-exception.txt`, `GCC-GPL-3.0.txt` | MSYS2 `share\licenses\gcc-libs\COPYING.RUNTIME`, `COPYING3` |

`THIRD-PARTY.txt` names the `pacman -Q` versions of the shipped packages and
the exact source commit and tag. Since milestone 14 `psmodern.exe` contains
neither the Z80 core nor the Zeta headers (see below), so the release carries
no license file for them and the script no longer checks this file for their
commits. `THIRD-PARTY.txt` keeps a short credit to the Z80 core as the
reference the tests compare against, saying the program contains none of its
code (maintainer decision, 2026-11-19). The Windows executable contains
PSModern's code and Nuked-OPLL. The zip includes the four DLLs and the
license texts listed above.

## FM sound chip

[Nuked-OPLL](https://github.com/nukeykt/Nuked-OPLL/tree/1269cf5a783b65583b50fa2464d08be75830aaa0)
emulates the Yamaha YM2413 and is pinned to commit
`1269cf5a783b65583b50fa2464d08be75830aaa0`, GPL-2.0-or-later. Its `opll.c`,
`opll.h` and `LICENSE` are vendored verbatim under `third_party/nuked-opll/`.
The factory instrument tables in upstream `opll.c` describe the sound chip's
built-in patches. PSModern selects upstream's `opll_type_ym2413`, which uses
the `patch_ym2413` table. Upstream credits digshadow and John McMaster for
a VRC VII die photograph. It leaves the origin of individual YM2413 preset
values unspecified. These are upstream hardware constants. The game scores
and effects are read from the player's Japanese cartridge. The chip core is
linked into the Windows executable and Android's `libmain.so`.

## Z80 core

Since milestone 14 the Z80 core is **test-only**. [redcode/Z80](https://github.com/redcode/Z80)
and the Zeta headers it needs are still vendored in `third_party/`, but CMake
compiles the core only into the library `psmodern_oracle` (the oracle that
tests and developer tools such as the replay runner compare the game with).
`psmodern.exe` and the libraries it links (`psmodern_hw`, `psmodern_rom`,
`psmodern_game`, `psmodern_app`) contain no code from either; the test
`program_link_closure` checks the link and that no `z80_` symbol is in an
unstripped `psmodern.exe`. The program's own register file (`Z80` in
`src/hw/cpu.h`) keeps the core's member names and encodings so the oracle can
exchange state with it. The program's C++ re-expression of the core's behaviour
(interrupt acceptance in `Machine::accept`, the instruction renderers of the
kits) follows the core's cycles, flags and arbitration and cites it in
comments.

### The vendored core

[redcode/Z80](https://github.com/redcode/Z80) is vendored at commit
`dcc3f28d3a344b3080e7ae6f1e7b694b45b370d9`, LGPL-3.0-or-later, compatible
with this project's GPL-3.0-or-later license. `third_party/redcode-z80`
contains its API header, C implementation, and GPL and LGPL license texts.
`Machine::accept` reproduces the core's interrupt acceptance rules in C++;
re-verify it against `Z80.c` (lockstep does this on every test run) whenever
the core pin changes.
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
figure 7](https://www.zilog.com/docs/z80/um0080.pdf), shows the I/O machine
cycle as T1, T2, an automatic wait state and T3, with read data taken in T3
before /RD goes inactive. The wrapper timestamps port accesses at the start of
T3, three T-states after the start of the I/O machine cycle given by the core's
helpers. [Development](development.md#vdp-and-interrupt-timing) gives the
timing evidence.

## SMS VDPTest conformance

[FluBBaOfWard/SmsVdpTest](https://github.com/FluBBaOfWard/SmsVdpTest)
is pinned to commit `490c00b38c83012dee5a7a582097f080c210a3e7`.
Its source does not state a license, so neither its source nor the built ROM
is in this repository. The local build used
[WLA-DX v10.5](https://github.com/vhelin/wla-dx/releases/tag/v10.5), commit
`7461036`, kept outside the repository. The source required one build-only
cross-section branch change at `testmain.asm:1731`, `jr StdOut_Char` to
`jp StdOut_Char`; the patch is kept with the external build material. The
resulting 32 KiB `VDPTest.sms` SHA-256 is
`8fbacb3f4c150453cdfa2a4c270682245d6b29040e770fbad9b28e0d54d4e563`.
The corresponding `VDPTest.sym` SHA-256 is
`5ea322a0a3132cd305916e7da48f646ba80fe0dd68fd73325ee29d984add0782`.
The conformance test refuses a different binary or symbol file before execution.

Set `PSMODERN_VDP_TEST_DIR` to an external directory containing `VDPTest.sms`
and WLA-DX's `VDPTest.sym`. The test runner reads the `RunTest60` and
`WaitForButton` labels from that symbol file, then reads the 60 result bytes
at work RAM `$C900`–`$C93B` when the test page completes. Result 2 is the
expected NTSC branch of test 21; zero is success for every other applicable
automated case. A missing path or file skips the CTest entry. The source
does not call tests 20 or 37–40 in the SMS1 path. Test 90 is an unscored
visual check of the horizontal-scroll latch and needs picture inspection.
Test 200 displays CPU opcode timing, outside the VDP conformance scope. Neither is
counted in the 55 automated results.

## Reference emulators

The reference setup uses [MesenCE 2.2.1](https://github.com/nesdev-org/MesenCE/releases/tag/2.2.1)
(commit `20ba206c`, Windows) with Lua and `--testRunner`. The second opinion
is [Emulicious 2026-03-27](https://emulicious.net/downloads/), configured as
Master System 1, export/NTSC, without BIOS or FM. The driver setup and the
fixed start are in [development](development.md#reference-driver) and
[development](development.md#fixed-machine-start-and-frame-input).

The primary reference is a build of MesenCE tag `2.2.1` (commit
`20ba206cef5ba207c21203176d02cb9f43dda9fb`) with three patches applied in
order: the six Z80 instruction-duration corrections in
[mesence-2.2.1-durations.patch](../tools/mesence/mesence-2.2.1-durations.patch),
then INT sampling at the start of the final T-state with port transfers at
the start of T3 in
[mesence-2.2.1-int-boundary.patch](../tools/mesence/mesence-2.2.1-int-boundary.patch),
then block I/O flags (including repeat-phase flags) in
[mesence-2.2.1-block-io.patch](../tools/mesence/mesence-2.2.1-block-io.patch).
The changes and their [Zilog UM0080](https://map.grauw.nl/resources/cpu/z80.pdf)
basis, plus the block-flag sources, are described in
[development](development.md#reference-comparison). Emulicious is the
independent timing check. SMS Reset remains unsupported through Lua.

To rebuild, check out that upstream tag, apply all three patches in that
order, build the native core (`InteropDLL/InteropDLL.vcxproj`, `Release|x64`) and publish the UI
(`UI/UI.csproj`, `win-x64`, framework-dependent single file) following the
upstream build instructions. This requires the .NET 8 SDK and MSVC toolset
`v145` or newer. The UI embeds `MesenCore.dll` in `Dependencies.zip`: publish
the UI again after changing the native core, and force `Rebuild` of both
projects to avoid stale objects or archives. Replacing an adjacent DLL alone
does not apply the patch. The upstream UI pre-build command does not quote its
source path, so use a path without spaces for that build. Keep binaries, build
output and captures outside this repository. Each run's `reference.json`
identifies the executable build used.

The fixed PSG state uses CE's [SmsPsg.cpp](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsPsg.cpp)
and [SmsTypes.h](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsTypes.h).
External PSG comparisons use its Lua write callbacks and native
[`WaveRecord` API](https://github.com/nesdev-org/MesenCE/blob/2.2.1/InteropDLL/RecordApiWrapper.cpp).
The callback-domain limitation and workaround are documented in
[development](development.md#reference-comparison), from CE's
[ScriptingContext.cpp](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/Debugger/ScriptingContext.cpp)
and [SmsConsole.cpp](https://github.com/nesdev-org/MesenCE/blob/2.2.1/Core/SMS/SmsConsole.cpp).
These are reference evidence. The production FM chip uses the vendored
Nuked-OPLL core described above.

## Hardware documentation

- [SMS Power development index](https://www.smspower.org/Development/Index),
  [memory map](https://www.smspower.org/Development/MemoryMap),
  [mapper registers](https://www.smspower.org/Development/Mappers), and
  [I/O port map](https://www.smspower.org/Development/IOPortMap): memory and ports.
- [Charles MacDonald's VDP notes](https://www.smspower.org/uploads/Development/msvdp-20021112.txt): display and interrupts.
- [Charles MacDonald's SMS display timing measurements](https://www.smspower.org/forums/8161-SMSDisplayTiming): IRQ, sync and Pause NMI timing on an original NTSC console.
- [SMS VDPTest](https://github.com/FluBBaOfWard/SmsVdpTest): hardware-tested
  VDP counter, IRQ and status timing. Its author describes testing the original
  SMS1 VDP in [the SMS Power discussion](https://www.smspower.org/forums/10695-SMSVDPTester?start=50).
- [Maxim's SN76489 notes](https://www.smspower.org/Development/SN76489): Sega sound.
- [Zilog Z80 CPU User Manual](https://www.smspower.org/uploads/Development/z80cpu_um.zip): CPU bus and interrupt timing.

## Continuous integration

The GitHub Actions workflow builds on hosted Windows 2025 with MSYS2 UCRT64.
It installs the exact package files above from the MSYS2 repository after
verifying SHA-256, configures `build-rel` with `PSMODERN_ROM_TESTS=OFF`,
builds and runs CTest. CPython 3.13 x64 is provisioned by `actions/setup-python`
v6.1.0, pinned to commit `83679a892e2d95755f2dac6acb0bfd1e9ac5d548`.
The action's absolute `python-path` output is passed to CMake through
`Python3_EXECUTABLE`, converted with `cygpath -m`; discovery does not depend on
Python being in MSYS2's minimal PATH. Python remains required for test builds,
including the `replay_tools` and `routine_coverage_tools` CI entries. No pip packages are
installed. Actions use full commit pins, checkout does not retain
credentials, the token is read-only, and no secrets or ROMs are used. The
hosted run covers data-free tests only and does not replace the local complete
suite. Old package files or the pinned Actions may become unavailable; update
the package list and hashes together when maintaining the toolchain.

A second job builds on hosted macOS 15 (Apple silicon): it verifies and
builds the SDL2 2.32.10 source as a static universal library, then configures
the universal app with `PSMODERN_ROM_TESTS=OFF` and the same Z80 conformance
programs, checks for `NDEBUG`, builds and runs CTest.

## Replay and reference scripts

The recording launcher, comparison tool, coverage generator
(`tools/generate_coverage.py`) and Python test helpers use Python 3
standard library only; validated locally with **Python 3.13.15**. CMake discovers
Python through `find_package(Python3 COMPONENTS Interpreter)` when tests are
built. There are no pip dependencies. The reference launcher accepts stock or
patched Windows **MesenCE 2.2.1** (upstream product version
`2.2.1+20ba206cef5ba207c21203176d02cb9f43dda9fb`) and its adjacent native DLLs.
It copies these into a new external portable folder per run; none are vendored.
Pass `--reference-label` to identify the build; `reference.json` records the
label and executable/DLL SHA-256 digests.
See [Reference driver](development.md#reference-driver) for the SetInput API
workaround, instruction-boundary capture and unsupported SMS Reset pin.
