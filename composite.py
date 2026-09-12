"""Assemble the final skin: geometry from base_render, material from ComfyUI (optional),
legends from legends.py.

usage: composite.py OUT.png [DIFFUSION.png|-] [SCALE] [--theme official|day|night] [--half]
  --half  Lanczos-halve the result (the 6.5 render -> 880x1829 laptop size, see README)
"""
import sys
import numpy as np
from PIL import Image, ImageFilter
from layout import Layout, STYLE, GLASS
from base_render import render
from legends import draw_legends, draw_wordmarks
import themes


def edge_guard(base, reach=10.0):
    """1 on flat areas, falling to 0 within ~reach px of the base render's edges.

    The diffusion pass redraws every key edge a pixel or two off the code edge; left
    in, its high-pass would print a second, dark outline around each key.
    """
    lum = Image.fromarray((base.mean(axis=2) * 255).astype(np.uint8))
    g = np.asarray(lum.filter(ImageFilter.FIND_EDGES), np.float32) / 255
    e = Image.fromarray(((g > 0.06) * 255).astype(np.uint8))
    e = e.filter(ImageFilter.MaxFilter(2 * int(reach * 0.6) + 1)).filter(ImageFilter.GaussianBlur(reach * 0.4))
    return 1 - np.asarray(e, np.float32)[..., None] / 255


def glass_keepout(L, shape):
    """1 everywhere except the LCD glass and its frame (0 there)."""
    s = L.s
    gl, gt, gr, gb = L.box(*GLASS)
    k = np.ones(shape[:2] + (1,), np.float32)
    k[max(0, int(gt - 2 * s)):int(gb + 2 * s), max(0, int(gl - 2 * s)):int(gr + 2 * s)] = 0
    return k


def detail_transfer(base, diff, strength=0.7, radius=3.0, clamp=0.05, reach=10.0, keep=None):
    """Add the diffusion image's fine texture (high-pass) to the crisp base.

    base, diff: float RGB arrays in 0..1, same size. Low frequencies (colour, layout)
    stay from the code render; the diffusion pass contributes plastic grain and
    surface detail only, so no key can drift in colour or position. Detail is
    luminance-only, amplitude-clamped, and kept away from the base's edges.
    """
    d_img = Image.fromarray((np.clip(diff, 0, 1) * 255).astype(np.uint8))
    low = np.asarray(d_img.filter(ImageFilter.GaussianBlur(radius)), np.float32) / 255
    hp = diff - low
    lum = hp.mean(axis=2, keepdims=True)  # luminance-only detail: no colour bleed
    lum = np.clip(lum, -clamp, clamp) * edge_guard(base, reach)
    if keep is not None:
        lum = lum * keep
    return np.clip(base + lum * strength, 0, 1)


def build(L, diffusion_path=None, strength=0.7):
    cv, masks = render(L)
    img = cv.image()
    if diffusion_path:
        diff = Image.open(diffusion_path).convert("RGB").resize(img.size, Image.LANCZOS)
        base = np.asarray(img, np.float32)[..., :3] / 255
        a = np.asarray(img, np.float32)[..., 3:] / 255
        keep = glass_keepout(L, base.shape) if STYLE["texture_skip_glass"] else None
        out = detail_transfer(base, np.asarray(diff, np.float32) / 255, strength, keep=keep)
        img = Image.fromarray((np.dstack([out, a]) * 255 + 0.5).astype(np.uint8), "RGBA")
    draw_wordmarks(img, L)
    draw_legends(img, L)
    return img


if __name__ == "__main__":
    args = sys.argv[1:]
    theme = "official"
    if "--theme" in args:
        i = args.index("--theme")
        theme = args[i + 1]
        del args[i:i + 2]
    half = "--half" in args
    args = [a for a in args if a != "--half"]
    themes.apply(theme)
    out = args[0]
    diff = args[1] if len(args) > 1 and args[1] != "-" else None
    scale = float(args[2]) if len(args) > 2 else 6.5
    L = Layout(scale)
    img = build(L, diff)
    if half:
        img = img.resize((img.width // 2, img.height // 2), Image.LANCZOS)
    img.save(out, optimize=True)
    print("wrote", out, img.size, "theme", theme)
