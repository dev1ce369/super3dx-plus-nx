import os
import numpy as np
from PIL import Image
R = os.environ.get("ARCADE_VIEW", "build/arcade-view") + "/data/sprite-us/menu_p_topbar_mes_"

def edt(core, rad=14):
    """distance (px) from each pixel to the nearest core pixel, up to rad (brute force)."""
    h, w = core.shape
    d = np.full((h, w), float(rad + 1))
    pad = np.pad(core, rad)
    for dy in range(-rad, rad + 1):
        for dx in range(-rad, rad + 1):
            r = (dx * dx + dy * dy) ** 0.5
            if r > rad:
                continue
            sh = pad[rad + dy: rad + dy + h, rad + dx: rad + dx + w]
            d = np.where(sh & (r < d), r, d)
    return d

if __name__ == "__main__":
    for n in ["carsel_transmission", "cardcheck_1", "st_rival", "carsel_color", "mode"]:
        im = np.array(Image.open(R + n + ".png").convert("RGBA")).astype(float)
        if n == "carsel_transmission":
            im = im[:30]
        a = im[..., 3]
        rgb = im[..., :3]
        core = (a >= 250) & (rgb.min(-1) > 200)
        d = edt(core)
        print(n, "core px", core.sum())
        for lo in range(0, 13):
            mm = core if lo == 0 else (d > lo - 0.5) & (d <= lo + 0.5) & ~core
            if mm.sum() < 5:
                continue
            print("  d=%2d n=%5d alpha mean %5.1f [p10 %5.1f p90 %5.1f] rgb %s" % (lo, mm.sum(), a[mm].mean(), np.percentile(a[mm], 10), np.percentile(a[mm], 90), rgb[mm].mean(0).round(0)))
