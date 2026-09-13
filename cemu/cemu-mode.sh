#!/bin/bash
# cemu-mode.sh -- switch CEmu between the docked calculator and the big detached screen.
#
#   cemu-mode.sh keypad   full calculator: screen + keypad in one window, and resets
#                         the window to the size where the two panels line up
#   cemu-mode.sh screen   detached screen only, filling the display (~4x linear)
#   cemu-mode.sh toggle   flip to whichever one it is not in
#
# Why a script instead of just pressing the key: CEmu's only built-in control for this
# is the F11 shortcut, which on a Mac means holding fn+F11, and it has no menu item.
# This is for a student with Stargardt disease, so a one-click launcher beats a chord.
#
# CEmu rewrites its whole config on quit (save_on_close=true), so the config must only
# be edited while CEmu is NOT running. That is what the wait loop below is for.

set -euo pipefail

CFG="$HOME/Library/Preferences/cemu-dev/CEmu/cemu_config.ini"
# Prefer the accessible build (issue 3: the calculator scales as one object with the
# window). It is self-contained, so it keeps working if Homebrew's Qt is ever removed.
# Falls back to the stock App Store/prebuilt CEmu if the build is not installed.
APP="$HOME/Documents/CEmu/CEmu Accessible.app"
[ -d "$APP" ] || APP="/Applications/CEmu.app"
PROC="MacOS/CEmu"

[ -f "$CFG" ] || { echo "No CEmu config at $CFG" >&2; exit 1; }
[ -d "$APP" ] || { echo "CEmu not found at $APP" >&2; exit 1; }

# Read the current [Window] fullscreen value (0 = docked, 2 = screen only).
current() {
  python3 - "$CFG" <<'PY'
import re, sys
t = open(sys.argv[1]).read()
m = re.search(r'(?ms)^\[Window\]\n(.*?)(?=^\[|\Z)', t)
v = re.search(r'(?m)^fullscreen=(\d+)$', m.group(1)) if m else None
print(v.group(1) if v else '0')
PY
}

case "${1:-toggle}" in
  keypad|docked|calculator) WANT=0 ;;
  screen|big|lcd)           WANT=2 ;;
  toggle)                   [ "$(current)" = "2" ] && WANT=0 || WANT=2 ;;
  *) echo "usage: $(basename "$0") keypad|screen|toggle" >&2; exit 2 ;;
esac

# Quit CEmu and wait for it to actually exit, so its save-on-close finishes first.
# The AppleScript launchers may already have asked it to quit -- in that case
# "application is running" can go false while the process is still tearing down and
# still holding the config open, so always wait on the real process, not on who asked.
wait_gone() {
  for _ in $(seq 1 40); do
    pgrep -f "$PROC" >/dev/null || return 0
    sleep 0.5
  done
  return 1
}

if pgrep -f "$PROC" >/dev/null; then
  osascript -e 'tell application id "com.yourcompany.CEmu" to quit' >/dev/null 2>&1 || true
  # Deliberately NO pkill fallback: SIGTERM was tested and CEmu writes neither its
  # config nor cemu_image.ce on it, so force-killing would throw away the student's
  # in-progress work. Failing to switch modes is the better outcome.
  wait_gone || {
    echo "CEmu did not quit (it may be showing a dialog). Nothing was changed." >&2
    exit 1
  }
fi
wait_gone || { echo "CEmu still running; not touching the config." >&2; exit 1; }
# Let the dying process finish releasing the config and its LaunchServices registration,
# otherwise the relaunch below can be swallowed by the instance that is still exiting.
sleep 1

# Now it is safe to edit. Only [Window] fullscreen changes -- [Screen] has a key of the
# same name (the aspect-fill mode) that must be left alone.
python3 - "$CFG" "$WANT" <<'PY'
import re, sys
path, want = sys.argv[1], sys.argv[2]
t = open(path).read()
m = re.search(r'(?ms)^\[Window\]\n(.*?)(?=^\[|\Z)', t)
if not m:
    sys.exit('no [Window] section in ' + path)
sec = m.group(1)
new, n = re.subn(r'(?m)^fullscreen=\d+$', 'fullscreen=' + want, sec)
if n == 0:
    new = 'fullscreen=' + want + '\n' + sec
open(path, 'w').write(t[:m.start(1)] + new + t[m.end(1):])
PY

# Going back to the full calculator also restores the one window size where the
# two panels line up. CEmu scales them by different rules -- fixed-size screen
# pinned left, aspect-locked keypad centred horizontally but anchored to the top --
# so at any other size the calculator is visibly in two pieces (issue 3).
if [ "$WANT" = "0" ]; then
  python3 "$(dirname "$0")/cemu_fitsize.py" || echo "size reset skipped" >&2
fi

open "$APP"

# Confirm it actually came up; one retry covers a launch swallowed by the old instance.
for _ in $(seq 1 16); do
  pgrep -f "$PROC" >/dev/null && exit 0
  sleep 0.5
done
open "$APP"
for _ in $(seq 1 16); do
  pgrep -f "$PROC" >/dev/null && exit 0
  sleep 0.5
done
echo "CEmu did not relaunch." >&2
exit 1
