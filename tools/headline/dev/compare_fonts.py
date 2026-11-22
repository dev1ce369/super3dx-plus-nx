import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from edt_profile import edt

D = os.environ.get("HEADLINE_FONTS", os.path.expanduser("~/headline_fonts")) + "/"
R = os.environ.get("ARCADE_VIEW", "build/arcade-view") + "/data/sprite-us/menu_p_topbar_mes_"
SET = sys.argv[2] if len(sys.argv) > 2 else "a"
if SET == "a":
    STR = ["Select transmission", "Card data confirmed (1/9)", "Choose a rival"]
    REAL = ["carsel_transmission", "cardcheck_1", "st_rival"]
else:  # (more letters: g, y, p, q, j, w, b ...)
    STR = ["Select game mode", "Your card needs to be renewed.", "Do you have a card?"]
    REAL = ["mode", "card_update", "card_have"]
cands = [("Libre Franklin Bold", "LibreFranklin-wght.ttf", 700),
         ("Public Sans Bold", "PublicSans-wght.ttf", 700),
         ("Public Sans SemiBold", "PublicSans-wght.ttf", 600),
         ("M PLUS Rounded 1c ExtraBold", "MPLUSRounded1c-ExtraBold.ttf", None),
         ("Liberation Sans Bold", "LiberationSans-Bold.ttf", None),
         ("Nimbus Sans Bold", "NimbusSans-Bold.otf", None)]
SP = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
SS = 3  # supersample

# the arcade's glow, measured on its title sprites (tools-wsl/prof.py): by distance in pixels from the white fill,
# the opacity and colour of the halo (a tight red falling to white, nothing past 6 px)
PROF_D = [0, 1, 2, 3, 4, 5, 6, 7]
PROF_A = [255, 207, 128, 73, 22, 5, 1, 0]
PROF_R = [254, 255, 255, 255, 255, 255, 255, 255]
PROF_G = [251, 104, 126, 182, 233, 250, 254, 255]
FILL = (254, 251, 251)
SIGMA, POWER, GAIN = 1.95, 0.83, 450.0

def font_at(fn, wght, size):
    f = ImageFont.truetype(D + fn, size)
    if wght:
        try:
            vals = []
            for ax in f.get_variation_axes():
                nm = ax["name"]
                nm = nm.decode() if isinstance(nm, bytes) else nm
                vals.append(wght if "eight" in nm else ax.get("default", ax["minimum"]))
            f.set_variation_by_axes(vals)
        except Exception as e:
            print("axes failed", fn, e)
    return f

def width(f, text):
    return sum(f.getlength(c) + SP * SS for c in text) - SP * SS

def mask(f, text, pad):
    w = int(width(f, text)) + 2 * pad * SS
    m = Image.new("L", (w, 40 * SS + 2 * pad * SS), 0)
    d = ImageDraw.Draw(m)
    x = pad * SS
    for c in text:
        d.text((x, pad * SS), c, font=f, fill=255)
        x += f.getlength(c) + SP * SS
    return m.resize((m.width // SS, m.height // SS), Image.LANCZOS)

def glow(m):
    """The arcade's halo, fitted to its title sprites (tools-wsl/fitglow.py): the letters' mask blurred (sigma 1.8), its opacity
    550 * blur ** 0.8 (clipped), and the halo's colour (255, 255 - opacity, 255 - opacity): saturated red where the glow is thick,
    paling to white where it thins. The white fill over it."""
    from PIL import ImageFilter
    a = np.array(m).astype(float) / 255
    b = np.array(m.filter(ImageFilter.GaussianBlur(SIGMA))).astype(float) / 255
    pa = np.clip(GAIN * b ** POWER, 0, 255)
    pg = 255 - pa
    out = np.zeros(a.shape + (4,))
    out[..., 0] = 255 * (1 - a) + FILL[0] * a
    out[..., 1] = pg * (1 - a) + FILL[1] * a
    out[..., 2] = pg * (1 - a) + FILL[2] * a
    out[..., 3] = a * 255 + pa * (1 - a)
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGBA")

reals = []
for n in REAL:
    im = Image.open(R + n + ".png").convert("RGBA")
    if n in ("carsel_transmission", "card_update"):
        im = im.crop((0, 0, im.width, 30))
    reals.append(im.crop(im.split()[3].getbbox()))
target = [r.width - 10 for r in reals]
rows = [("real", [Image.new("RGBA", (r.width + 6, r.height + 6), (0, 0, 0, 0)) for r in reals])]
for i, r in enumerate(reals):
    rows[0][1][i].alpha_composite(r, (3, 3))
for label, fn, wg in cands:
    try:
        lo, hi = 8.0, 60.0
        for _ in range(14):
            mid = (lo + hi) / 2
            f = font_at(fn, wg, int(round(mid * SS)))
            if width(f, STR[0]) / SS > target[0]: hi = mid
            else: lo = mid
        f = font_at(fn, wg, int(round(lo * SS)))
        rows.append((label + " (%.1fpx)" % lo, [glow(mask(f, s, 8)) for s in STR]))
    except Exception as e:
        print("failed", label, e)
colw = [max(r[1][i].width for r in rows) for i in range(3)]
rh = [max(im.height for im in r[1]) for r in rows]
W = sum(colw) + 12
H = sum(rh) + 4 * len(rows)
sheet = Image.new("RGBA", (W, H), (60, 60, 60, 255))
y = 0
d = ImageDraw.Draw(sheet)
for (label, imgs), h in zip(rows, rh):
    x = 0
    for i, im in enumerate(imgs):
        sheet.alpha_composite(im, (x, y)); x += colw[i] + 4
    d.text((W - 180, y + 2), label, fill=(255, 255, 0, 255))
    y += h + 4
sheet.save(os.environ.get("HEADLINE_OUT", ".") + "/glow_fonts_%s.png" % SET)
zs = []
for label, imgs in rows:
    im = imgs[2]
    zs.append(im.crop((im.width // 2, 0, im.width, im.height)))
zh = sum(z.height * 4 + 6 for z in zs)
zw = max(z.width for z in zs) * 4
zsheet = Image.new("RGBA", (zw, zh), (60, 60, 60, 255))
y = 0
for z in zs:
    z4 = z.resize((z.width * 4, z.height * 4), Image.NEAREST)
    zsheet.alpha_composite(z4, (0, y)); y += z4.height + 6
zsheet.save(os.environ.get("HEADLINE_OUT", ".") + "/glow_zoom_%s.png" % SET)
print("saved", sheet.size)
