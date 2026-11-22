"""Decode igImage pixel data to RGBA via Pillow's DDS reader.

The images carry a pixel-order enum (igImage field 6): 107 in the maps' textures, 100 in the cars', wheels', effects' and course arrows'. The
block-compressed images (DXT1/3/5) of order 100 hold their colours with red and blue exchanged (a "BGR" 565 endpoint pair): decoded as they are,
the Impreza's gold wheels come out blue, the course arrows cyan, and the yellow goal line, the neons and the auras take each other's colours (a
blue aura is red...). The arcade draws them the right way round, so the decode swaps them. (The raw RGB/RGBA images of order 100 are right as
stored.) Every packer used to patch these one by one (the wheels, the goal line, the neons, the minimap's icon, the traffic bodies); they get the
right colours from here now.
"""
import io
import struct

from PIL import Image

# igImage pixel formats seen so far -> DDS FourCC
FOURCC = {14: b"DXT1", 15: b"DXT3", 16: b"DXT5"}
BGR_ORDER = 100  # igImage field 6 of the images whose compressed colours are stored with red and blue exchanged


def to_pil(img_obj, data):
    w, h, fmt = img_obj[2].i32, img_obj[3].i32, img_obj[11].i32
    if fmt == 0:  # 8-bit luminance (the miniature course's roads, base_road_Fix)
        return Image.frombytes("L", (w, h), bytes(data[:w * h])).convert("RGBA")
    if fmt == 5:  # 24-bit RGB
        return Image.frombytes("RGB", (w, h), bytes(data[:w * h * 3])).convert("RGBA")
    if fmt == 7:  # 32-bit RGBA
        return Image.frombytes("RGBA", (w, h), bytes(data[:w * h * 4]))
    if fmt not in FOURCC:
        raise ValueError("unsupported igImage format %d" % fmt)
    block = 8 if fmt == 14 else 16
    top = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block
    hdr = struct.pack("<4s7I44x", b"DDS ", 124, 0x1007 | 0x80000, h, w, top, 0, 1)
    pf = struct.pack("<2I4s5I", 32, 0x4, FOURCC[fmt], 0, 0, 0, 0, 0)
    caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    im = Image.open(io.BytesIO(hdr + pf + caps + data[:top])).convert("RGBA")
    try:
        order = img_obj[6].i32
    except Exception:
        order = 0
    if order == BGR_ORDER:
        r, g, b, a = im.split()
        im = Image.merge("RGBA", (b, g, r, a))
    return im
