#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds PSModern.app as a universal (Apple silicon arm64 + Intel x86_64)
# macOS 11+ program and packages it as PSModern-<version>-macos-universal.zip.
#
# SDL2 2.32.10 (the version pinned in docs/dependencies.md) is downloaded,
# checked against its SHA-256 and built as a static universal library, so the
# app needs nothing outside macOS itself. The app is stripped and ad-hoc
# signed; it is not notarized, so Gatekeeper asks for confirmation on first
# launch (see README-macOS.txt in the zip).
#
# Usage: [PSMODERN_ICON=<icon.png|icon.icns>] tools/package-macos.sh <output-dir>
#   <output-dir> must be outside the repository; it receives the SDL2 build,
#   the PSModern build and the zip. The app is staged in a new temporary folder
#   inside it, removed at the end, so nothing else in <output-dir> is touched.
# Needs Xcode Command Line Tools, cmake and ninja (brew install cmake ninja).
set -eu

[ $# -eq 1 ] || { echo "Usage: $0 <output-dir>" >&2; exit 2; }
repo=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$1"
out=$(cd "$1" && pwd)
case "$out/" in "$repo"/*) echo "Output folder must be outside the repository." >&2; exit 2;; esac

sdl_version=2.32.10
sdl_sha256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
archs="arm64;x86_64"
min_macos=11.0

version=$(sed -n 's/^project(PSModern VERSION \([0-9.]*\).*/\1/p' "$repo/CMakeLists.txt")
[ -n "$version" ] || { echo "Cannot read the version from CMakeLists.txt." >&2; exit 1; }
head=$(git -C "$repo" rev-parse HEAD)
suffix=""
[ -z "$(git -C "$repo" status --porcelain)" ] || suffix="-dirty"
name="PSModern-$version$suffix-macos-universal"
zip="$out/$name.zip"
[ ! -e "$zip" ] || { echo "Refusing to overwrite $zip" >&2; exit 1; }

# 1. SDL2, static and universal.
sdl_prefix="$out/sdl2-$sdl_version-universal"
if [ ! -f "$sdl_prefix/lib/libSDL2.a" ]; then
    tarball="$out/SDL2-$sdl_version.tar.gz"
    [ -f "$tarball" ] || curl -fsSL -o "$tarball" "https://www.libsdl.org/release/SDL2-$sdl_version.tar.gz"
    echo "$sdl_sha256  $tarball" | shasum -a 256 -c - >/dev/null || { echo "SDL2 archive SHA-256 mismatch." >&2; exit 1; }
    rm -rf "$out/SDL2-$sdl_version" "$out/sdl2-build"
    tar -xzf "$tarball" -C "$out"
    cmake -S "$out/SDL2-$sdl_version" -B "$out/sdl2-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_OSX_DEPLOYMENT_TARGET=$min_macos \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$sdl_prefix"
    cmake --build "$out/sdl2-build"
    cmake --install "$out/sdl2-build"
fi

# 2. PSModern, game target only.
build="$out/build-macos-universal"
cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_OSX_DEPLOYMENT_TARGET=$min_macos \
    -DCMAKE_PREFIX_PATH="$sdl_prefix" -DPSMODERN_ROM_TESTS=OFF -DBUILD_TESTING=OFF
cmake --build "$build" --target psmodern

# 3. Stage in a fresh temporary folder: stripped, ad-hoc signed app plus licenses.
work=$(mktemp -d "$out/.package.XXXXXX")
trap 'rm -rf "$work"' EXIT
stage="$work/$name"
mkdir -p "$stage/licenses"
ditto "$build/PSModern.app" "$stage/PSModern.app"
exe="$stage/PSModern.app/Contents/MacOS/PSModern"
strip -S -x "$exe"
# Optional icon from outside the repository (no art is committed here):
# PSMODERN_ICON=<square .png, ideally 1024x1024, or an .icns>.
if [ -n "${PSMODERN_ICON:-}" ]; then
    [ -f "$PSMODERN_ICON" ] || { echo "Icon not found: $PSMODERN_ICON" >&2; exit 1; }
    icns="$stage/PSModern.app/Contents/Resources/PSModern.icns"
    mkdir -p "$(dirname "$icns")"
    case "$PSMODERN_ICON" in
    *.icns) cp "$PSMODERN_ICON" "$icns";;
    *)
        iconset="$work/PSModern.iconset"
        mkdir "$iconset"
        for size in 16 32 128 256 512; do
            sips -s format png -z $size $size "$PSMODERN_ICON" --out "$iconset/icon_${size}x${size}.png" >/dev/null
            sips -s format png -z $((size*2)) $((size*2)) "$PSMODERN_ICON" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
        done
        iconutil -c icns "$iconset" -o "$icns"
        rm -rf "$iconset";;
    esac
    plutil -replace CFBundleIconFile -string PSModern "$stage/PSModern.app/Contents/Info.plist"
    echo "Icon: $PSMODERN_ICON"
