import os
import sys

"""The game's own text fonts (clSpriteFont, 11 of them) drawing a string, each measured for stroke thickness:
game_fonts.py "Register a new car" out.png      (ARCADE_VIEW: the arcade dump with `main`, default build/arcade-view;
run from a dev tree with tools/ and tools/recomp on the path, like menupack.py)"""
root = os.environ.get("ARCADE_VIEW", "build/arcade-view")
os.environ.setdefault("WMMT_MAIN", root + "/main")
tree = os.environ.get("DEV_TREE", ".")
sys.path.insert(0, tree + "/tools")
sys.path.insert(0, tree + "/tools/recomp")
import numpy as np  # noqa: E402
import menupack as M  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

text = sys.argv[1] if len(sys.argv) > 1 else "Register a new car"
out = sys.argv[2] if len(sys.argv) > 2 else "game_fonts.png"
elf = M.Elf(root + "/main")
names = M.sprite_names(elf)
rows = []
for font in range(11):
    try:
        t = "".join(chr(ord(c) + 0xFEE0) if "!" <= c <= "~" else (chr(0x3000) if c == " " else c) for c in text) if font >= 8 else text  # (the name fonts hold the full-width forms)
        im = M.render_text(elf, names, root, font, t)
    except Exception:
        continue
    a = np.array(im)[..., 3] > 128
    edge = a & ~(np.roll(a, 1, 0) & np.roll(a, -1, 0) & np.roll(a, 1, 1) & np.roll(a, -1, 1))
    stroke = 2.0 * a.sum() / max(1, edge.sum())  # (area over perimeter: the stroke width in px)
    rows.append((font, stroke, im))
    print("font %d: %dx%d px, stroke about %.2f px" % (font, im.width, im.height, stroke))
H = sum(r[2].height + 18 for r in rows)
W = max(r[2].width for r in rows) + 10
sheet = Image.new("RGBA", (W, H), (110, 110, 110, 255))
d = ImageDraw.Draw(sheet)
y = 2
for font, stroke, im in rows:
    d.text((2, y), "font %d  stroke %.2f" % (font, stroke), fill=(255, 255, 0, 255))
    sheet.alpha_composite(im, (0, y + 14))
    y += im.height + 18
sheet.save(out)
best = max(rows, key=lambda r: r[1])
print("thickest: font %d" % best[0])
best[2].save(out.replace(".png", "_thickest.png"))
