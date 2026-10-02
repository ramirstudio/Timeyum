"""Builds ae/TimeyumBanner.bin from ae/TimeyumBanner.webp.

The .bin is what the .aex embeds: width and height as uint32 little endian, then raw BGRA
pixels (straight alpha). Requires Pillow:  python tools/make_banner.py [width]
"""
import os
import struct
import sys

from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, "..", "ae", "TimeyumBanner.webp")
dst = os.path.join(here, "..", "ae", "TimeyumBanner.bin")
width = int(sys.argv[1]) if len(sys.argv) > 1 else 800

im = Image.open(src).convert("RGBA")
height = round(im.height * width / im.width)
im = im.resize((width, height), Image.LANCZOS)
r, g, b, a = im.split()
bgra = Image.merge("RGBA", (b, g, r, a)).tobytes()
with open(dst, "wb") as f:
    f.write(struct.pack("<II", width, height))
    f.write(bgra)
print("%s: %dx%d, %d bytes" % (os.path.basename(dst), width, height, 8 + len(bgra)))
