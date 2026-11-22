import os
import numpy as np
from PIL import Image
R = os.environ.get("ARCADE_VIEW", "build/arcade-view") + "/data/sprite-us/menu_p_topbar_mes_"
im = np.array(Image.open(R + "st_rival.png").convert("RGBA")).astype(float)
a, rgb = im[..., 3], im[..., :3]
print("alpha bucket: n, mean rgb, min G, max G")
for lo in range(0, 256, 16):
    m = (a >= lo) & (a < lo + 16)
    if m.sum():
        g = rgb[m][:, 1]
        print("  %3d-%3d n=%5d rgb %s  G min %3.0f p10 %3.0f p50 %3.0f p90 %3.0f max %3.0f" % (lo, lo + 15, m.sum(), rgb[m].mean(0).round(0), g.min(), np.percentile(g, 10), np.percentile(g, 50), np.percentile(g, 90), g.max()))
# the most saturated red pixels
m = (a > 200) & (rgb[..., 1] < 40)
print("saturated red px (alpha>200, G<40):", m.sum(), "mean rgb", rgb[m].mean(0).round(0) if m.sum() else None, "mean alpha", a[m].mean() if m.sum() else None)
m = (a > 240) & (rgb[..., 1] < 100)
print("alpha>240 and G<100:", m.sum(), rgb[m].mean(0).round(0) if m.sum() else None)
