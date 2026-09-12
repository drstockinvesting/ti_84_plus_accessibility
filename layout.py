"""Single source of truth for TI-84 Plus skin geometry.

All positions are measured in *photo units*: pixels of the 325x602 reference photo
embedded in ~/Documents/WabbitEmu/ti-84plus.skn (see extract_ref.py). They are
mapped to skin pixels by `Layout(scale)`, so the skin and keymap can be produced
at any resolution from the same numbers.

Keymap encoding (verified in sputt/wabbitemu gui/guibuttons.c and gui/gui.c):
  button pixel = (R=0, G=group*16, B=bit*16); LCD = (255,0,0); background white.
"""
import math

# ---- body, in photo units -------------------------------------------------
BODY = (44, 26, 300, 574)          # outer case, symmetric about x=172
CENTER_X = 172
FACE = (53, 29, 291, 548)          # inner face (silver top + navy keypad)
SILVER_BOTTOM = 229                # y where the silver faceplate meets the navy keypad
ARROW_SURROUND = (237, 267, 43, 39)  # navy raised oval around the arrow pad: cx, cy, rx, ry
GLASS = (78.5, 64, 265.5, 190)     # LCD window (glass), pale green-gray
TITLE_Y = 35.5                     # "TI-84 Plus" vertical centre
MAKER_Y = 48.5                     # "TEXAS INSTRUMENTS" vertical centre

# ---- palette sampled from the reference photo (median of key-edge patches) ---
PAL = {
    "silver": (196, 196, 198),
    "navy": (40, 54, 70),
    "rail": (44, 60, 76),
    "lip": (20, 28, 36),
    "glass": (150, 162, 140),
    "glass_frame": (96, 104, 92),
    "title_ink": (40, 52, 66),
    "maker_ink": (80, 105, 125),
    # key caps
    "k_top": (200, 200, 203),      # Y= .. GRAPH
    "k_2nd": (118, 145, 190),
    "k_alpha": (104, 180, 133),
    "k_dark": (30, 40, 52),
    "k_num": (246, 246, 246),
    "k_op": (200, 200, 202),       # ÷ × − + and ENTER
    "k_arrow": (220, 220, 222),
    # legend inks
    "ink_white": (250, 250, 250),
    "ink_dark": (28, 34, 42),
    "ink_2nd_navy": (126, 160, 210),
    "ink_2nd_silver": (60, 105, 190),
    "ink_alpha_navy": (122, 196, 112),
    "ink_alpha_silver": (60, 135, 75),
    "ink_apps": (200, 132, 180),
}

# key classes: cap colour, legend ink, shape (w, h, top radius, bottom bulge)
CLASSES = {
    "top":   dict(cap="k_top",   ink="ink_dark",  w=32, h=11, r=4.0, bulge=3.0, legend=4.2),
    "2nd":   dict(cap="k_2nd",   ink="ink_white", w=31, h=18, r=6.0, bulge=4.0, legend=5.4),
    "alpha": dict(cap="k_alpha", ink="ink_white", w=31, h=18, r=6.0, bulge=4.0, legend=5.4),
    "dark":  dict(cap="k_dark",  ink="ink_white", w=32, h=18, r=6.0, bulge=4.0, legend=5.4),
    "outer": dict(cap="k_dark",  ink="ink_white", w=31, h=17, r=6.0, bulge=4.0, legend=5.4),
    "num":   dict(cap="k_num",   ink="ink_dark",  w=35, h=22, r=5.0, bulge=8.0, legend=8.6),
    "op":    dict(cap="k_op",    ink="ink_dark",  w=31, h=17, r=6.0, bulge=4.0, legend=8.4),
    "enter": dict(cap="k_op",    ink="ink_dark",  w=30, h=28, r=6.0, bulge=5.0, legend=5.4),
    "on":    dict(cap="k_dark",  ink="ink_white", w=31, h=19, r=6.0, bulge=4.0, legend=5.4),
}

# Render switches that themes.py may override (defaults = the photo-matched "official" look)
STYLE = {
    "glass_grad": (1.05, 0.97),   # top/bottom brightness of the LCD glass gradient
    "outline": None,              # {cap palette name: (outline palette name, alpha)} ring around caps
    "outline_w": 0.8,             # outline width, photo units
    "rim": (0.35, 0.55),          # specular top-rim alpha for dark / light caps
    "texture_skip_glass": False,  # keep the diffusion grain off the glass (flat LCD surround)
}

