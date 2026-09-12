"""Colour themes for the skin. Geometry never changes, so every theme shares one keymap.

  official  photo-matched look (v1/v2)
  day       official + pure-white LCD glass, for Wabbitemu patched with the white LCD palette
  night     reversed polarity for low light: near-black body and caps, white legends,
            lightened 2nd/alpha colours, outlined keys, black glass for the black LCD palette

apply(name) mutates layout.PAL / CLASSES / STYLE in place; base_render, legends and
composite read those same dict objects, so call it before rendering.
"""
from layout import PAL, STYLE

THEMES = {
    "official": {},
    "day": {
        "pal": {"glass": (255, 255, 255)},
        "style": {"glass_grad": (1.0, 1.0), "texture_skip_glass": True},
    },
    "night": {
        "pal": {
            # body: everything near-black so nothing large glows
            "silver": (30, 31, 34), "navy": (20, 21, 24), "rail": (34, 35, 38), "lip": (10, 10, 12),
            "glass": (0, 0, 0), "glass_frame": (58, 60, 64),
            "title_ink": (175, 178, 184), "maker_ink": (140, 144, 150),
            # caps: dark; 2nd/alpha keep a deep tint so they are still findable by colour
            "k_top": (40, 42, 46), "k_dark": (30, 32, 36), "k_num": (52, 54, 58), "k_op": (40, 42, 46),
            "k_arrow": (46, 48, 52), "k_2nd": (28, 62, 132), "k_alpha": (26, 96, 52),
            # inks: pure white on the caps (ink_dark is also the arrow-chevron colour)
            "ink_white": (255, 255, 255), "ink_dark": (255, 255, 255),
            "ink_2nd_navy": (150, 200, 255), "ink_2nd_silver": (150, 200, 255),
            "ink_alpha_navy": (140, 232, 140), "ink_alpha_silver": (140, 232, 140),
            "ink_apps": (245, 175, 225),
            # outline colours
            "ol_key": (150, 152, 158), "ol_2nd": (120, 175, 255), "ol_alpha": (110, 220, 130),
        },
        "style": {
            "glass_grad": (1.0, 1.0), "texture_skip_glass": True, "rim": (0.12, 0.12),
            "outline": {"k_top": ("ol_key", 0.9), "k_dark": ("ol_key", 0.9), "k_num": ("ol_key", 0.9),
                        "k_op": ("ol_key", 0.9), "k_arrow": ("ol_key", 0.9),
                        "k_2nd": ("ol_2nd", 1.0), "k_alpha": ("ol_alpha", 1.0)},
            "outline_w": 0.8,
        },
    },
}


def apply(name):
    t = THEMES[name]
    PAL.update(t.get("pal", {}))
    STYLE.update(t.get("style", {}))
    return name
