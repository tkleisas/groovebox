"""NSR-2 front-panel layout mockup, drawn to scale (6 px/mm).

Compact variant: 180x210mm panel, Choc grid @14mm pitch.
Adapted copy of tools/mockup_panel.py (NSR-1) -- originals untouched.
Layout per docs/nsr2-design.md: mic / screen / 4 soft keys / 4 encoders /
transport+MODE+</>/ 4x8 grid / SHIFT+spacebar+SHIFT, thumbstick and
vertical crossfader flanking the grid on the grid centerline.

Aesthetic: bokontep.gr Soviet-constructivist palette --
  --red #e31e24, --red-dark #a31217, --cream #f3ecdd, --cream-dim #e5d9bd,
  --black #0b0a07, --ink #17150f, terminal green #38ff6b.
Deep-red faceplate, cream/black keys, REC as the single red accent,
red brand star (site logo) filling the upper-left print zone.
"""
from PIL import Image, ImageDraw, ImageFont
import math
import os

# bokontep.gr palette
RED = (227, 30, 36)         # --red: accent (REC, star, LEDs)
RED_DARK = (163, 18, 23)    # --red-dark: faceplate
CREAM = (243, 236, 221)     # --cream: white keys, strong print
CREAM_DIM = (229, 217, 189) # --cream-dim: secondary print
BLACK = (11, 10, 7)         # --black: black keys, ink lines
INK = (23, 21, 15)          # --ink: dark key bodies
GREEN = (56, 255, 107)      # --green: terminal phosphor (screen)
GREEN_DIM = (20, 184, 74)   # --green-dim

S = 6  # px per mm
PW, PH = 180, 210          # panel size in mm
M = 30
W, H = PW * S + 2 * M, PH * S + 2 * M + 40

img = Image.new("RGB", (W, H), (30, 30, 34))
d = ImageDraw.Draw(img)