COLS = [92, 132, 172, 212, 252]

# (name, group, bit, cx, cy, class, legend, 2nd, alpha)
# group/bit from ti83pkeystrings[] (spec table); positions measured on the photo.
KEYS = [
    # row 0 — function keys on the silver faceplate
    ("Y=",     6, 4, COLS[0], 210, "top", "Y=",      "STAT PLOT", "F1"),
    ("WINDOW", 6, 3, COLS[1], 211, "top", "WINDOW",  "TBLSET",    "F2"),
    ("ZOOM",   6, 2, COLS[2], 212, "top", "ZOOM",    "FORMAT",    "F3"),
    ("TRACE",  6, 1, COLS[3], 211, "top", "TRACE",   "CALC",      "F4"),
    ("GRAPH",  6, 0, COLS[4], 210, "top", "GRAPH",   "TABLE",     "F5"),
    # row 1
    ("2ND",    6, 5, COLS[0], 257, "2nd",  "2ND",   None,  None),
    ("MODE",   6, 6, COLS[1], 259.5, "dark", "MODE", "QUIT", None),
    ("DEL",    6, 7, COLS[2], 261, "dark", "DEL",   "INS",   None),
    # row 2
    ("ALPHA",  5, 7, COLS[0], 285, "alpha", "ALPHA", "A-LOCK", None),
    ("XTθn",   4, 7, COLS[1], 288, "dark",  "X,T,θ,n", "LINK", None),
    ("STAT",   3, 7, COLS[2], 290, "dark",  "STAT",  "LIST",   None),
    # row 3
    ("MATH",   5, 6, COLS[0], 312, "outer", "MATH",  "TEST",  "A"),
    ("APPS",   4, 6, COLS[1], 315, "dark",  "APPS",  "ANGLE", "B"),
    ("PRGM",   3, 6, COLS[2], 318, "dark",  "PRGM",  "DRAW",  "C"),
    ("VARS",   2, 6, COLS[3], 315, "dark",  "VARS",  "DISTR", None),
    ("CLEAR",  1, 6, COLS[4], 312, "outer", "CLEAR", None,    None),
    # row 4
    ("x⁻¹",    5, 5, COLS[0], 338, "outer", "x^-1", "MATRIX", "D"),
    ("SIN",    4, 5, COLS[1], 343, "dark",  "SIN",  "SIN^-1", "E"),
    ("COS",    3, 5, COLS[2], 345, "dark",  "COS",  "COS^-1", "F"),
    ("TAN",    2, 5, COLS[3], 343, "dark",  "TAN",  "TAN^-1", "G"),
    ("^",      1, 5, COLS[4], 338, "outer", "^",    "π",      "H"),
    # row 5
    ("x²",     5, 4, COLS[0], 366, "outer", "x^2", "√",  "I"),
    (",",      4, 4, COLS[1], 371, "dark",  ",",   "EE", "J"),
    ("(",      3, 4, COLS[2], 373, "dark",  "(",   "{",  "K"),
    (")",      2, 4, COLS[3], 371, "dark",  ")",   "}",  "L"),
    ("÷",      1, 4, COLS[4], 366, "op",    "÷",   "e",  "M"),
    # row 6
    ("LOG",    5, 3, COLS[0], 396, "outer", "LOG", "10^x", "N"),
    ("7",      4, 3, COLS[1], 403, "num",   "7",   "u",    "O"),
    ("8",      3, 3, COLS[2], 405, "num",   "8",   "v",    "P"),
    ("9",      2, 3, COLS[3], 403, "num",   "9",   "w",    "Q"),
    ("×",      1, 3, COLS[4], 395, "op",    "×",   "[",    "R"),
    # row 7
    ("LN",     5, 2, COLS[0], 424, "outer", "LN",  "e^x",  "S"),
    ("4",      4, 2, COLS[1], 440, "num",   "4",   "L4",   "T"),
    ("5",      3, 2, COLS[2], 442, "num",   "5",   "L5",   "U"),
    ("6",      2, 2, COLS[3], 440, "num",   "6",   "L6",   "V"),
    ("−",      1, 2, COLS[4], 424, "op",    "−",   "]",    "W"),
    # row 8
    ("STO▶",   5, 1, COLS[0], 453, "outer", "STO>", "RCL", "X"),
    ("1",      4, 1, COLS[1], 478, "num",   "1",   "L1",   "Y"),
    ("2",      3, 1, COLS[2], 480, "num",   "2",   "L2",   "Z"),
    ("3",      2, 1, COLS[3], 478, "num",   "3",   "L3",   "θ"),
    ("+",      1, 1, COLS[4], 453, "op",    "+",   "MEM",  '"'),
    # row 9
    ("ON",     5, 0, COLS[0], 487, "on",    "ON",  "OFF",     None),
    ("0",      4, 0, COLS[1], 514, "num",   "0",   "CATALOG", "_space"),
    (".",      3, 0, COLS[2], 516, "num",   ".",   "i",       ":"),
    ("(-)",    2, 0, COLS[3], 514, "num",   "(-)", "ANS",     "?"),
    ("ENTER",  1, 0, 251,     490, "enter", "ENTER", "ENTRY", "SOLVE"),
]