fi
codesign --force --sign - --timestamp=none "$stage/PSModern.app"
codesign --verify --strict "$stage/PSModern.app"

for arch in arm64 x86_64; do
    lipo "$exe" -verify_arch "$arch" || { echo "The app has no $arch slice." >&2; exit 1; }
done
for arch in arm64 x86_64; do
    if [ "$arch" = "$(uname -m)" ] || arch -"$arch" /usr/bin/true 2>/dev/null; then
        reported=$(arch -"$arch" "$exe" --version)
        [ "$reported" = "PSModern $version" ] || { echo "$arch slice reported: $reported" >&2; exit 1; }
        echo "$arch: $reported"
    else
        echo "$arch: cannot run here (no Rosetta); skipped the --version check"
    fi
done
otool -L -arch all "$exe" | awk '/^\t/ && $1 !~ /^\/(System\/Library|usr\/lib)\// { print; bad=1 } END { exit bad }' \
    || { echo "The app links a library outside macOS." >&2; exit 1; }

cp "$repo/LICENSE" "$stage/LICENSE.txt"
cp "$out/SDL2-$sdl_version/LICENSE.txt" "$stage/licenses/SDL2.txt" 2>/dev/null \
    || tar -xzOf "$out/SDL2-$sdl_version.tar.gz" "SDL2-$sdl_version/LICENSE.txt" >"$stage/licenses/SDL2.txt"
cp "$repo/third_party/nuked-opll/LICENSE" "$stage/licenses/Nuked-OPLL.txt"
cp "$repo/tools/release/README-macOS.txt" "$stage/README.txt"
cat >"$stage/THIRD-PARTY.txt" <<EOF
PSModern $version - third-party notices (macOS universal build)

PSModern is free software under the GNU General Public License version 3 or
later (LICENSE.txt).

Corresponding source:
  PSModern  https://github.com/carbiecarbie/psmodern/tree/$head (commit $head$suffix)

Credit (not shipped):
  redcode/Z80, a Z80 CPU core (LGPL-3.0-or-later, https://github.com/redcode/Z80),
  is the reference PSModern's tests compare the C++ code against, instruction
  by instruction. The app contains none of its code.

Built into PSModern.app:
  Nuked-OPLL, Yamaha YM2413 emulation, copyright (C) 2019-2023 Nuke.YKT.
  GPL version 2 or later (licenses/Nuked-OPLL.txt).
  https://github.com/nukeykt/Nuked-OPLL
  SDL $sdl_version, Simple DirectMedia Layer, zlib license (licenses/SDL2.txt),
  linked statically from https://www.libsdl.org/release/SDL2-$sdl_version.tar.gz
  (SHA-256 $sdl_sha256).

The C++ runtime (libc++) and every other library come with macOS.

No Phantasy Star data is included; bring your own cartridge dump.
EOF

(cd "$work" && ditto -c -k --norsrc --noextattr --noacl --keepParent "$name" "$zip.partial")
mv "$zip.partial" "$zip"
echo "Package: $zip"
echo "Built from commit $head$suffix"
