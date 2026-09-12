"""Build the WabbitEmu keymap from layout.py only — hard-edged, exact colours, no resampling."""
import sys
from PIL import Image, ImageDraw
from layout import Layout, keymap_color


def build(layout):
    im = Image.new("RGB", (layout.W, layout.H), (255, 255, 255))
    d = ImageDraw.Draw(im)
    (l, t, r, b), _ = layout.lcd_rect()
    d.rectangle([l, t, r - 1, b - 1], fill=(255, 0, 0))
    for k in layout.keys():
        l, t, r, b = k["rect"]
        d.rectangle([l, t, r - 1, b - 1], fill=keymap_color(k["group"], k["bit"]))
    return im


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "TI-84Plus-Official-Keymap.png"
    scale = float(sys.argv[2]) if len(sys.argv) > 2 else 6.5
    L = Layout(scale)
    build(L).save(out, optimize=True)
    print("wrote", out, L.W, L.H)
