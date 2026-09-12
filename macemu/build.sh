#!/bin/bash
# Build the emulator: a CLI test driver and a double-clickable .app bundle.
# No dependencies beyond the Xcode command line tools.
set -e
cd "$(dirname "$0")"
APP="build/TI-84 Plus.app"
CFLAGS="-O2 -Wall -Wextra"

echo "regenerating keytable.h from ../layout.py"
PY=${PYTHON:-/opt/homebrew/bin/python3.11}
"$PY" gen_keytable.py

echo "building headless + zextest"
cc $CFLAGS -o headless headless.c ti84.c z80.c
cc $CFLAGS -o zextest zextest.c z80.c

echo "building the app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cc $CFLAGS -fobjc-arc -framework Cocoa -framework QuartzCore -framework ImageIO \
   -o "$APP/Contents/MacOS/TI-84 Plus" main.m ti84.c z80.c
cp -f ti84mac.icns "$APP/Contents/Resources/" 2>/dev/null || true

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>TI-84 Plus</string>
    <key>CFBundleDisplayName</key><string>TI-84 Plus</string>
    <key>CFBundleExecutable</key><string>TI-84 Plus</string>
    <key>CFBundleIdentifier</key><string>local.ti84.macemu</string>
    <key>CFBundleVersion</key><string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleIconFile</key><string>ti84mac.icns</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSPrincipalClass</key><string>NSApplication</string>
</dict>
</plist>
PLIST

# ad-hoc signature: without it macOS refuses to keep the window key on some setups
codesign --force --deep -s - "$APP" 2>/dev/null || echo "(codesign skipped)"
echo "built $APP"
