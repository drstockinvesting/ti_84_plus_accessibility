# TI-84 Plus emulator for macOS

A native Mac emulator for the TI-84 Plus (z80), written for this project so the
accessible skin, the keymap and real ROMs can be tested and used on a Mac —
WabbitEmu is Windows-only and there is no Wine on this machine.

It reads the **same skin + keymap PNG pair as WabbitEmu**, so everything built in
`../` works here unchanged:

* keymap pixel `(R=0, G=group*16, B=bit*16)` = that key's hit area
* pure red `(255,0,0)` = the LCD rectangle
* white = no key

## Build

```bash
./build.sh
```

Needs only the Xcode command line tools (`cc`) — no libraries, no downloads.
It produces:

| file | what it is |
|---|---|
| `build/TI-84 Plus.app` | the Mac app (double-clickable) |
| `headless` | CLI driver: boot a ROM, press keys, dump the screen |
| `zextest` | CP/M harness for the ZEX CPU test suites |

`build.sh` regenerates `keytable.h` from `../layout.py`, so the group/bit numbers
can never drift from the skin.

## Using the app

On first launch it looks for, and remembers:

* ROM: `~/Documents/WabbitEmu/TI-84 Plus.rom` (File ▸ Open ROM… to change)
* skins: `TI-84Plus-Day.png` / `TI-84Plus-Night.png` in
  `~/Documents/WabbitEmu/TI-84 Accessible/`, and `TI-84Plus-Official-v2.png`
* keymap: `TI-84 Accessible/TI-84Plus-Keymap.png`

The calculator's memory is saved to
`~/Library/Application Support/TI-84 Mac/calc.t84` when you quit and restored on
launch, so work in progress survives a restart. Press **ON** to wake the
calculator, exactly like the real thing.

### Menus

* **View ▸ Classic / Day / Night / Night Amber** (⌘1–⌘4) — swaps both the LCD
  palette and the skin. Day is black on pure white; Night is white on black;
  Night Amber is amber on black.
* **View ▸ Detached LCD Screen** (⌘0) — opens a second window showing nothing
  but the calculator's screen, mirroring the one on the skin. Resize it as large
  as you like; it keeps the LCD's 3:2 shape so the pixels stay square, and draws
  with nearest-neighbour sampling so they stay hard-edged at any size. It stays
  open between launches and follows the current theme. (This replaces the old
  "Screen Only" view, which took the calculator away while it was on.)
* **View ▸ Magnifier (4×)** (⌘Z) — see below.
* **View ▸ Actual Size / Fit to Screen** (⌘=, ⌘F) act on whichever window is in
  front. For the detached LCD, *Actual Size* snaps to the nearest whole-pixel
  scale, so every LCD pixel is exactly N×N screen pixels.
* **Calculator ▸ Press ON**, **Screen Darker / Lighter** (sends 2nd ▲ / 2nd ▼).
* **File ▸ Reset Calculator** (⌘R) wipes RAM like pulling the batteries.

Unlike WabbitEmu the window has **no minimum size tied to the skin's pixel
size** — it scales from 160 px wide up to the full screen, so the same skin works
on a laptop and on a large monitor.

### The click-and-hold magnifier

**Press and hold the mouse button for about half a second** anywhere on the
calculator and the window zooms to 4×, centred on the pointer. Hold again, or
press **Escape**, to go back. ⌘Z does the same thing from the keyboard.

While it is zoomed:

* moving the pointer pans the view, and **whatever is under the pointer stays
  under the pointer** — the zoom never slides the thing you are looking at away.
  Sweeping the pointer across the window reaches every part of the calculator,
  including the corners.
* clicks still press keys, at the place you are pointing.
* a bright yellow frame and the window title (`4× magnifier`) say it is on.

Because a *hold* means "magnify", a mouse click now presses its key on release
rather than on press, and holding the mouse on a key no longer auto-repeats it.
**Hold the key on the Mac keyboard instead** — arrows, DEL and the rest repeat
exactly as on a real calculator. Escape leaves the magnifier if it is on, and
otherwise sends CLEAR as before.

### Keyboard

