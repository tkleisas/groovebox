"""Elektron-style UI mockup: 240x160 effective, 2x integer scale to 480x320."""
from PIL import Image, ImageDraw, ImageFont

W, H = 240, 160
BG, FG, DIM, ACCENT = (10, 10, 12), (235, 235, 235), (110, 110, 115), (120, 200, 255)

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)
f = ImageFont.load_default(16)   # blocky after 2x nearest-neighbor scale


def text(x, y, s, color=FG, invert=False):
    if invert:
        w = int(d.textlength(s, font=f))
        d.rectangle([x - 2, y - 1, x + w + 2, y + 15], fill=FG)
        d.text((x, y), s, fill=BG, font=f)
    else:
        d.text((x, y), s, fill=color, font=f)


def bar(x, y, w, frac, color=FG):
    d.rectangle([x, y + 5, x + w, y + 9], outline=DIM)
    d.rectangle([x, y + 5, x + int(w * frac), y + 9], fill=color)


# header bar (inverted)
text(4, 2, "SYNTH", invert=True)
text(60, 2, "SUBTRACT", color=DIM)
text(200, 2, "T1:64", color=DIM)
d.line([0, 20, W, 20], fill=DIM)

# parameter rows: name, value, bar; selected row inverted
params = [
    ("CUTOFF", 87, 0.68, True),
    ("RESO",   41, 0.32, False),
    ("ATTACK",  3, 0.02, False),
    ("DECAY",  55, 0.43, False),
    ("SUSTAIN", 70, 0.55, False),
    ("RELEASE", 24, 0.19, False),
]
y = 28
for name, val, frac, sel in params:
    if sel:
        d.rectangle([0, y - 2, W, y + 15], fill=FG)
        d.text((4, y), name, fill=BG, font=f)
        d.text((100, y), f"{val:3d}", fill=BG, font=f)
        d.rectangle([140, y + 4, 232, y + 9], fill=BG)
        d.rectangle([140, y + 4, 140 + int(92 * frac), y + 9], fill=(90, 90, 95))
    else:
        text(4, y, name, color=DIM)
        text(100, y, f"{val:3d}")
        bar(140, y, 92, frac)
    y += 19

# footer: encoder assignments
d.line([0, H - 18, W, H - 18], fill=DIM)
text(4,  H - 15, "CUT", color=DIM)
text(64, H - 15, "RES", color=DIM)
text(124, H - 15, "ATK", color=DIM)
text(184, H - 15, "REL", color=DIM)

img = img.resize((W * 2, H * 2), Image.NEAREST)
img.save("docs/ui_mockup.png")
print("wrote docs/ui_mockup.png")
