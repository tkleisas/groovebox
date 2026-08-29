"""Front-panel layout mockup for the groovebox, drawn to scale (6 px/mm).
MX-switch version: 370x150mm panel, 19.05mm key pitch."""
from PIL import Image, ImageDraw, ImageFont

S = 6  # px per mm
PW, PH = 400, 150          # panel size in mm
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
f_badge = _load_font(30)


def mm(x, y):
    return M + x * S, M + y * S


def rect(x, y, w, h, fill, outline=None, label=None, font=None):
    d.rectangle([*mm(x, y), *mm(x + w, y + h)], fill=fill, outline=outline)
    if label:
        f = font or f_sm
        bb = d.textbbox((0, 0), label, font=f)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        cx, cy = mm(x + w / 2, y + h / 2)
        d.text((cx - tw / 2, cy - th / 2 - 1), label, fill=(220, 220, 220), font=f)


def label(text, x, y, color=(160, 160, 160), font=None):
    d.text(mm(x, y), text, fill=color, font=font or f_sm)


def knob(cx, cy, r, text):
    x, y = mm(cx, cy)
    d.ellipse([x - r * S, y - r * S, x + r * S, y + r * S], fill=(70, 70, 78),
              outline=(150, 150, 160), width=2)
    d.line([x, y, x, y - r * S + 4], fill=(240, 240, 240), width=3)
    if text:
        bb = d.textbbox((0, 0), text, font=f_sm)
        d.text((x - (bb[2] - bb[0]) / 2, y + (r + 1.5) * S), text, fill=(200, 200, 200), font=f_sm)


def mx_key(x, y, cap_fill, led=False, lbl=None):
    # 1u MX key: 19.05mm footprint, 18.1mm cap
    rect(x + 0.5, y + 0.5, 18.1, 18.1, cap_fill, outline=(140, 140, 150))
    if led:
        lx, ly = mm(x + 9.55, y + 2.5)
        d.ellipse([lx - 3, ly - 3, lx + 3, ly + 3], fill=(180, 40, 40))
    if lbl:
        bb = d.textbbox((0, 0), lbl, font=f_sm)
        cx, cy = mm(x + 9.55, y + 10)
        d.text((cx - (bb[2] - bb[0]) / 2, cy - (bb[3] - bb[1]) / 2), lbl,
               fill=(170, 170, 170), font=f_sm)


# --- panel -------------------------------------------------------------
d.rectangle([*mm(0, 0), *mm(PW, PH)], fill=(42, 42, 48), outline=(120, 120, 130), width=3)

# mic hole, top edge right of screen
mic_x, mic_y = mm(112, 5)
d.ellipse([mic_x - 4, mic_y - 4, mic_x + 4, mic_y + 4], fill=(25, 25, 28),
          outline=(140, 140, 150), width=2)
label("MIC", 109, 8.5, (140, 140, 140))

# screen: 4.0" 480x320 module, active area ~88x59mm
rect(8, 10, 96, 68, (20, 20, 24), outline=(100, 100, 110))
rect(11, 13, 88, 59, (12, 18, 28))
d.text(mm(14, 16), "4.0\" 480x320 SPI TFT", fill=(90, 160, 220), font=f_md)
d.text(mm(14, 24), "params above encoders,", fill=(70, 110, 160), font=f_sm)
d.text(mm(14, 29), "menu labels beside keys", fill=(70, 110, 160), font=f_sm)

# brand badge in the free zone
d.text(mm(150, 62), "ВОКОИТЕР - ΟΡΓΑΝΟ 1", fill=(230, 225, 215), font=f_badge)

# soft menu keys: vertical column right of the screen
label("SOFT KEYS", 108, 82.5)
for i, y in enumerate((14, 29, 44, 59)):
    rect(108, y, 14, 11, (55, 55, 62), outline=(140, 140, 150), label=f"S{i+1}")

# encoders under the screen (labels on screen)
for cx in (20, 45, 70, 95):
    knob(cx, 88, 7, "")

# pots: filter + ADSR
label("FILTER", 136, 17)
label("AMP ENVELOPE", 172, 17)
for cx, lbl in ((140, "CUT"), (158, "RES"), (176, "A"), (192, "D"), (208, "S"), (224, "R")):
    knob(cx, 32, 6, lbl)

# transport: top-right
label("TRANSPORT", 310, 9)
for x, lbl in ((310, "PLAY"), (327, "STOP"), (344, "REC")):
    rect(x, 14, 15, 10, (55, 55, 62), outline=(140, 140, 150), label=lbl)

# mode + prev/next below transport
for x, lbl in ((310, "MODE"), (328, "<"), (346, ">")):
    rect(x, 34, 16, 9, (55, 55, 62), outline=(140, 140, 150), label=lbl)

# data slider: horizontal, above keyboard right end
rect(310, 85, 62, 6, (25, 25, 28), outline=(110, 110, 120))
rect(328, 82.5, 13, 11, (80, 80, 90), outline=(160, 160, 170))
label("DATA SLIDER (velocity)", 310, 94)

# pitch/mod joystick: above SHIFT-L, bottom-left
jx, jy = mm(18, 105)
d.ellipse([jx - 10 * S, jy - 10 * S, jx + 10 * S, jy + 10 * S], fill=(30, 30, 35),
          outline=(110, 110, 120), width=2)
d.ellipse([jx - 4.5 * S, jy - 4.5 * S, jx + 4.5 * S, jy + 4.5 * S], fill=(80, 80, 90),
          outline=(160, 160, 170), width=2)
d.line([jx - 10 * S, jy, jx + 10 * S, jy], fill=(90, 90, 100), width=1)
d.line([jx, jy - 10 * S, jx, jy + 10 * S], fill=(90, 90, 100), width=1)
label("PITCH/MOD", 6, 116.5)

# --- MX keyboard: 16 whites at 19.05 pitch, 11 offset blacks -------------
KB_X, WY, BY = 36, 125, 105   # keyboard origin, white row y, black row y
label("KEYBOARD / STEPS", 160, 100)

# shifts at row ends (1u)
rect(KB_X - 27, WY + 0.5, 20, 18.1, (70, 60, 60), outline=(150, 130, 130), label="SH")
rect(KB_X + 16 * 19.05 + 7, WY + 0.5, 20, 18.1, (70, 60, 60), outline=(150, 130, 130), label="SH")

# whites (with in-switch LED dots on steps 1/5/9/13)
for i in range(16):
    mx_key(KB_X + i * 19.05, WY, (215, 212, 205), led=(i in (0, 4, 8, 12)), lbl=str(i + 1))

# blacks (raised row, offset half a key)
for i in (0, 1, 3, 4, 5, 7, 8, 10, 11, 12, 14):
    bx = KB_X + 19.05 * (i + 1) - 9.55
    mx_key(bx, BY, (18, 18, 22))

# rear-edge callouts
d.text((M, M + PH * S + 8),
       "rear edge: USB-C power in  |  3.5mm line out + line in (WM8731 codec)  |  DIN MIDI IN / OUT  |  power switch",
       fill=(140, 140, 140), font=f_sm)
d.text((M, M + PH * S + 26), f"panel {PW}x{PH}mm, 1mm = {S}px, MX keyboard @19.05mm pitch",
       fill=(100, 100, 100), font=f_sm)

img.save("docs/panel_mockup.png")
print("wrote docs/panel_mockup.png")