| Mac key | calculator |
|---|---|
| `0`–`9` `.` `+` `-` `*` `/` `^` `(` `)` `,` | the matching key |
| Return | ENTER |
| Delete | DEL |
| Escape | leaves the magnifier, else CLEAR |
| ↑ ↓ ← → | arrow pad |
| F1–F5 | Y=, WINDOW, ZOOM, TRACE, GRAPH |
| Shift (held) | 2ND |
| Option (held) | ALPHA |
| Tab / ` | 2ND / ALPHA as taps |
| A–Z, space | ALPHA + that key, sent automatically |

Mouse clicks on the skin press whatever key the keymap says is under the pointer
(on release — see the magnifier above). Clicks in the detached LCD window do
nothing, but it takes the keyboard, so you can type into the calculator while
looking only at the big screen.

## Headless driver

```bash
./headless "~/Documents/WabbitEmu/TI-84 Plus.rom" --secs 1 --keys "ON,CLEAR,2,+,3,ENTER" --after 1.5
./headless ROM --keys ON --pgm screen.pgm --quiet      # 96x64 greyscale dump
./ti84mac --render out.png 560 1 0 "ON,CLEAR,MATH"     # full skin render, no window
./ti84mac --render lcd.png 768 1 1 "ON,2,+,3,ENTER"    # LCD only (what the detached window draws)
TI84_MAG=0.5,0.45 ./ti84mac --render mag.png 500 1 0   # what the 4x magnifier shows
```

`--render` draws through exactly the same code the window uses, which is how the
app is checked without a display.

`TI84_MAG="fx,fy"` (fractions of the window, 0..1) renders through the same
magnifier transform the window uses, so the zoom can be checked without a display
too.

`TI84_WATCH=1` prints a status line 4x/second; `TI84_HIST=1` prints a PC and port
histogram (how the boot problems in this emulator were found).

## What is emulated

* **Z80 core** (`z80.c`) — full documented instruction set plus the common
  undocumented ones (IXH/IXL, SLL, DDCB copy-back), correct flags including the
  X/Y bits, interrupt modes 0/1/2. Timing uses the decomposed machine-cycle model
  (4 t-states per opcode fetch, 3 per memory byte, 4 per I/O byte, documented
  internal cycles added at each site).
  **Verified: all 67 tests of `zexdoc` pass, 0 errors** (5.76 billion instructions).
* **TI-84 Plus hardware** (`ti84.c`) — 1 MB flash in 64 pages, 128 KB RAM,
  bank switching (ports 6/7 with the 0E/0F high bits, port 5 for the C000 bank),
  boot mapping (port 4 bit 0) and the boot-page-0 remap quirk, the AMD flash
  command state machine (program, sector and chip erase, autoselect, fast mode),
  flash locking, the T6A04 LCD (6- and 8-bit modes, cursor modes, Z scroll,
  contrast, the 60 t-state busy delay, 6-frame greyscale), the keypad matrix
  including the ON key's separate interrupt path, the standard timer interrupts,
  the three crystal timers, the real-time clock, CPU speed switching (6/15 MHz),
  and a "no cable attached" fake USB.

Behaviour was matched against sputt/wabbitemu (`core/core.c`,
`hardware/83phw.c`, `83psehw.c`, `lcd.c`, `keys.c`) so a ROM behaves the same in
both. Notably, ports no device answers must read as `0xFF`, not 0 — with 0 the
OS boots, draws its splash, and then never scans the keyboard.

## Not emulated

Link cable / file transfer (sending .8xp programs), real USB, sound, and the
assembly-program "no exec" memory protection. Games and assembly programs already
in a ROM image or a saved state run; there is no way to load a new one yet.

## Status

Boots OS 2.55MP from a real 1 MB TI-84 Plus dump, RAM-clear splash, home screen,
arithmetic, menus and greyscale all work; state save/restore round-trips.

Checked in the running app: mouse clicks on the skin type the right keys, the
click-and-hold magnifier toggles and stays centred on the pointer without typing
the key it was held on, the detached LCD mirrors the skin's screen live, and it
reopens with the app. Escape is the one path not exercised end to end — macOS
automation would not deliver a synthetic Escape to the app at all (confirmed: no
`keyDown` arrives), so that one is code-checked only; it matches on both the
character and key code 53.
