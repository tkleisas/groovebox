#include "PanelView.h"

#include <cmath>
#include <cstdio>

namespace gb {

namespace {

constexpr int S = PanelView::kPxPerMm;
constexpr int mm(float v) { return int(v * S + 0.5f); }

constexpr uint16_t rgb(int r, int g, int b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Panel palette: deep instrument red (Nord/Elektron territory) with a
// darker red edge; controls stay dark/neutral for contrast; captions
// off-white so they read on red.
const uint16_t kPanel     = rgb(160, 24, 24);  // ~#A01818 red metal
const uint16_t kPanelEdge = rgb(104, 14, 14);  // darker red border
const uint16_t kBtn       = rgb(48, 46, 50);
const uint16_t kBtnEdge   = rgb(140, 140, 150);
const uint16_t kLabel     = rgb(228, 222, 212); // off-white
const uint16_t kLabelDim  = rgb(198, 186, 178); // light warm gray
const uint16_t kBezel     = rgb(20, 20, 24);
const uint16_t kScreenBg  = rgb(0, 0, 0);
const uint16_t kKnob      = rgb(58, 58, 64);
const uint16_t kKnobEdge  = rgb(160, 160, 170);
const uint16_t kPointer   = rgb(240, 240, 240);
const uint16_t kWhiteCap  = rgb(215, 212, 205);
const uint16_t kBlackCap  = rgb(18, 18, 22);
const uint16_t kShiftCap  = rgb(26, 24, 28);   // near-black, white text
const uint16_t kTrack     = rgb(25, 25, 28);
const uint16_t kCap       = rgb(80, 80, 90);
const uint16_t kLedOn     = rgb(235, 70, 55);  // red-on reads on white caps
const uint16_t kLedOff    = rgb(70, 30, 28);
const uint16_t kJoyRing   = rgb(30, 30, 35);
const uint16_t kActive    = rgb(245, 245, 245);

// ── geometry (mm, from tools/mockup_panel.py — 4.0" TFT layout) ─────
struct RectMm { float x, y, w, h; };

constexpr RectMm kScreenBezel = {8, 10, 96, 68};
constexpr RectMm kScreenInner = {11, 13, 88, 59};
constexpr float kSoftX = 108, kSoftY[4] = {14, 29, 44, 59};
constexpr RectMm kSoftBtn = {0, 0, 14, 11};
constexpr float kEncCx[4] = {20, 45, 70, 95}, kEncCy = 88, kEncR = 7;
// rev B2: 2 filter pots + 4 dedicated amp-ADSR encoders (same strip)
constexpr float kPotCx[2] = {140, 158};
constexpr float kPotCy = 32, kPotR = 6;
constexpr float kAdsrCx[4] = {184, 202, 220, 238}; // encoders 4..7
constexpr RectMm kTransport[3] = {{310, 14, 15, 10}, {327, 14, 15, 10},
                                  {344, 14, 15, 10}};
constexpr RectMm kNav[3] = {{310, 34, 16, 9}, {328, 34, 16, 9},
                            {346, 34, 16, 9}};
constexpr RectMm kSliderTrack = {310, 85, 62, 6};
constexpr float kSliderCapW = 13, kSliderCapH = 11;
constexpr float kJoyCx = 18, kJoyCy = 105, kJoyR = 10, kJoyKnobR = 4.5f;
constexpr float kKbX = 36, kWhiteY = 125, kBlackY = 105;
constexpr float kStepY = 147;  // rev B step row, aligned with whites
constexpr float kPitch = 19.05f, kCapMm = 18.1f;
constexpr RectMm kShiftL = {kKbX - 27, kWhiteY + 0.5f, 20, kCapMm};
constexpr RectMm kShiftR = {kKbX + 16 * kPitch + 7, kWhiteY + 0.5f, 20,
                            kCapMm};
// black-key white-neighbor indices (C# pattern), left to right
constexpr int kBlackPos[11] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12, 14};

bool inRect(int x, int y, RectMm r) {
    return x >= mm(r.x) && y >= mm(r.y) && x < mm(r.x + r.w) &&
           y < mm(r.y + r.h);
}

bool inCircle(int x, int y, float cx, float cy, float r) {
    const float dx = x - mm(cx), dy = y - mm(cy);
    const float rr = mm(r) + 2; // small slack for hit comfort
    return dx * dx + dy * dy <= rr * rr;
}

// ── drawing helpers ─────────────────────────────────────────────────
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

void knob(Canvas565& c, Font5x7& f, float cxMm, float cyMm, float rMm,
          float value01, bool endless, bool active, const char* lbl) {
    const int cx = mm(cxMm), cy = mm(cyMm), r = mm(rMm);
    fillCircle(c, cx, cy, r, kKnob);
    ringCircle(c, cx, cy, r, 2, active ? kActive : kKnobEdge);
    // pointer: endless encoders sit at 12 o'clock; pots sweep 300°
    float a = 0.0f;
    if (!endless) a = (value01 * 300.0f - 150.0f) * 3.14159265f / 180.0f;
    c.line(cx, cy, int(cx + (r - 4) * std::sin(a)),
           int(cy - (r - 4) * std::cos(a)), kPointer);
    if (lbl && *lbl) {
        const int tw = Canvas565::textWidth(lbl, 2);
        c.text(f, cx - tw / 2, cy + r + 8, 2, lbl, kLabel);
    }
}

// Button with label; pressed → inverted.
void button(Canvas565& c, Font5x7& f, RectMm r, const char* lbl,
            bool pressed, bool active, uint16_t fill = kBtn) {
    const bool inv = pressed;
    const uint16_t fg = inv ? kPanel : kLabel;
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
    c.text(f, mm(xMm), mm(yMm), 2, s, kLabelDim);
}

} // namespace

// ── hit testing ─────────────────────────────────────────────────────

PanelView::Hit PanelView::hitTest(int x, int y) {
    // blacks before whites (they overlap); small controls before big.
    for (int k = 0; k < 11; ++k) {
        const float bx = kKbX + kPitch * (kBlackPos[k] + 1) - 9.55f;
        if (inRect(x, y, {bx, kBlackY, kCapMm, kCapMm}))
            return {kHitKey, kKeyBlack0 + k};
    }
    for (int i = 0; i < 16; ++i)
        if (inRect(x, y, {kKbX + 0.5f + i * kPitch, kWhiteY + 0.5f,
                          kCapMm, kCapMm}))
            return {kHitKey, kKeyWhite0 + i};
    if (inRect(x, y, kShiftL)) return {kHitKey, kKeyShiftL};
    if (inRect(x, y, kShiftR)) return {kHitKey, kKeyShiftR};
    // rev B step row (keys 40-55), aligned with the whites below them
    for (int i = 0; i < 16; ++i)
        if (inRect(x, y, {kKbX + 0.5f + i * kPitch, kStepY + 0.5f,
                          kCapMm, kCapMm}))
            return {kHitKey, kKeyStep0 + i};
    for (int i = 0; i < 4; ++i)
        if (inRect(x, y, {kSoftX, kSoftY[i], kSoftBtn.w, kSoftBtn.h}))
            return {kHitKey, kKeySoft1 + i};
    for (int i = 0; i < 3; ++i)
        if (inRect(x, y, kTransport[i])) return {kHitKey, kKeyPlay + i};
    if (inRect(x, y, kNav[0])) return {kHitKey, kKeyMode};
    if (inRect(x, y, kNav[1])) return {kHitKey, kKeyPrev};
    if (inRect(x, y, kNav[2])) return {kHitKey, kKeyNext};
    for (int i = 0; i < 2; ++i)
        if (inCircle(x, y, kPotCx[i], kPotCy, kPotR)) return {kHitPot, i};
    for (int i = 0; i < 4; ++i)
        if (inCircle(x, y, kAdsrCx[i], kPotCy, kPotR))
            return {kHitEncoder, 4 + i}; // ADSR encoders 5-8
    for (int i = 0; i < 4; ++i)
        if (inCircle(x, y, kEncCx[i], kEncCy, kEncR))
            return {kHitEncoder, i};
    if (inRect(x, y, {kSliderTrack.x, kSliderTrack.y - 3,
                      kSliderTrack.w, kSliderTrack.h + 6}))
        return {kHitSlider, 0};
    if (inCircle(x, y, kJoyCx, kJoyCy, kJoyR)) return {kHitJoy, 0};
    return {};
}

// ── render ──────────────────────────────────────────────────────────

void PanelView::render(Canvas565& c, Font5x7& font, const PanelState& st,
                       const uint16_t* screen) {
    // panel body
    c.fill(0, 0, kW, kH, kPanel);
    c.frame(0, 0, kW, kH, kPanelEdge);
    c.frame(1, 1, kW - 2, kH - 2, kPanelEdge);

    const bool actKey = st.activeType == kHitKey;
    const bool actPot = st.activeType == kHitPot;
    const bool actEnc = st.activeType == kHitEncoder;
    const bool actSld = st.activeType == kHitSlider;
    const bool actJoy = st.activeType == kHitJoy;

    // mic hole (top edge, right of the bigger screen)
    fillCircle(c, mm(112), mm(5), 4 * S / 2 + 2, kBezel);
    ringCircle(c, mm(112), mm(5), 4 * S / 2 + 2, 1, kBtnEdge);
    c.text(font, mm(109), mm(8.5f), 1, "MIC", kLabelDim);

    // ── screen bezel + device framebuffer (1:1, centered) ───────────
    c.fill(mm(kScreenBezel.x), mm(kScreenBezel.y), mm(kScreenBezel.w),
           mm(kScreenBezel.h), kBezel);
    c.frame(mm(kScreenBezel.x), mm(kScreenBezel.y), mm(kScreenBezel.w),
            mm(kScreenBezel.h), kBtnEdge);
    const int ix = mm(kScreenInner.x), iy = mm(kScreenInner.y);
    const int iw = mm(kScreenInner.w), ih = mm(kScreenInner.h);
    c.fill(ix, iy, iw, ih, kScreenBg);
    if (screen) {
        // Stretch the 240x160 framebuffer to FILL the inner bezel
        // (~1.47x non-uniform, nearest-neighbor — slightly uneven
        // pixels at non-integer scale read like a real LCD).
        for (int row = 0; row < ih; ++row) {
            const int sr = row * kDisplayH / ih;
            for (int col = 0; col < iw; ++col)
                c.pixel(ix + col, iy + row,
                        screen[sr * kDisplayW + col * kDisplayW / iw]);
        }
    }

    // soft keys S1-S4 (column right of the screen)
    sectionLabel(c, font, 108, 82.5f, "SOFT KEYS");
    for (int i = 0; i < 4; ++i) {
        char lbl[4];
        std::snprintf(lbl, sizeof(lbl), "S%d", i + 1);
        button(c, font, {kSoftX, kSoftY[i], kSoftBtn.w, kSoftBtn.h}, lbl,
               st.keyDown[kKeySoft1 + i],
               actKey && st.activeIndex == kKeySoft1 + i);
    }

    // encoders under the screen
    for (int i = 0; i < 4; ++i)
        knob(c, font, kEncCx[i], kEncCy, kEncR, 0.0f, true,
             (actEnc && st.activeIndex == i) || st.encoderPush[i],
             nullptr);

    // rev B2: 2 filter pots + 4 amp-ADSR encoders (relative, tick only)
    sectionLabel(c, font, 136, 17, "FILTER");
    sectionLabel(c, font, 184, 17, "AMP ENVELOPE");
    static const char* kPotLbl[2] = {"CUT", "RES"};
    for (int i = 0; i < 2; ++i)
        knob(c, font, kPotCx[i], kPotCy, kPotR, st.pots[i], false,
             actPot && st.activeIndex == i, kPotLbl[i]);
    static const char* kAdsrLbl[4] = {"A", "D", "S", "R"};
    for (int i = 0; i < 4; ++i)
        knob(c, font, kAdsrCx[i], kPotCy, kPotR, 0.0f, true,
             (actEnc && st.activeIndex == 4 + i) || st.encoderPush[4 + i],
             kAdsrLbl[i]);

    // transport + mode/nav
    sectionLabel(c, font, 310, 9, "TRANSPORT");
    static const char* kTrLbl[3] = {"PLAY", "STOP", "REC"};
    for (int i = 0; i < 3; ++i)
        button(c, font, kTransport[i], kTrLbl[i], st.keyDown[kKeyPlay + i],
               actKey && st.activeIndex == kKeyPlay + i);
    static const char* kNavLbl[3] = {"MODE", "<", ">"};
    static const int kNavKey[3] = {kKeyMode, kKeyPrev, kKeyNext};
    for (int i = 0; i < 3; ++i)
        button(c, font, kNav[i], kNavLbl[i], st.keyDown[kNavKey[i]],
               actKey && st.activeIndex == kNavKey[i]);

    // data slider
    c.fill(mm(kSliderTrack.x), mm(kSliderTrack.y), mm(kSliderTrack.w),
           mm(kSliderTrack.h), kTrack);
    c.frame(mm(kSliderTrack.x), mm(kSliderTrack.y), mm(kSliderTrack.w),
            mm(kSliderTrack.h), kBtnEdge);
    const int capTravel = mm(kSliderTrack.w) - mm(kSliderCapW);
    const int capX = mm(kSliderTrack.x) + int(st.slider * capTravel);
    c.fill(capX, mm(82.5f), mm(kSliderCapW), mm(kSliderCapH), kCap);
    c.frame(capX, mm(82.5f), mm(kSliderCapW), mm(kSliderCapH),
            actSld ? kActive : kBtnEdge);
    sectionLabel(c, font, 310, 94, "DATA SLIDER (VEL)");

    // joystick
    {
        const int jx = mm(kJoyCx), jy = mm(kJoyCy);
        const int ro = mm(kJoyR), ri = mm(kJoyKnobR);
        fillCircle(c, jx, jy, ro, kJoyRing);
        ringCircle(c, jx, jy, ro, 2, actJoy ? kActive : kBtnEdge);
        c.hline(jx - ro, jy, 2 * ro, kLabelDim);
        c.vline(jx, jy - ro, 2 * ro, kLabelDim);
        const int travel = ro - ri;
        const int kx = jx + int(st.joyX * travel);
        const int ky = jy - int(st.joyY * travel);
        fillCircle(c, kx, ky, ri, kCap);
        ringCircle(c, kx, ky, ri, 2, actJoy ? kActive : kKnobEdge);
        sectionLabel(c, font, 6, 116.5f, "PITCH/MOD");
    }

    // ── brand badge (mixed-script UTF-8: Cyrillic + Greek) ──────────
    {
        const char* badge = "ВОКОИТЕР - ΟΡΓΑΝΟ 1";
        const int scale = 4;
        const int tw = Canvas565::textWidth(badge, scale);
        // centered in the free zone x=120..300mm, y≈62mm
        const int zoneX = mm(120), zoneW = mm(300) - zoneX;
        c.text(font, zoneX + (zoneW - tw) / 2, mm(62), scale, badge,
               kLabel);
    }

    // ── MX keyboard ─────────────────────────────────────────────────
    sectionLabel(c, font, 160, 100, "KEYBOARD / STEPS");

    // shift keys at the row ends
    button(c, font, kShiftL, "SH", st.keyDown[kKeyShiftL],
           actKey && st.activeIndex == kKeyShiftL, kShiftCap);
    button(c, font, kShiftR, "SH", st.keyDown[kKeyShiftR],
           actKey && st.activeIndex == kKeyShiftR, kShiftCap);

    // whites (with per-key LEDs)
    for (int i = 0; i < 16; ++i) {
        const float kx = kKbX + 0.5f + i * kPitch;
        const bool down = st.keyDown[kKeyWhite0 + i];
        const uint16_t cap = down ? rgb(120, 118, 112) : kWhiteCap;
        c.fill(mm(kx), mm(kWhiteY + 0.5f), mm(kCapMm), mm(kCapMm), cap);
        c.frame(mm(kx), mm(kWhiteY + 0.5f), mm(kCapMm), mm(kCapMm),
                actKey && st.activeIndex == i ? kActive : kBtnEdge);
        // LED dot (top center of the cap) — rev B: notes 16-31 = whites
        fillCircle(c, mm(kx + 9.55f), mm(kWhiteY + 3.0f), 3 * S / 2,
                   st.leds[16 + i] ? kLedOn : kLedOff);
        // key number
        char nb[4];
        std::snprintf(nb, sizeof(nb), "%d", i + 1);
        const int tw = Canvas565::textWidth(nb, 2);
        c.text(font, mm(kx + 9.55f) - tw / 2, mm(kWhiteY + 10.0f), 2, nb,
               rgb(140, 138, 132));
    }
    // blacks
    for (int k = 0; k < 11; ++k) {
        const float bx = kKbX + kPitch * (kBlackPos[k] + 1) - 9.55f;
        const bool down = st.keyDown[kKeyBlack0 + k];
        c.fill(mm(bx + 0.5f), mm(kBlackY + 0.5f), mm(kCapMm), mm(kCapMm),
               down ? rgb(70, 70, 78) : kBlackCap);
        c.frame(mm(bx + 0.5f), mm(kBlackY + 0.5f), mm(kCapMm), mm(kCapMm),
                actKey && st.activeIndex == kKeyBlack0 + k ? kActive
                                                           : kBtnEdge);
    }

    // rev B: dedicated step row under the piano (dark caps, per-key
    // LEDs = notes 0-15), aligned with the white keys above
    sectionLabel(c, font, 370, 152, "STEPS");
    for (int i = 0; i < 16; ++i) {
        const float kx = kKbX + 0.5f + i * kPitch;
        const bool down = st.keyDown[kKeyStep0 + i];
        c.fill(mm(kx), mm(kStepY + 0.5f), mm(kCapMm), mm(kCapMm),
               down ? rgb(80, 76, 70) : rgb(32, 30, 36));
        c.frame(mm(kx), mm(kStepY + 0.5f), mm(kCapMm), mm(kCapMm),
                actKey && st.activeIndex == kKeyStep0 + i ? kActive
                                                          : kBtnEdge);
        fillCircle(c, mm(kx + 9.55f), mm(kStepY + 3.0f), 3 * S / 2,
                   st.leds[i] ? kLedOn : kLedOff);
        char nb[4];
        std::snprintf(nb, sizeof(nb), "%d", i + 1);
        const int tw = Canvas565::textWidth(nb, 2);
        c.text(font, mm(kx + 9.55f) - tw / 2, mm(kStepY + 10.0f), 2, nb,
               rgb(120, 116, 122));
    }

    // caption
    c.text(font, 8, kH - 20, 2,
           "GROOVEBOX SIM - panel 400x170mm @ 4px/mm - F11 screen view",
           kLabelDim);
}

} // namespace gb
