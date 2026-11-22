#!/usr/bin/env python3
"""The arcade's red-glow headline style, for the port's own menu titles (the top bar's messages).

The arcade's titles ("Select brand", "Card data confirmed (1/9)") are ready-made pictures, one per string, in a heavy sans with a
red halo; none of the game's glyph fonts (clSpriteFont) is that style. This draws a string in the same look so a new title can be
baked into menu.bin (tools/menupack.py):

  * the letters: Libre Franklin Bold (SIL OFL, tools/headline/fonts/), 23.8 px, 0.9 px added between letters, white-pink fill (254, 251, 251);
  * the halo, fitted to the arcade's own title sprites (tools/headline/dev/fitglow.py): the letters' mask blurred (sigma 1.95 px),
    its opacity 450 * blur ** 0.83 (clipped at 255), and its colour (255, 255 - opacity, 255 - opacity): saturated red where the glow is
    thick, paling to white where it thins (the arcade's sprites keep the colour that way, green and blue = 255 - alpha).

  python3 tools/headline.py "Choose a car for the race" out.png      one title as a PNG
  from headline import render_headline; img = render_headline("Versus battle")    a PIL RGBA image
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.environ.get("HEADLINE_FONT", os.path.join(HERE, "headline", "fonts", "LibreFranklin[wght].ttf"))
WEIGHT = 700
SIZE = 23.8  # px: "Select transmission" as wide as the arcade's (268 px)
SPACING = 0.9  # px added after each letter
SIGMA, POWER, GAIN = 1.95, 0.83, 450.0
FILL = (254, 251, 251)
PAD = 8  # px of room around the letters for the halo
SS = 3  # supersampling of the letters


def _font(size):
    f = ImageFont.truetype(FONT, int(round(size * SS)))
    try:
        vals = []
        for ax in f.get_variation_axes():
            name = ax["name"]
            name = name.decode() if isinstance(name, bytes) else name
            vals.append(WEIGHT if "eight" in name else ax.get("default", ax["minimum"]))
        f.set_variation_by_axes(vals)
    except Exception:
        pass  # (a static font)
    return f


def _halo(m):
    """A letters' mask (L, 0..255) as the RGBA title: the white fill over the red halo."""
    a = np.array(m).astype(float) / 255
    b = np.array(m.filter(ImageFilter.GaussianBlur(SIGMA))).astype(float) / 255
    halo = np.clip(GAIN * b ** POWER, 0, 255)
    out = np.zeros(a.shape + (4,))
    out[..., 0] = 255 * (1 - a) + FILL[0] * a
    out[..., 1] = (255 - halo) * (1 - a) + FILL[1] * a
    out[..., 2] = (255 - halo) * (1 - a) + FILL[2] * a
    out[..., 3] = a * 255 + halo * (1 - a)
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGBA")


# The arcade's top bar message sprites (menu_p_topbar_mes_*.png) are 512 x 64 with the letters' left edge at x 6 and their
# baseline on row 24 (measured on "Select car", "Select brand" ...: tools-wsl measure_title.py); the bar draws the sprite at its
# own size where its template is, so a title made to this layout sits as the arcade's do.
TITLE_W, TITLE_H, TITLE_LEFT, TITLE_BASE = 512, 64, 5, 25


def render_title_sprite(text, size=SIZE, spacing=SPACING):
    """The title as the top bar's 512 x 64 message sprite."""
    f = _font(size)
    m = Image.new("L", (TITLE_W * SS, TITLE_H * SS), 0)
    d = ImageDraw.Draw(m)
    x = 0.0
    for c in text:
        d.text((x, TITLE_BASE * SS), c, font=f, fill=255, anchor="ls")
        x += f.getlength(c) + spacing * SS
    box = m.getbbox()
    shift = TITLE_LEFT * SS - (box[0] if box else 0)  # (the letters' left edge to x 6)
    if shift:
        m = m.transform(m.size, Image.AFFINE, (1, 0, -shift, 0, 1, 0))
    return _halo(m.resize((TITLE_W, TITLE_H), Image.LANCZOS))


def render_headline(text, size=SIZE, spacing=SPACING):
    """The title as an RGBA image: its letters and the red halo round them, PAD px of room on every side."""
    f = _font(size)
    w = int(sum(f.getlength(c) + spacing * SS for c in text) - spacing * SS) + 2 * PAD * SS
    h = int(round(size * 1.7 * SS)) + 2 * PAD * SS
    m = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(m)
    x = PAD * SS
    for c in text:
        d.text((x, PAD * SS), c, font=f, fill=255)
        x += f.getlength(c) + spacing * SS
    m = m.resize((w // SS, h // SS), Image.LANCZOS)
    box = m.getbbox()
    if box:  # (trimmed to the letters and the halo)
        m = m.crop((max(0, box[0] - PAD), max(0, box[1] - PAD), min(m.width, box[2] + PAD), min(m.height, box[3] + PAD)))
    a = np.array(m).astype(float) / 255
    b = np.array(m.filter(ImageFilter.GaussianBlur(SIGMA))).astype(float) / 255
    halo = np.clip(GAIN * b ** POWER, 0, 255)
    out = np.zeros(a.shape + (4,))
    out[..., 0] = 255 * (1 - a) + FILL[0] * a
    out[..., 1] = (255 - halo) * (1 - a) + FILL[1] * a
    out[..., 2] = (255 - halo) * (1 - a) + FILL[2] * a
    out[..., 3] = a * 255 + halo * (1 - a)
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGBA")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    render_headline(sys.argv[1]).save(sys.argv[2])
