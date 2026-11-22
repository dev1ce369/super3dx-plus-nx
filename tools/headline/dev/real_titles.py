import os
import numpy as np
from PIL import Image
root = os.environ.get("ARCADE_VIEW", "build/arcade-view") + "/data/sprite-us/"
names = ["card_have", "card_make", "carsel_maker", "carsel_transmission", "cardcheck_1", "mode", "name_1", "setting", "st_rival", "continue", "card_update", "carsel_car"]
sel = []
for n in names:
    im = Image.open(root + "menu_p_topbar_mes_" + n + ".png").convert("RGBA")
    bb = im.split()[3].getbbox()
    sel.append((n, im.crop(bb)))
im = Image.open(root + "menu_p_topbar_mes_carsel_color.png").convert("RGBA")
a = np.array(im).astype(float)
al = a[..., 3]
for lo, hi in ((250, 256), (180, 250), (100, 180), (30, 100), (5, 30)):
    m = (al >= lo) & (al < hi)
    if m.sum():
        print("alpha %d-%d: n=%d mean rgb %s" % (lo, hi, m.sum(), a[m][:, :3].mean(0).round(0)))
W = max(i.width for _, i in sel) * 2
H = sum(i.height * 2 + 4 for _, i in sel)
sheet = Image.new("RGBA", (W, H), (60, 60, 60, 255))
y = 0
for n, i in sel:
    i2 = i.resize((i.width * 2, i.height * 2), Image.LANCZOS)
    sheet.alpha_composite(i2, (0, y)); y += i2.height + 4
sheet.save(os.environ.get("HEADLINE_OUT", ".") + "/real_titles.png")
print("saved", sheet.size)
