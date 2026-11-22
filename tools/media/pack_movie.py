"""Pack JPEG frames and an Ogg Vorbis soundtrack into a .wmv for the SDL2 player (platform/sdl2/movie.c).

usage: pack_movie.py out.wmv soundtrack.ogg frames_dir fps

The file (little endian): "WMV1", u32 width, height, fps, frames, the soundtrack's offset and size, then the frames' u32 offsets
(frames + 1 of them, the last the file's end), then the soundtrack and the frames (baseline JPEG, 00001.jpg on).
"""
import os
import struct
import sys

out, ogg_path, frames_dir, fps = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
names = sorted(n for n in os.listdir(frames_dir) if n.endswith(".jpg"))
if not names:
    sys.exit("no frames in " + frames_dir)
ogg = open(ogg_path, "rb").read()
frames = [open(os.path.join(frames_dir, n), "rb").read() for n in names]
# the size from the first JPEG's start-of-frame marker
d = frames[0]
i = 2
w = h = 0
while i + 9 < len(d):
    if d[i] != 0xFF:
        i += 1
        continue
    m = d[i + 1]
    if m in (0xC0, 0xC1, 0xC2):
        h, w = struct.unpack(">HH", d[i + 5:i + 9])
        assert m != 0xC2, "a progressive JPEG: the player reads baseline only"
        break
    i += 2 + struct.unpack(">H", d[i + 2:i + 4])[0]
n = len(frames)
head = 4 + 7 * 4 + 4 * (n + 1)
aoff = head
off = aoff + len(ogg)
offsets = []
for f in frames:
    offsets.append(off)
    off += len(f)
offsets.append(off)
with open(out, "wb") as fp:
    fp.write(b"WMV1" + struct.pack("<7I", w, h, fps, n, aoff, len(ogg), 0))
    fp.write(struct.pack("<%dI" % (n + 1), *offsets))
    fp.write(ogg)
    for f in frames:
        fp.write(f)
print("%s: %dx%d, %d frames at %d fps, %.1f MB" % (out, w, h, n, fps, off / 1e6))
