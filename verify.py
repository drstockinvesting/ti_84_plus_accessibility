"""Simulate WabbitEmu 1.9.5's skin loader against a skin/keymap pair.

Mirrors sputt/wabbitemu:
  gui/guiskin.c   gui_frame_update (size check), FindLCDRect (0xFFFF0000 scan)
  gui/guibuttons.c FindButtonsRect (bit = B>>4, group = G>>4, skip white or R != 0)
  gui/gui.c       WM_LBUTTONDOWN (keymap pixel lookup: ignore if R == 0xFF)
and additionally checks the stricter rules from the spec.

usage: verify.py SKIN KEYMAP [SCALE] [OVERLAY_OUT]
"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from layout import Layout, keymap_color, BLANK_SLOTS, KEYS, ARROWS

skin_p, km_p = sys.argv[1], sys.argv[2]
scale = float(sys.argv[3]) if len(sys.argv) > 3 else 6.5
overlay_p = sys.argv[4] if len(sys.argv) > 4 else None
L = Layout(scale)
fails = []


def check(ok, msg):
    print(("PASS  " if ok else "FAIL  ") + msg)
    if not ok:
        fails.append(msg)


skin = Image.open(skin_p)
km_img = Image.open(km_p)
check(km_img.mode == "RGB", f"keymap mode is RGB (got {km_img.mode}) — no palette/alpha surprises")
km = np.asarray(km_img.convert("RGB")).astype(np.int32)
H, W, _ = km.shape

# 1. size check, as in gui_frame_update
check(skin.size == (W, H), f"skin {skin.size} == keymap {(W, H)}")
check(skin.size == (L.W, L.H), f"size matches layout {(L.W, L.H)}")

# 2. FindLCDRect, literal port (ARGB 0xFFFF0000 == opaque pure red)
red = (km[:, :, 0] == 255) & (km[:, :, 1] == 0) & (km[:, :, 2] == 0)
ys, xs = np.nonzero(red)
found = None
if len(ys):
    y0 = ys.min(); x0 = xs[ys == y0].min()  # raster-order first red pixel
    fx = x0
    while fx + 1 < W and red[y0, fx + 1]:
        fx += 1
    width = fx + 1 - x0
    fy = y0
    while fy + 1 < H and red[fy + 1, fx]:
        fy += 1
    height = fy + 1 - y0
    found = (x0, y0, x0 + width, y0 + height)
exp_lcd, k = L.lcd_rect()
check(found == exp_lcd, f"FindLCDRect -> {found}, expected {exp_lcd} ({k}x scale of 96x64)")
if found:
    l, t, r, b = found
    check(red[t:b, l:r].all() and red.sum() == (r - l) * (b - t),
          "LCD red region is one solid rectangle, no stray red pixels")

# 3. palette: only white, red, and the 50 exact key colours
flat = km.reshape(-1, 3)
colors, counts = np.unique(flat, axis=0, return_counts=True)
expected = {(255, 255, 255), (255, 0, 0)} | {keymap_color(g, b) for _, g, b, *_ in KEYS} \
    | {keymap_color(g, b) for _, g, b, _ in ARROWS}
got = {tuple(int(v) for v in c) for c in colors}
check(got == expected, f"exactly {len(expected)} distinct colours (got {len(got)}; extra {sorted(got - expected)[:5]})")

# 4. every non-white, non-red pixel must decode to a real key in the WM_LBUTTONDOWN path
clickable = (km[:, :, 0] != 255)
g_ = km[:, :, 1] >> 4
b_ = km[:, :, 2] >> 4
check(bool(((g_[clickable] < 7) & (b_[clickable] < 8)).all()),
      "every clickable pixel decodes to group<7, bit<8 (no out-of-range keypad writes)")

# 5. FindButtonsRect port + spec checks
rects = {}
for c in got - {(255, 255, 255), (255, 0, 0)}:
    m = (km[:, :, 0] == c[0]) & (km[:, :, 1] == c[1]) & (km[:, :, 2] == c[2])
    yy, xx = np.nonzero(m)
    l, t, r, b = xx.min(), yy.min(), xx.max() + 1, yy.max() + 1
    rects[(c[1] >> 4, c[2] >> 4)] = (l, t, r, b)
    check(m.sum() == (r - l) * (b - t), f"key g{c[1]>>4} b{c[2]>>4}: solid rectangle {(l, t, r, b)}")

want = {(k["group"], k["bit"]): (k["name"], k["rect"]) for k in L.keys()}
for gb, (name, rect) in sorted(want.items()):
    check(rects.get(gb) == tuple(rect), f"{name:7s} {gb} bbox {rects.get(gb)} == layout {tuple(rect)}")
check(not (set(rects) & BLANK_SLOTS), "none of the 6 blank (group,bit) slots are used")
check(len(rects) == 50, f"50 keys present (got {len(rects)})")

# 6. overlap / gap
items = list(rects.items()) + [((-1, -1), exp_lcd)]
min_gap = 10 ** 9
for i in range(len(items)):
    for j in range(i + 1, len(items)):
        (a, (al, at, ar, ab)), (bb, (bl, bt, br, bb_)) = items[i], items[j]
        dx = max(bl - ar, al - br, 0)
        dy = max(bt - ab, at - bb_, 0)
        gap = max(dx, dy)
        min_gap = min(min_gap, gap)
gap_min = max(4, int(8 * L.k))
check(min_gap >= gap_min, f"minimum white gap between regions = {min_gap}px (>= {gap_min})")

# 7. WabbitEmu window size (guisize.c GetMinMaxInfo): the window can never be smaller
#    than HALF the skin's source pixels, whatever the screen. On a 1080p laptop the
#    usable height is ~1000px (taskbar at 100-175% scaling), so H/2 must stay below it.
#    A 3658px-tall skin locked the window at 1829px: only the top half was reachable.
LAPTOP_WORK_H = 1000
check(H // 2 <= LAPTOP_WORK_H - 60,
      f"min window {W // 2}x{H // 2} fits a 1080p laptop (needs H/2 <= {LAPTOP_WORK_H - 60})")
ds = (253750.0 * 4 / (W * H)) ** 0.5
print(f"INFO  default display scale {ds:.3f} -> window ~{int(W*ds)}x{int(H*ds)}; "
      f"clamped to screen height, then resizable by corner drag down to {H // 2}px")

if overlay_p:
    ov = skin.convert("RGBA").copy()
    d = ImageDraw.Draw(ov)
    f = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 22)
    names = {(k["group"], k["bit"]): k["name"] for k in L.keys()}
    for gb, (l, t, r, b) in rects.items():
        d.rectangle([l, t, r - 1, b - 1], outline=(255, 0, 255, 255), width=3)
        d.text((l + 4, t + 2), f"{names.get(gb, '?')} {gb[0]},{gb[1]}", fill=(255, 0, 255, 255), font=f)
    if found:
        d.rectangle([found[0], found[1], found[2] - 1, found[3] - 1], outline=(255, 0, 0, 255), width=4)
    ov.save(overlay_p)
    print("wrote", overlay_p)

print(f"\n{'ALL CHECKS PASSED' if not fails else f'{len(fails)} FAILURE(S)'}")
sys.exit(1 if fails else 0)
