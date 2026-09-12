"""Pull the real TI-84 Plus reference photo and key rectangles out of the VTI v2.5 skin."""
import io, json, os, struct
from PIL import Image

SKN = os.path.expanduser(os.environ.get("SKN", "~/Documents/WabbitEmu/ti-84plus.skn"))
d = open(SKN, "rb").read()
assert d[:8] == b"VTIv2.5 "
lcd = struct.unpack_from("<4I", d, 0x98)
keys = [struct.unpack_from("<4I", d, 0xA8 + 16 * i) for i in range(50)]
jpg = d[d.find(b"\xff\xd8"):]
open("ref_photo.jpg", "wb").write(jpg)
im = Image.open(io.BytesIO(jpg)).convert("RGB")
json.dump({"size": im.size, "lcd": lcd, "keys": keys}, open("ref_rects.json", "w"), indent=1)
im.resize((im.width * 3, im.height * 3), Image.LANCZOS).save("ref_photo_3x.png")
print(im.size, lcd)
