#!/usr/bin/env python3
"""Add the title/legend/caption to the Blender exploded render."""
from __future__ import annotations

import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RENDERS = os.path.join(ROOT, "docs", "renders")
SRC = os.path.join(RENDERS, "nsr1_exploded.png")
DST = os.path.join(RENDERS, "nsr1_exploded_labeled.png")


def font(size):
    for name in ("DejaVuSans.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def main():
    image = Image.open(SRC).convert("RGBA")
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    f_title = font(58)
    f_sub = font(26)
    f_body = font(24)
    f_small = font(20)
    cream = (236, 231, 218, 255)
    muted = (188, 193, 202, 255)
    draw.rounded_rectangle((38, 28, 980, 170), radius=18, fill=(8, 12, 20, 195))
    draw.text((66, 54), "NSR-1  //  EXPLODED", font=f_title, fill=cream)
    draw.text((70, 124), "ВОКОИТЕР · ΟΡΓΑΝΟ 1 · rev B2 · 400 × 170 mm",
              font=f_sub, fill=muted)
    # Legend.
    x, y = 70, 1010
    draw.rounded_rectangle((x - 20, y - 22, x + 365, y + 130), radius=14,
                           fill=(8, 12, 20, 180))
    entries = (
        ((164, 28, 34), "Faceplate"),
        ((44, 158, 112), "Keyboard PCB + switches"),
        ((24, 88, 64), "Control PCB"),
        ((90, 96, 110), "Tray"),
    )
    for i, (color, label) in enumerate(entries):
        yy = y + i * 31
        draw.rounded_rectangle((x, yy, x + 22, yy + 22), radius=4,
                               fill=(*color, 255))
        draw.text((x + 34, yy - 3), label, font=f_body, fill=cream)
    draw.text((image.width - 470, image.height - 54),
              "validated with ElectroMechanicalDesigner", font=f_small, fill=muted)
    Image.alpha_composite(image, overlay).convert("RGB").save(DST, quality=95)
    print("wrote", DST)


main()
