#!/usr/bin/env python3
"""Renders titles in the headline style to one PNG on dark grey: test_titles.py out.png ["Title one" "Title two" ...]"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from headline import render_headline  # noqa: E402
from PIL import Image  # noqa: E402

TITLES = ["Choose a car for the race", "Versus battle", "Local Battle", "Online Battle", "Please select an option", "Settings", "Game settings",
          "Control settings", "Graphics settings", "Select the tuning option", "Card settings", "Save this car?", "Select your starting point"]

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "headline_test.png"
    titles = sys.argv[2:] or TITLES
    imgs = [render_headline(t) for t in titles]
    sheet = Image.new("RGBA", (max(i.width for i in imgs) + 20, sum(i.height + 10 for i in imgs) + 10), (60, 60, 60, 255))
    y = 10
    for im in imgs:
        sheet.alpha_composite(im, (10, y))
        y += im.height + 10
    sheet.save(out)
    print("saved", out, sheet.size)
