#include "PanelViewNSR2.h"

#include <cmath>
#include <cstdio>

namespace gb {

namespace {

constexpr int S = PanelViewNSR2::kPxPerMm;
constexpr int mm(float v) { return int(v * S + 0.5f); }

constexpr uint16_t rgb(int r, int g, int b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// NSR-2 palette: deep red PCB, cream silkscreen, black/cream caps.
const uint16_t kPanel     = rgb(160, 24, 24);
const uint16_t kPanelEdge = rgb(104, 14, 14);
const uint16_t kBtn       = rgb(24, 22, 26);
const uint16_t kBtnEdge   = rgb(140, 140, 150);
const uint16_t kCream     = rgb(232, 224, 200);
const uint16_t kCreamDim  = rgb(190, 178, 152);
const uint16_t kBezel     = rgb(16, 16, 20);
const uint16_t kScreenBg  = rgb(0, 0, 0);
const uint16_t kKnob      = rgb(48, 48, 54);
const uint16_t kKnobEdge  = rgb(160, 160, 170);
const uint16_t kPointer   = rgb(240, 240, 240);
const uint16_t kBlackCap  = rgb(18, 18, 22);
const uint16_t kCreamCap  = rgb(215, 212, 205);
const uint16_t kRecCap    = rgb(210, 42, 32);
const uint16_t kTrack     = rgb(20, 20, 24);
const uint16_t kCap       = rgb(80, 80, 90);
const uint16_t kLedOn     = rgb(235, 70, 55);
const uint16_t kLedOff    = rgb(80, 28, 24);
const uint16_t kJoyRing   = rgb(24, 24, 28);
const uint16_t kActive    = rgb(245, 245, 245);

// ── geometry (mm, from nsr2/mockup_panel.py + nsr2-design.md) ───────
struct RectMm { float x, y, w, h; };

constexpr RectMm kScreenBezel = {40, 8, 100, 72};
constexpr RectMm kScreenInner = {43, 11, 94, 66};
constexpr float kSoftX0 = 41, kSoftPitch = 24, kSoftY = 84;
constexpr RectMm kSoftBtn = {0, 0, 16, 12};
constexpr float kEncCx0 = 49, kEncPitch = 24, kEncCy = 103, kEncR = 7;
constexpr RectMm kTransport[6] = {
    {40, 116, 14, 9}, {56, 116, 14, 9}, {72, 116, 14, 9},
    {88, 116, 14, 9}, {104, 116, 14, 9}, {120, 116, 14, 9}};
constexpr float kGridX0 = 34, kGridY0 = 131, kGridPitch = 14;
constexpr float kGridCap = 12;
constexpr float kJoyCx = 17, kJoyCy = 158, kJoyR = 9, kJoyKnobR = 4.5f;
constexpr RectMm kFaderTrack = {160, 133, 5, 52};
constexpr float kFaderCapW = 10, kFaderCapH = 12;
constexpr RectMm kShiftL = {34, 192, 20, 12};
constexpr RectMm kSpace  = {58, 192, 66, 12};
constexpr RectMm kShiftR = {128, 192, 20, 12};

bool inRect(int x, int y, RectMm r) {
    return x >= mm(r.x) && y >= mm(r.y) && x < mm(r.x + r.w) &&
           y < mm(r.y + r.h);
}

bool inCircle(int x, int y, float cx, float cy, float r) {
    const float dx = x - mm(cx), dy = y - mm(cy);
    const float rr = mm(r) + 2;
    return dx * dx + dy * dy <= rr * rr;
}

void fillCircle(Canvas565& c, int cx, int cy, int r, uint16_t col) {
    for (int dy = -r; dy <= r; ++dy) {
        const int half = int(std::sqrt(float(r * r - dy * dy)));
        c.fill(cx - half, cy + dy, 2 * half + 1, 1, col);
    }
}

void ringCircle(Canvas565& c, int cx, int cy, int r, int thick,
                uint16_t col) {
    const int r0 = r - thick;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const int d2 = dx * dx + dy * dy;
            if (d2 <= r * r && d2 >= r0 * r0) c.pixel(cx + dx, cy + dy, col);
        }
}

void star(Canvas565& c, int cx, int cy, int rOut, uint16_t col) {
    // 5-point star outline (silkscreen badge)
    for (int i = 0; i < 5; ++i) {
        const float a0 = -3.14159265f / 2 + i * 2 * 3.14159265f / 5;
        const float a1 = a0 + 2 * 3.14159265f / 5;
        const float am = (a0 + a1) / 2;
        const int x0 = cx + int(rOut * std::cos(a0));
        const int y0 = cy + int(rOut * std::sin(a0));
        const int xm = cx + int(rOut * 0.42f * std::cos(am));
        const int ym = cy + int(rOut * 0.42f * std::sin(am));
        const int x1 = cx + int(rOut * std::cos(a1));
        const int y1 = cy + int(rOut * std::sin(a1));
        c.line(x0, y0, xm, ym, col);
        c.line(xm, ym, x1, y1, col);
    }
}

void button(Canvas565& c, Font5x7& f, RectMm r, const char* lbl,
            bool pressed, bool active, uint16_t fill = kBtn) {
    const bool inv = pressed;
    const uint16_t fg = inv ? kPanel : kCream;
    c.fill(mm(r.x), mm(r.y), mm(r.w), mm(r.h), inv ? kActive : fill);
    c.frame(mm(r.x), mm(r.y), mm(r.w), mm(r.h),
            active ? kActive : kBtnEdge);
    if (lbl && *lbl) {
        const int tw = Canvas565::textWidth(lbl, 2);
        c.text(f, mm(r.x) + (mm(r.w) - tw) / 2,
               mm(r.y) + (mm(r.h) - 14) / 2, 2, lbl, fg);
    }
}

void sectionLabel(Canvas565& c, Font5x7& f, float xMm, float yMm,
                  const char* s) {
    c.text(f, mm(xMm), mm(yMm), 2, s, kCreamDim);
}

} // namespace

// ── hit testing ─────────────────────────────────────────────────────

PanelView::Hit PanelViewNSR2::hitTest(int x, int y) {
    using HT = PanelView;
    // grid rows 0..3 (keys 0..31)
    for (int i = 0; i < 32; ++i) {
        const float kx = kGridX0 + (i % 8) * kGridPitch + 1;
        const float ky = kGridY0 + (i / 8) * kGridPitch + 1;
        if (inRect(x, y, {kx, ky, kGridCap, kGridCap}))
            return {HT::kHitKey, kN2Grid0 + i};
    }
    if (inRect(x, y, kShiftL)) return {HT::kHitKey, kN2ShiftL};
    if (inRect(x, y, kSpace))  return {HT::kHitKey, kN2Space};
    if (inRect(x, y, kShiftR)) return {HT::kHitKey, kN2ShiftR};
    static const int kTrKey[6] = {kN2Play, kN2Stop, kN2Rec,
                                  kN2Mode, kN2Prev, kN2Next};
    for (int i = 0; i < 6; ++i)
        if (inRect(x, y, kTransport[i])) return {HT::kHitKey, kTrKey[i]};
    for (int i = 0; i < 4; ++i)
        if (inRect(x, y, {kSoftX0 + i * kSoftPitch, kSoftY, kSoftBtn.w,
                          kSoftBtn.h}))
            return {HT::kHitKey, kN2Soft1 + i};
    for (int i = 0; i < 4; ++i)
        if (inCircle(x, y, kEncCx0 + i * kEncPitch, kEncCy, kEncR))
            return {HT::kHitEncoder, i};
    if (inRect(x, y, {kFaderTrack.x - 2, kFaderTrack.y - 3,
                      kFaderTrack.w + 4, kFaderTrack.h + 6}))
        return {HT::kHitSlider, 0}; // crossfader = analog ch 6
    if (inCircle(x, y, kJoyCx, kJoyCy, kJoyR)) return {HT::kHitJoy, 0};
    return {};
}

// ── render ──────────────────────────────────────────────────────────

void PanelViewNSR2::render(Canvas565& c, Font5x7& font,
                           const PanelState& st,
                           const uint16_t* screen) {
    c.fill(0, 0, kW, kH, kPanel);
    c.frame(0, 0, kW, kH, kPanelEdge);
    c.frame(1, 1, kW - 2, kH - 2, kPanelEdge);

    const bool actKey = st.activeType == PanelView::kHitKey;
    const bool actEnc = st.activeType == PanelView::kHitEncoder;
    const bool actSld = st.activeType == PanelView::kHitSlider;
    const bool actJoy = st.activeType == PanelView::kHitJoy;

    // mic hole + model tag
    fillCircle(c, mm(90), mm(5), 4 * S / 2 + 2, kBezel);
    ringCircle(c, mm(90), mm(5), 4 * S / 2 + 2, 1, kBtnEdge);
    c.text(font, mm(86), mm(9), 1, "MIC", kCreamDim);
    c.text(font, mm(160), mm(10), 2, "NSR-2", kCream);

    // star badge + brand (mixed-script, via the font's Cyrillic+Greek)
    star(c, mm(22), mm(27), mm(9), kCream);
    c.text(font, mm(6), mm(42), 2, "ВОКОИТЕР", kCream);
    c.text(font, mm(6), mm(50), 2, "ΟΡΓΑΝΟ 2", kCream);

    // screen bezel + stretched device framebuffer
    c.fill(mm(kScreenBezel.x), mm(kScreenBezel.y), mm(kScreenBezel.w),
           mm(kScreenBezel.h), kBezel);
    c.frame(mm(kScreenBezel.x), mm(kScreenBezel.y), mm(kScreenBezel.w),
            mm(kScreenBezel.h), kBtnEdge);
    const int ix = mm(kScreenInner.x), iy = mm(kScreenInner.y);
    const int iw = mm(kScreenInner.w), ih = mm(kScreenInner.h);
    c.fill(ix, iy, iw, ih, kScreenBg);
    if (screen) {
        for (int row = 0; row < ih; ++row) {
            const int sr = row * kDisplayH / ih;
            for (int col = 0; col < iw; ++col)
                c.pixel(ix + col, iy + row,
                        screen[sr * kDisplayW + col * kDisplayW / iw]);
        }
    }

    // soft keys S1-S4
    sectionLabel(c, font, 41, 80, "SOFT KEYS");
    for (int i = 0; i < 4; ++i) {
        char lbl[4];
        std::snprintf(lbl, sizeof(lbl), "S%d", i + 1);
        button(c, font,
               {kSoftX0 + i * kSoftPitch, kSoftY, kSoftBtn.w, kSoftBtn.h},
               lbl, st.keyDown[kN2Soft1 + i],
               actKey && st.activeIndex == kN2Soft1 + i);
    }

    // encoders
    sectionLabel(c, font, 6, 101, "ENCODERS");
    for (int i = 0; i < 4; ++i) {
        const int cx = mm(kEncCx0 + i * kEncPitch), cy = mm(kEncCy);
        const int r = mm(kEncR);
        fillCircle(c, cx, cy, r, kKnob);
        ringCircle(c, cx, cy, r, 2,
                   (actEnc && st.activeIndex == i) || st.encoderPush[i]
                       ? kActive : kKnobEdge);
        c.line(cx, cy, cx, cy - r + 4, kPointer);
    }

    // transport row (REC is red)
    sectionLabel(c, font, 41, 111, "TRANSPORT");
    static const char* kTrLbl[6] = {"PLAY", "STOP", "REC",
                                    "MODE", "<", ">"};
    static const int kTrKey[6] = {kN2Play, kN2Stop, kN2Rec,
                                  kN2Mode, kN2Prev, kN2Next};
    for (int i = 0; i < 6; ++i)
        button(c, font, kTransport[i], kTrLbl[i], st.keyDown[kTrKey[i]],
               actKey && st.activeIndex == kTrKey[i],
               i == 2 ? kRecCap : kBtn);

    // ── 4x8 grid ────────────────────────────────────────────────────
    sectionLabel(c, font, 34, 126, "GRID - STEPS/PLAY/QWERTY");
    for (int i = 0; i < 32; ++i) {
        const int row = i / 8, col = i % 8;
        const float kx = kGridX0 + col * kGridPitch + 1;
        const float ky = kGridY0 + row * kGridPitch + 1;
        const bool down = st.keyDown[kN2Grid0 + i];
        const uint16_t cap = row < 2 ? kBlackCap : kCreamCap;
        c.fill(mm(kx), mm(ky), mm(kGridCap), mm(kGridCap),
               down ? rgb(90, 70, 60) : cap);
        c.frame(mm(kx), mm(ky), mm(kGridCap), mm(kGridCap),
                actKey && st.activeIndex == i ? kActive : kBtnEdge);
        // per-key LED dot (top center)
        fillCircle(c, mm(kx + kGridCap / 2), mm(ky + 2), 2 * S / 2,
                   st.leds[i] ? kLedOn : kLedOff);
        // step numbers on the top row
        if (row == 0) {
            char nb[4];
            std::snprintf(nb, sizeof(nb), "%d", col + 1);
            const int tw = Canvas565::textWidth(nb, 2);
            c.text(font, mm(kx + kGridCap / 2) - tw / 2,
                   mm(ky + kGridCap - 6), 2, nb, rgb(120, 118, 120));
        }
    }

    // thumbstick (pitch/mod)
    {
        const int jx = mm(kJoyCx), jy = mm(kJoyCy);
        const int ro = mm(kJoyR), ri = mm(kJoyKnobR);
        fillCircle(c, jx, jy, ro, kJoyRing);
        ringCircle(c, jx, jy, ro, 2, actJoy ? kActive : kBtnEdge);
        c.hline(jx - ro, jy, 2 * ro, kCreamDim);
        c.vline(jx, jy - ro, 2 * ro, kCreamDim);
        const int travel = ro - ri;
        const int kx = jx + int(st.joyX * travel);
        const int ky = jy - int(st.joyY * travel);
        fillCircle(c, kx, ky, ri, kCap);
        ringCircle(c, kx, ky, ri, 2, actJoy ? kActive : kKnobEdge);
        sectionLabel(c, font, 6, 170, "PITCH/MOD");
    }

    // crossfader (vertical, right margin)
    c.fill(mm(kFaderTrack.x), mm(kFaderTrack.y), mm(kFaderTrack.w),
           mm(kFaderTrack.h), kTrack);
    c.frame(mm(kFaderTrack.x), mm(kFaderTrack.y), mm(kFaderTrack.w),
            mm(kFaderTrack.h), kBtnEdge);
    const int capTravel = mm(kFaderTrack.h) - mm(kFaderCapH);
    const int capY = mm(kFaderTrack.y) + int((1.0f - st.slider) * capTravel);
    c.fill(mm(kFaderTrack.x) - mm(2.5f), capY, mm(kFaderCapW),
           mm(kFaderCapH), kCap);
    c.frame(mm(kFaderTrack.x) - mm(2.5f), capY, mm(kFaderCapW),
            mm(kFaderCapH), actSld ? kActive : kBtnEdge);
    sectionLabel(c, font, 155, 190, "XFADE");

    // bottom row: SHIFT-L, spacebar, SHIFT-R
    button(c, font, kShiftL, "SH", st.keyDown[kN2ShiftL],
           actKey && st.activeIndex == kN2ShiftL);
    button(c, font, kSpace, "SPACE", st.keyDown[kN2Space],
           actKey && st.activeIndex == kN2Space, kCreamCap);
    button(c, font, kShiftR, "SH", st.keyDown[kN2ShiftR],
           actKey && st.activeIndex == kN2ShiftR);

    // caption
    c.text(font, 8, kH - 20, 2,
           "GROOVEBOX SIM - NSR-2 panel 180x210mm @ 4px/mm", kCreamDim);
}

} // namespace gb