# Arrow pad: hitbox rectangles (photo units, l,t,r,b) chosen to never overlap,
# plus the visual shapes drawn in base_render.py.
ARROW_CENTER = (237, 267)
ARROWS = [
    ("UP",    0, 3, (226.0, 236.0, 248.0, 264.0)),
    ("DOWN",  0, 0, (226.0, 270.0, 248.0, 298.0)),
    ("LEFT",  0, 1, (199.0, 254.0, 224.5, 280.0)),
    ("RIGHT", 0, 2, (249.5, 254.0, 275.0, 280.0)),
]

# the six blank slots in ti83pkeystrings[] — must never appear in the keymap
BLANK_SLOTS = {(2, 7), (1, 7), (0, 4), (0, 5), (0, 6), (0, 7)}


class Layout:
    """Maps photo units to skin pixels at a given scale."""

    def __init__(self, scale=6.5, margin=None):
        self.s = scale
        # margin and LCD padding scale with the skin, so Layout(3.25) is Layout(6.5) halved
        self.k = scale / 6.5
        margin = int(round(48 * self.k)) if margin is None else margin
        self.m = margin
        bx0, by0, bx1, by1 = BODY
        self.W = int(round((bx1 - bx0) * scale)) + 2 * margin
        self.H = int(round((by1 - by0) * scale)) + 2 * margin
        self.W += self.W % 2  # keep even so the body centre is an integer column
        self.cx = self.W // 2

    def x(self, u):
        return (u - CENTER_X) * self.s + self.cx

    def y(self, v):
        return (v - BODY[1]) * self.s + self.m

    def box(self, l, t, r, b):
        """Photo-unit box -> integer pixel box (l, t, r, b), r/b exclusive."""
        return (int(round(self.x(l))), int(round(self.y(t))),
                int(round(self.x(r))), int(round(self.y(b))))

    def lcd_rect(self):
        """Red LCD rectangle: an exact multiple of 96x64, centred in the glass."""
        gl, gt, gr, gb = self.box(*GLASS)
        gw, gh = gr - gl, gb - gt
        k = min(int(gw - 50 * self.k) // 96, int(gh - 40 * self.k) // 64)
        w, h = 96 * k, 64 * k
        l = (gl + gr) // 2 - w // 2
        t = (gt + gb) // 2 - h // 2
        return (l, t, l + w, t + h), k

    def keys(self):
        """All 50 keys with pixel hitbox rects (l, t, r, b exclusive)."""
        out = []
        for name, g, b, cx, cy, cls, legend, sec, alp in KEYS:
            c = CLASSES[cls]
            rect = self.box(cx - c["w"] / 2, cy - c["h"] / 2, cx + c["w"] / 2, cy + c["h"] / 2)
            out.append(dict(name=name, group=g, bit=b, cls=cls, legend=legend, sec=sec,
                            alp=alp, rect=rect, cx=self.x(cx), cy=self.y(cy)))
        for name, g, b, (l, t, r, bt) in ARROWS:
            out.append(dict(name=name, group=g, bit=b, cls="arrow", legend=None, sec=None,
                            alp=None, rect=self.box(l, t, r, bt),
                            cx=self.x((l + r) / 2), cy=self.y((t + bt) / 2)))
        return out


def keymap_color(group, bit):
    return (0, group * 16, bit * 16)


if __name__ == "__main__":
    L = Layout()
    print("canvas", L.W, L.H, "lcd", L.lcd_rect())
    ks = L.keys()
    print(len(ks), "keys")
    seen = {(k["group"], k["bit"]) for k in ks}
    assert len(seen) == 50 and not (seen & BLANK_SLOTS)
