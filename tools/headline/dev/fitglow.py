import os
import sys
import numpy as np
from PIL import Image, ImageFilter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from edt_profile import edt
R = os.environ.get("ARCADE_VIEW", "build/arcade-view") + "/data/sprite-us/menu_p_topbar_mes_"
data = []
for n in ["st_rival", "cardcheck_1", "mode", "carsel_color"]:
    im = np.array(Image.open(R + n + ".png").convert("RGBA")).astype(float)
    a, rgb = im[..., 3], im[..., :3]
    core = (a >= 250) & (rgb.min(-1) > 200)
    d = edt(core, 10)
    data.append((core, a, d < 8.5))
best = None
for sigma in np.arange(0.8, 3.01, 0.2):
    blurs = [np.array(Image.fromarray((c * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(float(sigma)))).astype(float) / 255 for c, _, _ in data]
    for p in (0.8, 1.0, 1.3, 1.6, 2.0):
        for c in np.arange(150, 900, 25):
            err = 0
            for (core, a, near), b in zip(data, blurs):
                pred = np.clip(c * b ** p, 0, 255)
                m = near & ~core
                err += np.abs(pred[m] - a[m]).mean()
            if best is None or err < best[0]:
                best = (err, sigma, p, c)
print("best mean abs alpha error %.2f at sigma %.1f power %.1f gain %d" % best)