def _load_font(size):
    for name in ("DejaVuSans.ttf", "C:/Windows/Fonts/segoeui.ttf",
                 "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/cour.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


f_sm = _load_font(16)
f_md = _load_font(20)
f_badge = _load_font(24)


def mm(x, y):
    return M + x * S, M + y * S


def rect(x, y, w, h, fill, outline=None, label=None, font=None, lbl_fill=CREAM):
    d.rectangle([*mm(x, y), *mm(x + w, y + h)], fill=fill, outline=outline)
    if label:
        f = font or f_sm
        bb = d.textbbox((0, 0), label, font=f)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        cx, cy = mm(x + w / 2, y + h / 2)
        d.text((cx - tw / 2, cy - th / 2 - 1), label, fill=lbl_fill, font=f)


def label(text, x, y, color=CREAM_DIM, font=None):
    d.text(mm(x, y), text, fill=color, font=font or f_sm)


def knob(cx, cy, r, text):
    x, y = mm(cx, cy)
    d.ellipse([x - r * S, y - r * S, x + r * S, y + r * S], fill=INK,
              outline=BLACK, width=2)
    d.line([x, y, x, y - r * S + 4], fill=CREAM, width=3)
    if text:
        bb = d.textbbox((0, 0), text, font=f_sm)
        d.text((x - (bb[2] - bb[0]) / 2, y + (r + 1.5) * S), text, fill=CREAM_DIM, font=f_sm)


def choc_key(x, y, cap_fill, led=False, lbl=None, lbl_fill=CREAM_DIM, outline=BLACK):
    # 1u Choc key: 14mm pitch, 13.2mm cap
    rect(x + 0.4, y + 0.4, 13.2, 13.2, cap_fill, outline=outline)
    if led:
        lx, ly = mm(x + 7, y + 2.4)
        d.ellipse([lx - 2.5, ly - 2.5, lx + 2.5, ly + 2.5], fill=RED)
    if lbl:
        bb = d.textbbox((0, 0), lbl, font=f_sm)
        cx, cy = mm(x + 7, y + 7.8)
        d.text((cx - (bb[2] - bb[0]) / 2, cy - (bb[3] - bb[1]) / 2), lbl,
               fill=lbl_fill, font=f_sm)


def star(cx_mm, cy_mm, r_mm, fill, outline, width=3):
    cx, cy = mm(cx_mm, cy_mm)
    r_out, r_in = r_mm * S, r_mm * S * 0.42
    pts = []
    for i in range(10):
        ang = -math.pi / 2 + i * math.pi / 5
        r = r_out if i % 2 == 0 else r_in
        pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    d.polygon(pts, fill=fill, outline=outline, width=width)


# --- panel: deep-red faceplate ------------------------------------------
d.rectangle([*mm(0, 0), *mm(PW, PH)], fill=RED_DARK, outline=BLACK, width=3)

# mic hole, top edge center
mic_x, mic_y = mm(90, 5.5)
d.ellipse([mic_x - 4, mic_y - 4, mic_x + 4, mic_y + 4], fill=BLACK,
          outline=CREAM_DIM, width=2)
label("MIC", 93, 3.5, CREAM_DIM)

# upper-left print zone: brand star + identity (bokontep.gr logo mark)
star(19, 26, 9.5, RED, BLACK, width=4)
d.text(mm(8, 40), "ВОКОИТЕР", fill=CREAM, font=f_md)
d.text(mm(8, 47), "ΟΡΓΑΝΟ 2", fill=CREAM_DIM, font=f_sm)

# upper-right: model badge
d.text(mm(146, 4), "NSR-2", fill=CREAM, font=f_badge)

# screen: 4.0" 480x320 module (same panel as NSR-1), centered
rect(42, 10, 96, 68, BLACK, outline=INK)
rect(45, 13, 88, 59, (8, 15, 10))
d.text(mm(48, 16), "4.0\" 480x320 SPI TFT", fill=GREEN, font=f_md)
d.text(mm(48, 24), "params above encoders,", fill=GREEN_DIM, font=f_sm)
d.text(mm(48, 29), "menu labels above soft keys", fill=GREEN_DIM, font=f_sm)

# soft menu keys: 4, equally spaced over the grid width
label("SOFT KEYS", 6, 84)
for x, lbl in ((41, "S1"), (69, "S2"), (97, "S3"), (125, "S4")):
    rect(x, 83, 14, 11, INK, outline=BLACK, label=lbl, lbl_fill=CREAM)

# encoders: 4, aligned with the soft keys
label("ENCODERS", 6, 99)
for cx in (48, 76, 104, 132):
    knob(cx, 102, 7, "")

# transport line: PLAY STOP REC MODE < >  (REC = the one red accent)
label("TRANSPORT", 6, 116)
for x, lbl, fill in ((40.5, "PLAY", INK), (57.5, "STOP", INK), (74.5, "REC", RED),
                     (91.5, "MODE", INK), (108.5, "<", INK), (125.5, ">", INK)):
    rect(x, 115, 14, 10, fill, outline=BLACK, label=lbl, lbl_fill=CREAM)

# --- 4x8 Choc grid: cream steps (bottom rows), black accents (top rows) --
label("GRID - STEPS / PLAY / QWERTY", 34, 126.5)
GRID_X, GRID_Y = 34, 131
for r in range(4):
    for c in range(8):
        if r < 2:
            choc_key(GRID_X + c * 14, GRID_Y + r * 14, BLACK, led=True,
                     lbl=str(c + 1) if r == 0 else None, lbl_fill=CREAM_DIM,
                     outline=INK)
        else:
            choc_key(GRID_X + c * 14, GRID_Y + r * 14, CREAM, led=True,
                     outline=BLACK)

# shift / spacebar / shift row (one Choc switch under a ~3u cap)
rect(52.2, 191.4, 13.2, 13.2, BLACK, outline=INK, label="SH", lbl_fill=CREAM_DIM)
rect(69, 191.4, 42, 13.2, CREAM, outline=BLACK, label="SPACE", lbl_fill=BLACK)
rect(114.6, 191.4, 13.2, 13.2, BLACK, outline=INK, label="SH", lbl_fill=CREAM_DIM)

# pitch/mod thumbstick: left of the grid, on the grid centerline
jx, jy = mm(17, 159)
d.ellipse([jx - 10 * S, jy - 10 * S, jx + 10 * S, jy + 10 * S], fill=BLACK,
          outline=INK, width=2)
d.ellipse([jx - 4.5 * S, jy - 4.5 * S, jx + 4.5 * S, jy + 4.5 * S], fill=INK,
          outline=CREAM_DIM, width=2)
d.line([jx - 10 * S, jy, jx + 10 * S, jy], fill=INK, width=1)
d.line([jx, jy - 10 * S, jx, jy + 10 * S], fill=INK, width=1)
label("PITCH/MOD", 7, 172)

# crossfader: vertical, right of the grid, on the grid centerline
rect(160, 133, 6, 52, BLACK, outline=INK)
rect(158.5, 154, 9, 12, INK, outline=CREAM_DIM)
label("XFADE", 159, 188)

# rear-edge callouts
d.text((M, M + PH * S + 8),
       "rear edge: USB-C power in  |  3.5mm line out + line in (WM8731 codec)  |  DIN MIDI IN / OUT  |  power switch",
       fill=(140, 140, 140), font=f_sm)
d.text((M, M + PH * S + 26), f"panel {PW}x{PH}mm, 1mm = {S}px, Choc grid @14mm pitch",
       fill=(100, 100, 100), font=f_sm)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "panel_mockup_nsr2.png")
img.save(out)
print("wrote", out)
