"""Patch Wabbitemu 1.9.5.22's hard-coded LCD palette (black-and-white calcs).

Source (sputt/wabbitemu gui/guilcd.c, LCDProc WM_CREATE) builds two 256-entry palettes,
index 0 = pixel off ... 255 = pixel fully on:
    bi[i]         = (0x9E, 0xAB, 0x88) * (256 - i) / 255              (normal)
    contrastbi[i] = same R,G;  B = 0x20 - 0x88 * (256 - i) / 255       (contrast >= 37)
MSVC strength-reduced each channel to an accumulator: acc = start; each entry stores
acc / 255 (signed magic-number division) and then does acc -= step. Every start and step is a
plain 32-bit immediate, so a channel can be given any linear ramp v0 (off) -> v255 (on)
with start = 255*v0, step = v0 - v255; the division is then exact.

The shipped exe is UPX-packed: run `upx -d -o unpacked.exe Wabbitemu.exe` first.

--ini NAME renames the portable-mode settings file (registry.c CheckSetIsPortableMode reads
"<current dir>\\Wabbitemu.ini"; same-length names only). Two exes in one folder then keep
their own skin settings but share the auto-save state, which gui.c derives from the folder.

usage: patch_lcd.py UNPACKED.exe OUT.exe day|night|night-yellow [--ini NightMode.ini]
"""
import struct
import sys

# channel ramps (value for pixel off, value for pixel fully on)
PALETTES = {
    "original":     {"R": None, "G": None, "B": None},
    "day":          {"R": (255, 0), "G": (255, 0), "B": (255, 0)},        # black on white
    "night":        {"R": (0, 255), "G": (0, 255), "B": (0, 255)},        # white on black
    "night-yellow": {"R": (0, 255), "G": (0, 255), "B": (0, 0)},          # yellow on black
}

# (name, VA of instruction, expected instruction bytes, immediate offset, immediate size)
SITES = [
    ("G_start",   0x140069B20, "bf 00 ab 00 00",       1, 4),  # mov edi, 0xAB00   (both loops)
    ("R_start",   0x140069B25, "bb 00 9e 00 00",       1, 4),  # mov ebx, 0x9E00   (both loops)
    ("B_start1",  0x140069B47, "41 bb 00 88 00 00",    2, 4),  # mov r11d, 0x8800
    ("R_step1",   0x140069B9F, "41 81 e9 9e 00 00 00", 3, 4),  # sub r9d, 0x9E
    ("G_step1",   0x140069BBF, "41 81 ea ab 00 00 00", 3, 4),  # sub r10d, 0xAB
    ("B_step1",   0x140069BDF, "41 81 eb 88 00 00 00", 3, 4),  # sub r11d, 0x88
    ("B_start2",  0x140069C1C, "41 b8 00 78 ff ff",    2, 4),  # mov r8d, -0x8800
    ("R_step2",   0x140069C5E, "81 eb 9e 00 00 00",    2, 4),  # sub ebx, 0x9E
    ("G_step2",   0x140069C7A, "81 ef ab 00 00 00",    2, 4),  # sub edi, 0xAB
    ("B_step2",   0x140069C98, "41 81 c0 88 00 00 00", 3, 4),  # add r8d, 0x88
    ("B_off2",    0x140069CA9, "80 c2 20",             2, 1),  # add dl, 0x20
]


def va_to_off(b, va):
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec = struct.unpack_from("<H", b, pe + 6)[0]
    opt_size = struct.unpack_from("<H", b, pe + 20)[0]
    base = struct.unpack_from("<Q", b, pe + 24 + 24)[0]
    rva = va - base
    sec = pe + 24 + opt_size
    for i in range(nsec):
        _, vsize, vaddr, rsize, raw = struct.unpack_from("<8sIIII", b, sec + 40 * i)
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raw + rva - vaddr
    raise ValueError(hex(va))


def read_imm(b, name):
    for n, va, _, io, sz in SITES:
        if n == name:
            o = va_to_off(b, va) + io
            return struct.unpack_from("<i" if sz == 4 else "<b", b, o)[0]


def div255(acc):
    """The exact x86 sequence: imul 0x80808081; add edx,acc; sar edx,7; add edx,sign."""
    acc = (acc + 2**31) % 2**32 - 2**31
    hi = (acc * -2139062143) >> 32
    edx = ((hi + acc + 2**31) % 2**32) - 2**31
    edx >>= 7
    edx += 1 if edx < 0 else 0
    return edx & 0xFF


