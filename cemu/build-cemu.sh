#!/bin/bash
# Build the accessible CEmu from source and install it next to the ROM.
#
# Downloads, all in: qtbase + cmake + ninja is about 74 MB compressed (NOT the
# 350-600 MB that "brew install qt" would cost -- CEmu asks only for Qt6
# Core/DBus/Gui/Network/Widgets, all of which live in qtbase). The CEmu source
# tarball is 1.1 MB and the two submodules are 0.45 MB.
#
# Installs to ~/Documents/CEmu/CEmu Accessible.app and does NOT touch the stock
# /Applications/CEmu.app, which stays as a fallback.
set -euo pipefail

TAG=v2.0
ZDIS_SHA=7eb89e56d219bbca5ca5cd82c98dce69bd75004b
TIVARS_SHA=f627164d42e1b8757e70b12c8d8c7913a4496cf0

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HOME/Documents/CEmu/src"
DEST="$HOME/Documents/CEmu/CEmu Accessible.app"

command -v brew >/dev/null || { echo "Homebrew is required." >&2; exit 1; }

echo "==> toolchain"
brew list --formula 2>/dev/null | grep -qx qtbase || brew install qtbase
brew list --formula 2>/dev/null | grep -qx cmake  || brew install cmake
brew list --formula 2>/dev/null | grep -qx ninja  || brew install ninja
export PATH="/opt/homebrew/bin:$PATH"

echo "==> source"
mkdir -p "$SRC"; cd "$SRC"
[ -f "cemu-$TAG.tar.gz" ] || curl -fsSL -o "cemu-$TAG.tar.gz" \
  "https://codeload.github.com/CE-Programming/CEmu/tar.gz/refs/tags/$TAG"
rm -rf "CEmu-${TAG#v}"
tar xzf "cemu-$TAG.tar.gz"
cd "CEmu-${TAG#v}"

# The GitHub tarball omits submodules, so fetch them at the commits the tag pins.
fetch_sub() {
  local repo="$1" sha="$2" dest="$3" tmp
  tmp="$(mktemp -d)"
  curl -fsSL -o "$tmp/s.tar.gz" "https://codeload.github.com/$repo/tar.gz/$sha"
  tar xzf "$tmp/s.tar.gz" -C "$tmp"
  rm -rf "$dest"
  mv "$(find "$tmp" -maxdepth 1 -mindepth 1 -type d | head -1)" "$dest"
  rm -rf "$tmp"
}
fetch_sub CE-Programming/zdis     "$ZDIS_SHA"   core/debug/zdis
fetch_sub adriweb/tivars_lib_cpp  "$TIVARS_SHA" gui/qt/tivars_lib_cpp

echo "==> patches"
# qt6.11-compat: upstream targeted an older Qt. Needed to compile at all.
# accessible-cemu: the actual accessibility change (issue 3).
patch -p1 --forward < "$HERE/qt6.11-compat.patch"
patch -p1 --forward < "$HERE/accessible-cemu.patch"

echo "==> build"
cd "$SRC"
cmake -S "CEmu-${TAG#v}/gui/qt" -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qtbase
ninja -C build

echo "==> deploy"
rm -rf stage
cmake --install build --prefix "$SRC/stage" || true   # macdeployqt warns about brotli rpath
# macdeployqt leaves the bundle's signature broken on arm64, where a valid
# signature is mandatory, so re-sign ad hoc.
codesign --force --deep --sign - "$SRC/stage/CEmu.app"
codesign --verify "$SRC/stage/CEmu.app"

rm -rf "$DEST"
cp -R "$SRC/stage/CEmu.app" "$DEST"
/usr/libexec/PlistBuddy -c "Set :CFBundleName 'CEmu Accessible'" "$DEST/Contents/Info.plist" 2>/dev/null \
  || /usr/libexec/PlistBuddy -c "Add :CFBundleName string 'CEmu Accessible'" "$DEST/Contents/Info.plist"
codesign --force --deep --sign - "$DEST"

echo
echo "installed: $DEST"
echo "the stock /Applications/CEmu.app was not touched."
