"""Crisp, code-rendered key legends and wordmarks (diffusion models garble text)."""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from layout import PAL, CLASSES, GLASS, TITLE_Y, MAKER_Y, CENTER_X, SILVER_BOTTOM

F_BOLD = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
F_UNI = "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
F_SERIF = "/System/Library/Fonts/Palatino.ttc"
CAP = 0.716  # Arial cap-height / em
SS = 2       # text supersampling for extra-clean edges

# Legends that differ from the photo on purpose: the real top-row, operator and
# ENTER keys print white on light gray, which is unreadable for a low-vision user.
LEGIBLE_LIGHT_KEYS = True


def font(path, px, index=0):
    return ImageFont.truetype(path, max(4, int(round(px))), index=index)


def pieces(text):
    """Split a legend into (string, is_superscript) runs. '^' starts a superscript."""
    if "^" not in text or text == "^":
        return [(text, False)]
    base, sup = text.split("^", 1)
    sup = sup.replace("-", "−")
    return [(base, False), (sup, True)]


def render_text(text, cap_px, color, path=F_BOLD, tracking=0.0):
    """Render a legend (with superscripts / special glyphs) to a tight RGBA tile."""
    px = cap_px / CAP * SS
    f_main = font(path, px)
    f_sup = font(path, px * 0.68)
    # thin glyphs that break up when WabbitEmu shrinks the skin: give them a stroke
    stroke = int(round(px * 0.035)) if any(ch in text for ch in "√{}[]") else 0
    special_tri = text.endswith(">")          # STO▶
    special_space = text == "_space"          # ␣ (alpha label on 0)
    if special_tri:
        text = text[:-1]
    runs = [] if special_space else pieces(text)
    # measure
    widths = []
    for s, sup in runs:
        f = f_sup if sup else f_main
        widths.append(f.getlength(s) + tracking * px * max(0, len(s) - 1))
    tri_w = px * 0.55 if special_tri else 0
    sp_w = px * 0.62 if special_space else 0
    W = int(sum(widths) + tri_w + sp_w + px * 0.3) + 4
    H = int(px * 1.6) + 4
    tile = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(tile)
    base_y = int(px * 1.15)  # baseline
    x = px * 0.15
    col = tuple(color) + (255,)
    for (s, sup), w in zip(runs, widths):
        f = f_sup if sup else f_main
        y = base_y - (cap_px * SS * 0.52 if sup else 0)
        if tracking:
            for ch in s:
                d.text((x, y), ch, font=f, fill=col, anchor="ls")
                x += f.getlength(ch) + tracking * px
        else:
            d.text((x, y), s, font=f, fill=col, anchor="ls", stroke_width=stroke, stroke_fill=col)
            x += w
    if special_tri:  # solid right-pointing triangle, cap-height tall
        ch = cap_px * SS * 0.78
        x0 = x + px * 0.08
        d.polygon([(x0, base_y - ch), (x0 + ch * 0.62, base_y - ch / 2), (x0, base_y)], fill=col)
    if special_space:  # ␣ bracket
        lw = max(2, int(px * 0.09))
        y1 = base_y
        d.line([(x, y1 - px * 0.22), (x, y1), (x + sp_w, y1), (x + sp_w, y1 - px * 0.22)], fill=col, width=lw)
    bbox = tile.getbbox()
    tile = tile.crop(bbox) if bbox else tile
    # remember where the cap line sits so callers can align baselines
    return tile.resize((max(1, tile.width // SS), max(1, tile.height // SS)), Image.LANCZOS), \
        (base_y - (bbox[1] if bbox else 0)) / SS


def paste_center(img, tile, cx, cy, max_w=None):
    if max_w and tile.width > max_w:  # condense horizontally, keep letter height
        tile = tile.resize((int(max_w), tile.height), Image.LANCZOS)
    img.alpha_composite(tile, (int(round(cx - tile.width / 2)), int(round(cy - tile.height / 2))))
    return tile.width


def paste_baseline(img, tile, base_off, x, baseline):
    img.alpha_composite(tile, (int(round(x)), int(round(baseline - base_off))))


def draw_legends(img, L):
    s = L.s
    for k in L.keys():
        l, t, r, b = k["rect"]
        w, h = r - l, b - t
        cls = k["cls"]
        if cls == "arrow":
            draw_chevron(img, k, L)
            continue
        cl = CLASSES[cls]
        on_silver = (k["cy"] < L.y(SILVER_BOTTOM))
        # ---- main legend on the cap
        ink = PAL[cl["ink"]]
        if cls in ("top", "op", "enter") and not LEGIBLE_LIGHT_KEYS:
            ink = PAL["ink_white"]
        if k["name"] == "APPS":
            ink = PAL["ink_apps"]
        face_h = h - cl["bulge"] * s * 0.5
        cy = t + face_h * (0.50 if cls != "num" else 0.50)
        cap = cl["legend"] * s
        if k["legend"] in ("÷", "×", "−", "+"):
            tile, _ = render_text(k["legend"], cap, ink, F_BOLD)
        elif k["legend"] == ",":
            tile, _ = render_text(",", cap * 1.1, ink)
            cy += cap * 0.1
        elif k["legend"] == ".":
            tile, _ = render_text(".", cap, ink)
            cy += cap * 0.35
        elif k["legend"] == "^":  # the real key shows a large caret
            tile, _ = render_text("^", cap * 1.9, ink)
            cy += cap * 0.25
        elif k["legend"] == "(-)":
            tile, _ = render_text("(−)", cap * 0.75, ink)
        else:
            tile, _ = render_text(k["legend"], cap, ink)
        paste_center(img, tile, (l + r) / 2, cy, max_w=w * 0.80)

        # ---- 2nd (blue) and alpha (green) legends above the cap
        sec, alp = k["sec"], k["alp"]
        if not sec and not alp:
            continue
        cap2 = 4.0 * s
        ink2 = PAL["ink_2nd_silver"] if on_silver else PAL["ink_2nd_navy"]
        inka = PAL["ink_alpha_silver"] if on_silver else PAL["ink_alpha_navy"]
        tiles = []
        if sec:
            tiles.append(render_text(sec, cap2, ink2))
        if alp:
            tiles.append(render_text(alp, cap2, inka, F_UNI if alp in ('"',) else F_BOLD))
        gap = 2.4 * s
        total = sum(tt.width for tt, _ in tiles) + gap * (len(tiles) - 1)
        max_total = 37 * s
        squeeze = min(1.0, max_total / total)
        baseline = t - 1.7 * s
        x = (l + r) / 2 - total * squeeze / 2
        for tt, off in tiles:
            if squeeze < 1:
                tt = tt.resize((max(1, int(tt.width * squeeze)), tt.height), Image.LANCZOS)
            paste_baseline(img, tt, off, x, baseline)
            x += tt.width + gap * squeeze


def draw_chevron(img, k, L):
    """Arrow glyphs on the arrow pad, drawn as solid anti-aliased chevrons."""
    s = L.s
    name = k["name"]
    ax, ay = k["cx"], k["cy"]
    size = 3.2 * s
    if name == "UP":
        ay = L.y(245)
    elif name == "DOWN":
        ay = L.y(289)
    pts = {"UP": [(0, -1), (1, 0.6), (-1, 0.6)], "DOWN": [(0, 1), (1, -0.6), (-1, -0.6)],
           "LEFT": [(-1, 0), (0.6, -1), (0.6, 1)], "RIGHT": [(1, 0), (-0.6, -1), (-0.6, 1)]}[name]
    ss = 4
    tw = int(size * 2.4)
    tile = Image.new("L", (tw * ss, tw * ss), 0)
    d = ImageDraw.Draw(tile)
    d.polygon([((tw / 2 + px * size) * ss, (tw / 2 + py * size) * ss) for px, py in pts], fill=255)
    tile = tile.resize((tw, tw), Image.LANCZOS)
    col = Image.new("RGBA", (tw, tw), tuple(PAL["ink_dark"]) + (255,))
    col.putalpha(tile.point(lambda v: int(v * 0.85)))
    img.alpha_composite(col, (int(ax - tw / 2), int(ay - tw / 2)))


def draw_wordmarks(img, L):
    s = L.s
    # "TI-84 Plus" — bold, navy, centred in the silver band above the glass
    tile, _ = render_text("TI-84 Plus", 8.6 * s, PAL["title_ink"], F_BOLD)
    paste_center(img, tile, L.x(CENTER_X), L.y(TITLE_Y), max_w=62 * s)
    # "TEXAS INSTRUMENTS" — serif small caps (large initials)
    ink = PAL["maker_ink"]
    big, small = 5.4 * s, 4.1 * s
    runs = [("T", big), ("EXAS", small), (" ", small), ("I", big), ("NSTRUMENTS", small)]
    rendered = []
    for txt, cap in runs:
        if txt == " ":
            rendered.append((None, cap * 0.9, 0))
            continue
        t, off = render_text(txt, cap, ink, F_SERIF, tracking=0.06)
        rendered.append((t, t.width, off))
    total = sum(w for _, w, _ in rendered) + 0.4 * s * (len(rendered) - 1)
    x = L.x(CENTER_X) - total / 2
    baseline = L.y(MAKER_Y) + big / 2
    for t, w, off in rendered:
        if t is not None:
            paste_baseline(img, t, off, x, baseline)
        x += w + 0.4 * s
