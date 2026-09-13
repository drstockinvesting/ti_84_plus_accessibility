#!/bin/bash
# cemu-deploy.sh -- build dir -> "CEmu Accessible.app", the whole sequence.
#
# The step that is easy to get wrong: the freshly linked binary in build-baseline
# still refers to Homebrew's Qt by absolute path. Copying just that binary into an
# already-deployed bundle loads TWO sets of Qt frameworks and the app dies with
# "Could not load the Qt platform plugin cocoa". macdeployqt has to run over the
# whole bundle every time, which is why this rebuilds the stage from scratch.
#
# macdeployqt also leaves the signature invalid on arm64, hence the re-sign; and a
# freshly ditto-ed bundle comes up with no accessibility window until it has been
# quit and relaunched once, hence the cycle at the end.
set -euo pipefail

BUILD="$HOME/Documents/CEmu/src/build-baseline"
STAGE="$HOME/Documents/CEmu/src/stage"
APP="$HOME/Documents/CEmu/CEmu Accessible.app"
# -x matches the process NAME exactly, so a shell loop that merely mentions the
# path does not count as a running CEmu (pgrep -f matches any command line).
PROC="CEmu"

wait_gone() {
  for _ in $(seq 1 40); do pgrep -x "$PROC" >/dev/null || return 0; sleep 0.5; done
  return 1
}

# CEmu's own quit is asynchronous: closeEvent() starts the calculator-image save and
# ignores the close, and only the save finishing calls close() again. A single Apple
# Event can therefore be answered with "User canceled" and nothing more, so re-send
# it while waiting. Deliberately NO pkill fallback: SIGTERM was tested and CEmu
# writes neither its config nor cemu_image.ce on it, so force-killing would throw
# away the student's in-progress work. Failing to switch is the better outcome.
quit_cemu() {
  for i in $(seq 1 10); do
    pgrep -x "$PROC" >/dev/null || return 0
    osascript -e 'tell application id "com.yourcompany.CEmu" to quit' >/dev/null 2>&1 || true
    for _ in $(seq 1 8); do
      pgrep -x "$PROC" >/dev/null || return 0
      sleep 0.5
    done
  done
  return 1
}

# Never SIGTERM: CEmu writes neither its config nor cemu_image.ce on it, so a force
# kill throws away the student's in-progress work.
quit_cemu || { echo "CEmu did not quit; nothing was deployed." >&2; exit 1; }
sleep 1

ninja -C "$BUILD"

rm -rf "$STAGE/CEmu.app"
ditto "$BUILD/CEmu.app" "$STAGE/CEmu.app"
macdeployqt "$STAGE/CEmu.app" >/dev/null
codesign --force --deep --sign - "$STAGE/CEmu.app" 2>/dev/null

rm -rf "$APP"
ditto "$STAGE/CEmu.app" "$APP"
codesign --force --deep --sign - "$APP" 2>/dev/null
codesign --verify --deep "$APP"

# First launch of a new bundle has no AX window; the quit-and-relaunch fixes it.
open "$APP"; sleep 5
quit_cemu || true
sleep 1
open "$APP"; sleep 5
pgrep -x "$PROC" >/dev/null && echo "deployed and running" || { echo "did not relaunch" >&2; exit 1; }
