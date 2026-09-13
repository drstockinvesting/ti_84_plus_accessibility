#!/bin/bash
# Build the two double-clickable mode launchers next to the ROM in ~/Documents/CEmu/.
# They are osacompile applets rather than shell scripts so that double-clicking them
# in Finder does not open a Terminal window.
#
# The quit MUST be native AppleScript inside the applet: a nested `osascript` called
# from `do shell script` deadlocks the applet waiting on its own Automation consent.
set -euo pipefail
DEST="$HOME/Documents/CEmu"
TOOLS="$DEST/tools"
mkdir -p "$TOOLS"
cp "$(dirname "$0")/cemu-mode.sh"    "$TOOLS/cemu-mode.sh"
cp "$(dirname "$0")/cemu_fitsize.py" "$TOOLS/cemu_fitsize.py"
cp "$(dirname "$0")/../cemu_qini.py" "$TOOLS/cemu_qini.py"
chmod +x "$TOOLS/cemu-mode.sh"

build() {
  local name="$1" mode="$2" src
  src="$(mktemp -t launcher).applescript"
  cat > "$src" <<APPLESCRIPT
-- CEmu display-mode launcher: $mode
on run
	if application "CEmu" is running then
		tell application "CEmu" to quit
		repeat 40 times
			if not (application "CEmu" is running) then exit repeat
			delay 0.5
		end repeat
	end if
	set theScript to (POSIX path of (path to home folder)) & "Documents/CEmu/tools/cemu-mode.sh"
	do shell script quoted form of theScript & " $mode"
end run
APPLESCRIPT
  rm -rf "$DEST/$name.app"
  osacompile -o "$DEST/$name.app" "$src"
  rm -f "$src"
  echo "built: $DEST/$name.app ($mode)"
}

build "Big Calculator Screen" screen
build "Full Calculator" keypad
