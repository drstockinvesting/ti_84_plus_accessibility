"""Procedural TI-84 Plus body + key caps (no text), drawn from layout.py.

Produces:
  out/base_full.png   full-resolution RGBA render (the geometry authority for the skin)
  out/cap_masks.npz   per-key cap masks, reused by composite.py
  ComfyUI input/ti84_layout.png  softened 832x1728 RGB copy for img2img
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from layout import (Layout, PAL, CLASSES, KEYS, ARROWS, ARROW_CENTER, BODY, FACE,
                    SILVER_BOTTOM, ARROW_SURROUND, GLASS, STYLE)

COMFY_INPUT = os.path.expanduser(os.environ.get(
    "COMFY_INPUT", "~/Documents/comfy/ComfyUI/input/ti84_layout.png"))
SS = 3  # supersampling factor for anti-aliased masks


def c(name, k=1.0):
    return np.array(PAL[name], np.float32) * k / 255.0


# ---------------------------------------------------------------- masks
def poly_mask(size, polys, ss=SS, blur=0):
    """Anti-aliased mask (float 0..1) from polygons given in pixel coords."""
    W, H = size
    im = Image.new("L", (W * ss, H * ss), 0)
    d = ImageDraw.Draw(im)
    for p in polys:
        d.polygon([(x * ss, y * ss) for x, y in p], fill=255)
    im = im.resize((W, H), Image.LANCZOS)
    if blur:
        im = im.filter(ImageFilter.GaussianBlur(blur))
    return np.asarray(im, np.float32) / 255.0


def rrect_pts(l, t, r, b, rt, rb=None, n=24):
    """Rounded rect polygon with separate top/bottom radii."""
    rb = rt if rb is None else rb
    pts = []
    for cx, cy, rad, a0 in [(r - rt, t + rt, rt, -90), (r - rb, b - rb, rb, 0),
                            (l + rb, b - rb, rb, 90), (l + rt, t + rt, rt, 180)]:
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    return pts


def cap_pts(l, t, w, h, r, bulge, n=40):
    """Key-cap outline: flat top with rounded corners, half-ellipse 'smile' bottom."""
    pts = []
    yb = t + h - bulge
    for i in range(n + 1):  # top-left corner
        a = math.radians(180 + 90 * i / n)
        pts.append((l + r + r * math.cos(a), t + r + r * math.sin(a)))
    for i in range(n + 1):  # top-right corner
        a = math.radians(270 + 90 * i / n)
        pts.append((l + w - r + r * math.cos(a), t + r + r * math.sin(a)))
    for i in range(2 * n + 1):  # bottom half-ellipse, right -> left
        a = math.pi * i / (2 * n)
        pts.append((l + w / 2 + (w / 2) * math.cos(a), yb + bulge * math.sin(a)))
    return pts


def ellipse_pts(cx, cy, rx, ry, n=120):
    return [(cx + rx * math.cos(2 * math.pi * i / n), cy + ry * math.sin(2 * math.pi * i / n)) for i in range(n)]


# ---------------------------------------------------------------- compositing
class Canvas:
    def __init__(self, W, H):
        self.W, self.H = W, H
        self.rgb = np.zeros((H, W, 3), np.float32)
        self.a = np.zeros((H, W), np.float32)

    def over(self, color, mask, box=None):
        """Paint colour (rgb triple or HxWx3 array) through mask (full-size or box-local)."""
        if box is None:
            l, t = 0, 0
            h, w = mask.shape
        else:
            l, t = box
            h, w = mask.shape
        l0, t0 = max(l, 0), max(t, 0)
        r0, b0 = min(l + w, self.W), min(t + h, self.H)
        m = mask[t0 - t:b0 - t, l0 - l:r0 - l][..., None]
        col = color if np.ndim(color) == 1 else color[t0 - t:b0 - t, l0 - l:r0 - l]
        self.rgb[t0:b0, l0:r0] = self.rgb[t0:b0, l0:r0] * (1 - m) + col * m
        self.a[t0:b0, l0:r0] = self.a[t0:b0, l0:r0] * (1 - m[..., 0]) + m[..., 0]

    def image(self):
        out = np.dstack([np.clip(self.rgb, 0, 1), np.clip(self.a, 0, 1)[..., None]])
        return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


def vgrad(h, w, top, bottom, gamma=1.0):
    t = np.linspace(0, 1, h, dtype=np.float32)[:, None, None] ** gamma
    return np.broadcast_to(top * (1 - t) + bottom * t, (h, w, 3)).copy()


def local_mask(pts, pad, ss=SS, blur=0):
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    l, t = int(math.floor(min(xs))) - pad, int(math.floor(min(ys))) - pad
    r, b = int(math.ceil(max(xs))) + pad, int(math.ceil(max(ys))) + pad
    m = poly_mask((r - l, b - t), [[(x - l, y - t) for x, y in pts]], ss, blur)
    return m, (l, t)


# ---------------------------------------------------------------- the render
def render(L):
    s = L.s
    cv = Canvas(L.W, L.H)
    W, H = L.W, L.H

    # --- outer case (rails) --------------------------------------------------
    bl, bt, br, bb = L.box(*BODY)
    case = rrect_pts(bl, bt, br, bb, rt=11 * s, rb=38 * s)
    # soft drop shadow under the whole calculator
    sh = poly_mask((W, H), [[(x, y + 4 * s * 0.25) for x, y in case]], blur=6 * s * 0.25)
    cv.over(np.zeros(3, np.float32), sh * 0.35)
    case_m = poly_mask((W, H), [case])
    xs = np.linspace(0, 1, W, dtype=np.float32)
    # rails: dark slate with glossy vertical bands (lighter band ~1/3 into each rail)
    rail = np.broadcast_to(c("rail"), (H, W, 3)).copy()
    xl = (np.arange(W) - bl) / (7 * s)
    xr = (br - np.arange(W)) / (7 * s)
    band = np.exp(-((xl - 0.45) ** 2) / 0.05) + np.exp(-((xr - 0.45) ** 2) / 0.05)
    edge = np.clip(np.minimum(xl, xr), 0, 1)
    rail *= (0.72 + 0.28 * edge)[None, :, None]
    rail += (band * 0.10)[None, :, None].astype(np.float32)
    cv.over(rail, case_m)

    # --- inner face ----------------------------------------------------------
    fl, ft, fr, fb = L.box(*FACE)
    face_pts = rrect_pts(fl, ft, fr, fb, rt=8 * s, rb=30 * s)
    face_m = poly_mask((W, H), [face_pts])
    # groove between rail and face
    groove = poly_mask((W, H), [rrect_pts(fl - 0.9 * s, ft - 0.6 * s, fr + 0.9 * s, fb + 0.9 * s,
                                          rt=8.8 * s, rb=30.8 * s)])
    cv.over(c("lip", 0.8), groove)

    # navy keypad face with gentle vertical falloff
    navy = vgrad(H, W, c("navy", 1.08), c("navy", 0.78))
    cv.over(navy, face_m)

    # silver faceplate: top of the face down to SILVER_BOTTOM
    sb = L.y(SILVER_BOTTOM)
    silver_pts = rrect_pts(fl, ft, fr, sb, rt=8 * s, rb=2 * s)
    silver_m = poly_mask((W, H), [silver_pts]) * face_m
    silver = vgrad(H, W, c("silver", 1.06), c("silver", 0.95))
    # faint horizontal brushed variation
    rng = np.random.default_rng(84)
    streak = rng.normal(0, 1, (1, W)).astype(np.float32)
    streak = np.asarray(Image.fromarray(streak).resize((W, 1), Image.BILINEAR))
    silver += (streak * 0.004)[..., None]
    cv.over(silver, silver_m)
    # soft shadow line where the silver plate steps down to the keypad
    step = poly_mask((W, H), [[(fl, sb), (fr, sb), (fr, sb + 1.2 * s), (fl, sb + 1.2 * s)]], blur=0.8 * s)
    cv.over(np.zeros(3, np.float32), step * face_m * 0.35)

    # arrow-pad surround: raised navy oval that bites into the silver plate
    acx, acy, arx, ary = ARROW_SURROUND
    sur = ellipse_pts(L.x(acx), L.y(acy), arx * s, ary * s)
    sur_m = poly_mask((W, H), [sur]) * face_m
    sur_sh = poly_mask((W, H), [[(x, y + 0.9 * s) for x, y in sur]], blur=1.2 * s) * face_m
    cv.over(np.zeros(3, np.float32), sur_sh * 0.30)
    cv.over(vgrad(H, W, c("navy", 1.14), c("navy", 0.86)), sur_m)
    rim = np.clip(sur_m - poly_mask((W, H), [[(x, y + 0.5 * s) for x, y in sur]]), 0, 1)
    cv.over(c("navy", 1.6), rim * 0.35)

    # chin / lower lip
    lip_top = L.y(541)
    lip = rrect_pts(fl, lip_top, fr, fb, rt=14 * s, rb=30 * s)
    lip_m = poly_mask((W, H), [lip]) * face_m
    cv.over(vgrad(H, W, c("lip", 1.3), c("lip", 0.9)), lip_m)
    lip_rim = np.clip(lip_m - poly_mask((W, H), [[(x, y + 0.7 * s) for x, y in lip]]), 0, 1)
    cv.over(c("navy", 1.6), lip_rim * 0.6)

    # --- LCD glass -----------------------------------------------------------
    gl, gt, gr, gb = L.box(*GLASS)
    frame = rrect_pts(gl - 1.6 * s, gt - 1.6 * s, gr + 1.6 * s, gb + 1.6 * s, rt=4 * s)
    cv.over(c("glass_frame"), poly_mask((W, H), [frame]))
    inner_sh = poly_mask((W, H), [rrect_pts(gl, gt, gr, gb, rt=2.5 * s)])
    g_top, g_bot = STYLE["glass_grad"]
    glass = vgrad(H, W, c("glass", g_top), c("glass", g_bot))
    cv.over(glass, inner_sh)
    # inset shadow along the top/left of the glass
    inset = np.clip(inner_sh - poly_mask((W, H), [rrect_pts(gl + 0.8 * s, gt + 1.2 * s, gr, gb, rt=2.5 * s)],
                                         blur=0.9 * s), 0, 1)
    cv.over(np.zeros(3, np.float32), inset * 0.35)

    # --- keys ------------------------------------------------------------------
    masks = {}
    for k in L.keys():
        if k["cls"] == "arrow":
            continue
        cl = CLASSES[k["cls"]]
        l, t, r, b = k["rect"]
        pts = cap_pts(l, t, r - l, b - t, cl["r"] * s, cl["bulge"] * s)
        masks[k["name"]] = draw_cap(cv, pts, cl["cap"], s, dark=cl["cap"] == "k_dark")

    # arrow pad: vertical 'bone' for UP/DOWN plus round LEFT/RIGHT keys
    ax, ay = L.x(ARROW_CENTER[0]), L.y(ARROW_CENTER[1])
    def bone_half(v):  # half-width profile: lobed ends, pinched waist, rounded tips
        return (12.8 - 4.6 * math.exp(-(v / 0.38) ** 2)) * s * math.sqrt(max(0.0, 1 - abs(v) ** 5))
    n = 160
    vs = [-1 + 2 * i / n for i in range(n + 1)]
    bone = [(ax + bone_half(v), ay + v * 31 * s) for v in vs] + \
           [(ax - bone_half(v), ay + v * 31 * s) for v in reversed(vs)]
    bone_m = draw_cap(cv, bone, "k_arrow", s, dark=False)
    masks["UP"] = masks["DOWN"] = bone_m
    for name, dx in (("LEFT", -24.5), ("RIGHT", 24.5)):
        pts = ellipse_pts(ax + dx * s, ay, 13.6 * s, 12.8 * s)
        masks[name] = draw_cap(cv, pts, "k_arrow", s, dark=False)

    return cv, masks


def draw_cap(cv, pts, cap, s, dark):
    """Molded key cap: drop shadow, side wall, domed top, specular rim, optional outline."""
    col = c(cap)
    pad = int(4 * s)
    m, (l, t) = local_mask(pts, pad)
    h, w = m.shape
    shift = lambda arr, dy: np.pad(arr, ((dy, 0), (0, 0)))[:h] if dy > 0 else arr
    # contact shadow on the face
    shadow = np.asarray(Image.fromarray((shift(m, int(1.3 * s)) * 255).astype(np.uint8))
                        .filter(ImageFilter.GaussianBlur(1.3 * s)), np.float32) / 255
    cv.over(np.zeros(3, np.float32), shadow * 0.55, (l, t))
    # side wall (visible below the cap top)
    wall = shift(m, int(1.1 * s))
    cv.over(col * (0.55 if not dark else 0.6), wall, (l, t))
    # cap top with vertical shading
    ys, xs = np.nonzero(m > 0.5)
    y0, y1 = ys.min(), ys.max()
    tt = np.clip((np.arange(h, dtype=np.float32) - y0) / max(1, y1 - y0), 0, 1)[:, None, None]
    top_k, bot_k = (1.35, 0.85) if dark else (1.05, 0.90)
    grad = col * (top_k * (1 - tt) + bot_k * tt)
    # subtle horizontal doming
    x0, x1 = xs.min(), xs.max()
    uu = (np.arange(w, dtype=np.float32) - (x0 + x1) / 2) / max(1, (x1 - x0) / 2)
    dome = (1 - 0.08 * np.clip(uu, -1, 1) ** 4)[None, :, None]
    grad = np.broadcast_to(grad, (h, w, 3)) * dome
    cv.over(grad.astype(np.float32), m, (l, t))
    # specular rim along the top edge
    rim = np.clip(m - shift(m, max(1, int(0.55 * s))), 0, 1)
    rim = np.asarray(Image.fromarray((rim * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.25 * s)),
                     np.float32) / 255
    hl = np.array([1, 1, 1], np.float32)
    cv.over(hl, rim * STYLE["rim"][0 if dark else 1], (l, t))
    # outline ring (night theme): the cap edge stays findable when the caps are dark
    ol = (STYLE["outline"] or {}).get(cap)
    if ol:
        wpx = max(1, int(round(STYLE["outline_w"] * s)))
        mi = Image.fromarray((m * 255).astype(np.uint8))
        inner = np.asarray(mi.filter(ImageFilter.MinFilter(2 * wpx + 1)), np.float32) / 255
        cv.over(c(ol[0]), np.clip(m - inner, 0, 1) * ol[1], (l, t))
    return (m, (l, t))


def soften_for_img2img(img, size=(832, 1728)):
    rgb = Image.new("RGB", img.size, (255, 255, 255))
    rgb.paste(img, mask=img.split()[3])
    small = rgb.resize(size, Image.LANCZOS)
    arr = np.asarray(small, np.float32)
    arr += np.random.default_rng(7).normal(0, 3.0, arr.shape)
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


if __name__ == "__main__":
    scale = float(sys.argv[1]) if len(sys.argv) > 1 else 6.5
    L = Layout(scale)
    cv, masks = render(L)
    img = cv.image()
    img.save("out/base_full.png")
    np.savez_compressed("out/cap_masks.npz", **{f"{k}__m": v[0] for k, v in masks.items()},
                        **{f"{k}__o": np.array(v[1]) for k, v in masks.items()})
    soften_for_img2img(img).save(COMFY_INPUT)
    img.resize((L.W // 4, L.H // 4), Image.LANCZOS).save("out/base_preview.png")
    print("wrote out/base_full.png", img.size, "and", COMFY_INPUT)
