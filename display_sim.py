"""What the student actually sees: WabbitEmu draws the skin with GDI+
InterpolationModeLowQuality at skin_scale = sqrt(253750*4/(w*h)) (guiskin.c),
shrunk further to fit the screen height. A screen magnifier then enlarges
those *screen* pixels. This renders a crop at a given window height, then
magnifies it with nearest-neighbour like a magnifier with smoothing off.

usage: display_sim.py SKIN OUT_PREFIX
"""
import sys
from PIL import Image

skin = Image.open(sys.argv[1]).convert("RGBA")
bg = Image.new("RGBA", skin.size, (128, 128, 128, 255))
bg.alpha_composite(skin)
W, H = skin.size
default = (253750.0 * 4 / (W * H)) ** 0.5
cases = {"default": default, "1080p_fit": 1000 / H, "1440p_fit": 1360 / H}
for name, sc in cases.items():
    size = (int(W * sc), int(H * sc))
    # worst case: point sampling (GDI+ low quality on big shrinks); best: box filter
    for mode, rs in (("nearest", Image.NEAREST), ("box", Image.BOX)):
        small = bg.resize(size, rs)
        # the 2nd/alpha + main legend region around SIN/COS/7-9, magnified 4x
        box = (int(W * 0.06 * sc), int(H * 0.52 * sc), int(W * 0.94 * sc), int(H * 0.70 * sc))
        crop = small.crop(box)
        crop.resize((crop.width * 4, crop.height * 4), Image.NEAREST).save(f"{sys.argv[2]}_{name}_{mode}.png")
    print(f"{name}: scale {sc:.3f} -> window {size}")
