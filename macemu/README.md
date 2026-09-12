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
* **View ▸ Screen Only** (⌘0) — hides the skin and fills the window with the
  screen, at whatever size the window is. This is the magnification mode.
* **View ▸ Actual Size / Fit to Screen** (⌘=, ⌘F).
* **Calculator ▸ Press ON**, **Screen Darker / Lighter** (sends 2nd ▲ / 2nd ▼).
* **File ▸ Reset Calculator** (⌘R) wipes RAM like pulling the batteries.

Unlike WabbitEmu the window has **no minimum size tied to the skin's pixel
size** — it scales from 160 px wide up to the full screen, so the same skin works
on a laptop and on a large monitor.

### Keyboard

| Mac key | calculator |
|---|---|
| `0`–`9` `.` `+` `-` `*` `/` `^` `(` `)` `,` | the matching key |
| Return | ENTER |
| Delete | DEL |
| Escape | CLEAR |
| ↑ ↓ ← → | arrow pad |
| F1–F5 | Y=, WINDOW, ZOOM, TRACE, GRAPH |
| Shift (held) | 2ND |
| Option (held) | ALPHA |
| Tab / ` | 2ND / ALPHA as taps |
| A–Z, space | ALPHA + that key, sent automatically |

Mouse clicks on the skin press whatever key the keymap says is under the pointer.

## Headless driver

```bash
./headless "~/Documents/WabbitEmu/TI-84 Plus.rom" --secs 1 --keys "ON,CLEAR,2,+,3,ENTER" --after 1.5
./headless ROM --keys ON --pgm screen.pgm --quiet      # 96x64 greyscale dump
./ti84mac --render out.png 560 1 0 "ON,CLEAR,MATH"     # full skin render, no window
```

`--render` draws through exactly the same code the window uses, which is how the
app is checked without a display.

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