def emulate(b):
    """Rebuild both palettes from the immediates actually present in the file."""
    im = {n: read_imm(b, n) for n, *_ in SITES}
    bi, cbi = [], []
    r, g, bl = im["R_start"], im["G_start"], im["B_start1"]
    for _ in range(256):
        bi.append((div255(r), div255(g), div255(bl)))
        r -= im["R_step1"]; g -= im["G_step1"]; bl -= im["B_step1"]
    r, g, bl = im["R_start"], im["G_start"], im["B_start2"]
    for _ in range(256):
        cbi.append((div255(r), div255(g), (div255(bl) + im["B_off2"]) & 0xFF))
        r -= im["R_step2"]; g -= im["G_step2"]; bl += im["B_step2"]
    return bi, cbi


def patch(b, pal):
    b = bytearray(b)
    for name, va, want, io, sz in SITES:  # refuse unless every site is byte-exact original
        o = va_to_off(b, va)
        got = bytes(b[o:o + len(bytes.fromhex(want))])
        if got != bytes.fromhex(want):
            raise SystemExit(f"{name} at {va:#x}: expected {want}, found {got.hex(' ')} — wrong exe build")
    vals = {}
    for ch in "RGB":
        v0, v255 = pal[ch]
        vals[f"{ch}_start"] = 255 * v0
        vals[f"{ch}_step"] = v0 - v255
    new = {"R_start": vals["R_start"], "G_start": vals["G_start"], "B_start1": vals["B_start"],
           "R_step1": vals["R_step"], "G_step1": vals["G_step"], "B_step1": vals["B_step"],
           "B_start2": vals["B_start"], "R_step2": vals["R_step"], "G_step2": vals["G_step"],
           "B_step2": -vals["B_step"], "B_off2": 0}
    for name, va, _, io, sz in SITES:
        o = va_to_off(b, va) + io
        struct.pack_into("<i" if sz == 4 else "<b", b, o, new[name])
    return bytes(b)


def rename_ini(b, name):
    old = b"\\Wabbitemu.ini\x00"
    new = b"\\" + name.encode("ascii") + b"\x00"
    if len(new) != len(old):
        raise SystemExit(f"--ini name must be {len(old) - 2} characters")
    if b.count(old) != 1:
        raise SystemExit("portable INI string not found exactly once — wrong exe build")
    return b.replace(old, new)


if __name__ == "__main__":
    args = sys.argv[1:]
    ini = None
    if "--ini" in args:
        i = args.index("--ini")
        ini = args[i + 1]
        del args[i:i + 2]
    src, dst, which = args
    orig = open(src, "rb").read()
    bi0, _ = emulate(orig)
    assert bi0[0] == (158, 171, 136) and bi0[255] == (0, 0, 0), bi0[0]  # matches guilcd.c
    out = patch(orig, PALETTES[which])
    if ini:
        out = rename_ini(out, ini)
        print(f"  settings file renamed to {ini}")
    bi, cbi = emulate(out)
    want = [tuple(round(a + (z - a) * i / 255) for a, z in (PALETTES[which][c] for c in "RGB")) for i in range(256)]
    maxerr = max(abs(p - q) for x, y in zip(bi, want) for p, q in zip(x, y))
    maxerr_c = max(abs(p - q) for x, y in zip(cbi, want) for p, q in zip(x, y))
    changed = [i for i in range(len(orig)) if orig[i] != out[i]]
    print(f"{which}: off pixel {bi[0]}, on pixel {bi[255]}, mid {bi[128]}")
    print(f"  max deviation from linear ramp: normal {maxerr}, high-contrast {maxerr_c}")
    print(f"  {len(changed)} bytes changed: " + ", ".join(f"{i:#x}" for i in (min(changed), max(changed))))
    assert maxerr <= 1 and maxerr_c <= 1
    code = [i for i in changed if i < 0xA0000]  # palette code; the INI string lives in .rdata
    assert max(code) - min(code) < 0x200 and len(out) == len(orig)
    assert len(changed) - len(code) <= 13
    open(dst, "wb").write(out)
    print("  wrote", dst)
