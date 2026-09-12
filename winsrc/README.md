# Accessible WabbitEmu — building from source

`accessible-wabbitemu.patch` adds the two Mac-emulator accessibility features to
WabbitEmu itself, and moves the Day / Night / Amber LCD colors out of the
byte-patched binary and into the source.

It applies cleanly to `sputt/wabbitemu` at `master`, touches **five files**, and
is +383 / −51 lines. It adds no new files, no new resources and no new menu
items, which is deliberate: `resource.h` and `Wabbitemu.rc` are where an
untested build breaks, so the patch stays out of them.

## What it changes

| File | Why |
|---|---|
| `gui/gui.h` | magnifier constants; five fields on `MainWindow_t` |
| `gui/gui.c` | the magnifier itself: paint transform, hold gesture, deferred keypress |
| `gui/guilcd.c` | `lcd_theme` palette; forwards mouse-move to the frame |
| `gui/registry.c` | the `lcd_theme` setting, and the command-line flavour override |
| `gui/registry.h` | three declarations for that override |

### 1. Click-and-hold magnifier (4×)

Hold the left button ~0.55 s anywhere on the calculator and the window zooms 4×
on the pointer. Hold again, or press **Escape**, to leave. A yellow frame marks
the zoomed state.

The transform is the pointer-centred one the Mac app uses. A client point `p`
is drawn at `d = Z·(p − c·k)` with `k = 1 − 1/Z`, so the point under the pointer
is drawn exactly where it already was. Three things follow, and the patch is
small because of them:

* **Hit testing needs no inverse.** The centre tracks the mouse, so at the
  moment of a click `p == c`, and WabbitEmu's existing keymap lookup — divide
  the client point by `skin_scale`, read the keymap pixel — is still correct
  while zoomed. Not one line of the key lookup changed.
* **Nothing outside the window can be exposed**, at any centre.
* **Every corner stays reachable** by sweeping the pointer.

The LCD is a child HWND rather than something the frame paints, so it is moved
to its magnified rect (`MagPlaceLCD`) rather than scaled by the frame's paint.
It already scales its contents to its client rect, so it magnifies for free.

**The one behavioural cost — mouse auto-repeat.** A hold now means "magnify",
so a click sends its key on the way *up*, not on the way down. Otherwise
holding over `5` to look at it would type a string of 5s before zooming. So
holding the *mouse* on a key no longer repeats it. Holding the key on the
**PC keyboard** still repeats normally — that is the replacement, and it is the
same trade-off the Mac build makes.

### 2. Detached LCD — already present, nothing to build

WabbitEmu has shipped this since long before this project (`gui/guidetached.c`,
**View ▸ Detached LCD**). `patch_lcd.py` only ever rewrote palette immediates,
so the three delivered exes already have it.

It also already picks up the Day/Night/Amber colors: `bi` and `contrastbi` are
shared globals in `guilcd.c` and every draw goes through one
`GetLCDColorPalette`, so the detached window cannot disagree with the main LCD.
**Verify this on the laptop rather than building anything for it.**

### 3. LCD themes as a setting, not a byte patch

`lcd_theme` — `0` stock green, `1` day (black on white), `2` night (white on
black), `3` night amber. Read once, at LCD creation.

This replaces `patch_lcd.py` entirely: no UPX unpacking, no hard-coded virtual
addresses, nothing that breaks when upstream recompiles. The accessibility
themes also keep their hue at high contrast, which the stock palette does not —
stock shifts blue down past contrast 37.

## Building

Needs **Visual Studio with the C++ desktop workload** (the solution is MSVC —
it will not build with MinGW), on Windows.

```
git clone https://github.com/sputt/wabbitemu.git
cd wabbitemu
git apply accessible-wabbitemu.patch
```

Then open `Wabbitemu.sln`, pick **Release / x64**, and build. The output exe
replaces the three patched ones.

If `git apply` complains, use `patch -p1 < accessible-wabbitemu.patch`.

## Deploying the three flavours

One exe, **one folder, one `Wabbitemu.ini`, three shortcuts** — so all three
flavours go on sharing a single saved calculator, exactly as the three patched
exes did. WabbitEmu derives the auto-save from the folder, not from the INI,
which is what makes this work.

The flavour comes from the command line:

```
Wabbitemu.exe -theme day   -skin TI-84Plus-Day.png
Wabbitemu.exe -theme night -skin TI-84Plus-Night.png
Wabbitemu.exe -theme amber -skin TI-84Plus-Night.png
```

`-theme` takes `green`, `day`, `night` or `amber` (or `0`–`3`). `-skin` and
`-keymap` take paths. Anything not given falls back to `Wabbitemu.ini`.

**The override is never written back on exit.** That is the point: the three
shortcuts share one INI, so without this the last window closed would redefine
the skin and theme for the other two. `SaveRegistrySettings` skips `skin_path`
and `keymap_path` whenever they came from the command line, and `lcd_theme` is
only ever read.

### Making the shortcuts

Put `Wabbitemu.ini` (in this folder) beside the exe together with the ROM, the
two skin PNGs and the keymap. Then, for each flavour: right-click the exe →
**Send to ▸ Desktop (create shortcut)**, then right-click the shortcut →
**Properties**, and append the arguments to **Target**, after the closing quote:

```
"C:\...\Wabbitemu.exe" -theme night -skin TI-84Plus-Night.png
```

Name them "TI-84 Day", "TI-84 Night", "TI-84 Amber". Leave **Start in** alone —
it must stay the folder holding the exe, or portable mode will not find the INI
and the shared save.

Amber deliberately uses the Night skin; only the LCD palette differs.

## Not verified

**None of this has been compiled or run.** This Mac has no MSVC and no Wine, so
the patch is reasoned from the source, checked for brace balance, and confirmed
to apply cleanly — nothing more. Expect to fix something on the first build.

The two most likely places, in order:

1. `MagCaptureLCDRect` assumes `hwndLCD` is a child of `hwndFrame`. If the
   magnified LCD lands in the wrong place, that mapping is why.
2. The `WM_MOUSEMOVE` forwarding in `LCDProc` is a convenience — it stops the
   zoom freezing when the pointer crosses the screen. If it misbehaves, delete
   that hunk; everything else keeps working.
3. The override reads `__argc` / `__targv`, the MSVC CRT's argument globals,
   which is what lets one code path serve both Unicode and ANSI builds. They
   are populated for `WinMain` — but if the arguments come back empty, that is
   the first thing to check.
