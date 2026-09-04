#include "widgets.h"

#include <cmath>
#include <cstdio>

namespace gb {

// ── Theme (built-in schemes) ────────────────────────────────────────
namespace {
constexpr uint16_t rgb565(int r, int g, int b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr Theme kThemes[kNumThemes] = {
    // MONO: original white/gray on near-black (byte-identical values;
    // all roles = fg)
    {0x0000, 0xFFFF, 0x7BEF, 0x4208, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}},
    // RED: red-lacquered instrument panel at night
    {rgb565(14, 4, 4), rgb565(255, 70, 52), rgb565(198, 92, 74),
     rgb565(92, 26, 20),
     {rgb565(255, 70, 52), rgb565(255, 70, 52), rgb565(255, 70, 52),
      rgb565(255, 70, 52)}},
    // GREEN: phosphor terminal
    {rgb565(3, 12, 5), rgb565(86, 255, 122), rgb565(96, 190, 130),
     rgb565(22, 82, 42),
     {rgb565(86, 255, 122), rgb565(86, 255, 122), rgb565(86, 255, 122),
      rgb565(86, 255, 122)}},
    // AMBER: amber phosphor
    {rgb565(14, 9, 2), rgb565(255, 176, 0), rgb565(184, 122, 32),
     rgb565(90, 58, 16),
     {rgb565(255, 176, 0), rgb565(255, 176, 0), rgb565(255, 176, 0),
      rgb565(255, 176, 0)}},
    // ARCADE: 16-color EGA/80s CRT — light-gray text, roles carry color
    {rgb565(0, 0, 0), rgb565(170, 170, 170), rgb565(85, 85, 85),
     rgb565(40, 40, 40),
     {rgb565(255, 255, 85),  // acc1 values/toasts: yellow
      rgb565(85, 255, 85),   // acc2 bars/levels: light green
      rgb565(85, 255, 255),  // acc3 selection: light cyan
      rgb565(255, 85, 85)}}, // acc4 playhead/rec: light red
};
ThemeId g_current = kThemeMono;
} // namespace

Theme g_theme = kThemes[kThemeMono];
uint16_t& kColBlack = g_theme.bg;
uint16_t& kColWhite = g_theme.fg;
uint16_t& kColGray  = g_theme.mid;
uint16_t& kColDim   = g_theme.dim;
uint16_t& kColAcc1  = g_theme.acc[0];
uint16_t& kColAcc2  = g_theme.acc[1];
uint16_t& kColAcc3  = g_theme.acc[2];
uint16_t& kColAcc4  = g_theme.acc[3];

void setTheme(ThemeId id) {
    if (id >= 0 && id < kNumThemes) { g_theme = kThemes[id]; g_current = id; }
}
ThemeId currentTheme() { return g_current; }
const char* themeName(ThemeId id) {
    static const char* kNames[kNumThemes] = {"MONO", "RED", "GREEN",
                                             "AMBER", "ARCADE"};
    return (id >= 0 && id < kNumThemes) ? kNames[id] : "?";
}

// ── Canvas565 ───────────────────────────────────────────────────────

void Canvas565::pixel(int x, int y, uint16_t c) {
    if (x >= 0 && y >= 0 && x < w && y < h) fb[y * w + x] = c;
}

void Canvas565::fill(int x, int y, int rw, int rh, uint16_t c) {
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (rw <= 0 || rh <= 0) return;
    for (int row = y; row < y + rh; ++row)
        for (int col = x; col < x + rw; ++col)
            fb[row * w + col] = c;
}

void Canvas565::frame(int x, int y, int rw, int rh, uint16_t c) {
    fill(x, y, rw, 1, c);
    fill(x, y + rh - 1, rw, 1, c);
    fill(x, y, 1, rh, c);
    fill(x + rw - 1, y, 1, rh, c);
}

void Canvas565::line(int x0, int y0, int x1, int y1, uint16_t c) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

int Canvas565::text(Font5x7& f, int x, int y, int scale, const char* s,
                    uint16_t fg) {
    const int advance = (Font5x7::kW + 1) * scale;
    int cx = x;
    const char* p = s;
    while (*p) {
        const uint32_t cp = utf8Next(p);
        if (cp != ' ') {
            for (int gx = 0; gx < Font5x7::kW; ++gx)
                for (int gy = 0; gy < Font5x7::kH; ++gy)
                    if (f.pixel(cp, gx, gy))
                        fill(cx + gx * scale, y + gy * scale, scale, scale, fg);
        }
        cx += advance;
    }
    return cx - x;
}

// ── PhasePieWidget ──────────────────────────────────────────────────

void PhasePieWidget::draw(Canvas565& c, Font5x7& font) const {
    const uint16_t fg = inverse ? kColBlack : kColWhite;
    const uint16_t bg = inverse ? kColWhite : kColBlack;
    c.fill(x, y, size, size, bg);
    const float cx = size * 0.5f, cy = size * 0.5f; // local coords
    const float rOut = size * 0.5f - 1.0f;
    const float rIn = rOut - 2.0f; // outline ring thickness 2 px
    float v = phase - std::floor(phase); // wrap into 0..1
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            const float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            const float r = std::sqrt(dx * dx + dy * dy);
            if (r > rOut) continue;
            // angle from 12 o'clock, clockwise, 0..1 turns
            float a = std::atan2(dx, -dy) / (2.0f * 3.14159265f);
            if (a < 0.0f) a += 1.0f;
            if (r >= rIn) {
                c.pixel(x + px, y + py, fg); // outline ring
            } else if (a <= v) {
                c.pixel(x + px, y + py, fg); // filled wedge
            }
        }
    // 12 o'clock tick (over ring, reads as the zero-phase marker)
    c.fill(x + int(cx) - 1, y + 1, 2, 4, fg);
    // center dot
    c.fill(x + int(cx) - 1, y + int(cy) - 1, 2, 2, fg);
    if (showText && size >= 24) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", int(v * 100.0f + 0.5f));
        const int tw = Canvas565::textWidth(buf, 1);
        c.text(font, x + int(cx) - tw / 2, y + int(cy) - 3, 1, buf, bg);
    }
}

// ── WaveformWidget ──────────────────────────────────────────────────

void WaveformWidget::draw(Canvas565& c) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColDim);
    const int iw = w - 2, ih = h - 2;
    const int midY = y + 1 + ih / 2;
    c.hline(x + 1, midY, iw, kColDim); // zero line
    if (!samples || count <= 0) return;
    for (int col = 0; col < iw; ++col) {
        const int i0 = col * count / iw;
        int i1 = (col + 1) * count / iw;
        if (i1 <= i0) i1 = i0 + 1;
        float lo = 1.0f, hi = -1.0f;
        for (int i = i0; i < i1 && i < count; ++i) {
            if (samples[i] < lo) lo = samples[i];
            if (samples[i] > hi) hi = samples[i];
        }
        if (lo < -1.0f) lo = -1.0f;
        if (hi > 1.0f) hi = 1.0f;
        const int y0 = midY - int(hi * (ih / 2 - 1));
        const int y1 = midY - int(lo * (ih / 2 - 1));
        c.vline(x + 1 + col, y0, y1 - y0 + 1, kColWhite);
    }
    if (playhead >= 0 && playhead < count) {
        const int px = x + 1 + playhead * iw / count;
        c.vline(px, y + 1, ih, kColGray);
    }
}

// ── AdsrWidget ──────────────────────────────────────────────────────

void AdsrWidget::draw(Canvas565& c) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColDim);
    const int iw = w - 2, ih = h - 2;
    const int baseY = y + 1 + ih - 1;
    const int topY = y + 1;
    // Segment widths: A/D/R proportional to their (normalized) times,
    // S gets the remainder of the width.
    const float tA = attack < 0.f ? 0.f : attack;
    const float tD = decay < 0.f ? 0.f : decay;
    const float tR = release < 0.f ? 0.f : release;
    const float sum = tA + tD + tR;
    const float k = sum > 0.001f ? (iw * 0.75f) / sum : 0.0f;
    const int wA = int(tA * k + 0.5f);
    const int wD = int(tD * k + 0.5f);
    const int wR = int(tR * k + 0.5f);
    const int wS = iw - wA - wD - wR;
    const float s = sustain < 0.f ? 0.f : (sustain > 1.f ? 1.f : sustain);
    const int susY = baseY - int(s * (ih - 1));

    const int x0 = x + 1;
    const int nx[5] = {x0, x0 + wA, x0 + wA + wD, x0 + wA + wD + wS,
                       x0 + iw};
    const int ny[5] = {baseY, topY, susY, susY, baseY};
    for (int seg = 0; seg < 4; ++seg) {
        const uint16_t col = (seg == selected) ? kColWhite : kColGray;
        c.line(nx[seg], ny[seg], nx[seg + 1], ny[seg + 1], col);
        c.line(nx[seg], ny[seg] + 1, nx[seg + 1], ny[seg + 1] + 1, col);
    }
    // node handles
    for (int i = 0; i < 5; ++i)
        c.frame(nx[i] - 1, ny[i] - 1, 3, 3, kColWhite);
}

// ── FmAlgoWidget ────────────────────────────────────────────────────

const FmAlgo kFmAlgoSerial = {
    "SERIAL",
    {3, 2, 1, 0}, {0, 0, 0, 0},        // OP1 rightmost, OP4 leftmost
    {{3, 2}, {2, 1}, {1, 0}},          // OP4→OP3→OP2→OP1
    3,
    {true, false, false, false},
};
const FmAlgo kFmAlgoParallel = {
    "PARALLEL",
    {0, 1, 0, 1}, {0, 0, 1, 1},
    {{0, 0}},                          // no modulator edges
    0,
    {true, true, true, true},
};
const FmAlgo kFmAlgoPair = {
    "2x2 PAIR",
    {1, 0, 1, 0}, {0, 0, 1, 1},        // OP2→OP1, OP4→OP3
    {{1, 0}, {3, 2}},
    2,
    {true, false, true, false},
};
const FmAlgo kFmAlgoStack = {
    "STACK",
    {3, 2, 1, 0}, {0, 0, 1, 1},        // OP4→OP3 (top), OP3↓OP2, OP2→OP1
    {{3, 2}, {2, 1}, {1, 0}},
    3,
    {true, false, false, false},
};

// Small arrowhead at (x, y), entered travelling along unit dir (dx, dy).
static void arrowHead(Canvas565& c, int x, int y, int dx, int dy,
                      uint16_t col) {
    const int px = -dy, py = dx; // perpendicular
    c.line(x, y, x - dx * 3 + px * 2, y - dy * 3 + py * 2, col);
    c.line(x, y, x - dx * 3 - px * 2, y - dy * 3 - py * 2, col);
}

void FmAlgoWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, 128, 64, kColBlack);
    if (!algo) return;
    c.frame(x, y, 128, 56, kColDim);
    // Operator box geometry: 32x24 cells, 20x12 boxes.
    int bx[4], by[4];
    for (int i = 0; i < 4; ++i) {
        bx[i] = x + algo->opCol[i] * 32 + 6;
        by[i] = y + 4 + algo->opRow[i] * 24 + 6;
    }
    // Edges first (boxes overdraw the line ends), elbow-routed.
    for (int e = 0; e < algo->numEdges; ++e) {
        const int m = algo->edges[e][0], cr = algo->edges[e][1];
        const int mcx = bx[m] + 10, mcy = by[m] + 6;   // modulator center
        const int ccx = bx[cr] + 10, ccy = by[cr] + 6; // carrier center
        if (by[m] == by[cr]) {
            // same row: straight horizontal, modulator side → carrier side
            const int dir = ccx > mcx ? 1 : -1;
            const int x0 = dir > 0 ? bx[m] + 20 : bx[m];
            const int x1 = dir > 0 ? bx[cr] : bx[cr] + 20;
            c.hline(dir > 0 ? x0 : x1, mcy, std::abs(x1 - x0), kColGray);
            arrowHead(c, x1, mcy, dir, 0, kColGray);
        } else if (bx[m] == bx[cr]) {
            // same column: straight vertical
            const int dir = ccy > mcy ? 1 : -1;
            const int y0 = dir > 0 ? by[m] + 12 : by[m];
            const int y1 = dir > 0 ? by[cr] : by[cr] + 12;
            c.vline(mcx, dir > 0 ? y0 : y1, std::abs(y1 - y0), kColGray);
            arrowHead(c, mcx, y1, 0, dir, kColGray);
        } else {
            // elbow: vertical out of the modulator's top/bottom, then
            // horizontal into the carrier's side
            const int dirY = ccy > mcy ? 1 : -1;
            const int dirX = ccx > mcx ? 1 : -1;
            const int vy0 = dirY > 0 ? by[m] + 12 : by[m];
            c.vline(mcx, dirY > 0 ? vy0 : ccy, std::abs(ccy - vy0), kColGray);
            const int x1 = dirX > 0 ? bx[cr] : bx[cr] + 20;
            c.hline(dirX > 0 ? mcx : x1, ccy, std::abs(x1 - mcx), kColGray);
            arrowHead(c, x1, ccy, dirX, 0, kColGray);
        }
    }
    // Boxes + labels + carrier output stubs
    for (int i = 0; i < 4; ++i) {
        c.fill(bx[i], by[i], 20, 12, kColBlack);
        c.frame(bx[i], by[i], 20, 12, kColWhite);
        char label[5];
        std::snprintf(label, sizeof(label), "OP%d", i + 1);
        c.text(font, bx[i] + 1, by[i] + 2, 1, label, kColWhite);
        if (algo->carrier[i]) {
            // output arrow stub toward the right edge (clipped to frame)
            const int x0 = bx[i] + 20;
            const int x1 = x0 + 8 < x + 126 ? x0 + 8 : x + 126;
            c.hline(x0, by[i] + 6, x1 - x0, kColWhite);
            arrowHead(c, x1, by[i] + 6, 1, 0, kColWhite);
        }
    }
    c.text(font, x + 2, y + 57, 1, algo->name, kColGray);
    const char* out = "OUT";
    c.text(font, x + 126 - Canvas565::textWidth(out, 1), y + 57, 1, out,
           kColGray);
}

// ── SpectrumWidget ──────────────────────────────────────────────────

void SpectrumWidget::draw(Canvas565& c) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColDim);
    const int iw = w - 2, ih = h - 2;
    if (!mags || bins <= 0) return;
    const int slot = iw / bins;
    if (slot < 1) return;
    const int bw = slot > 1 ? slot - 1 : 1;
    for (int i = 0; i < bins; ++i) {
        float v = mags[i];
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        const int bh = int(v * ih + 0.5f);
        const int bx = x + 1 + i * slot;
        if (bh > 0) c.fill(bx, y + 1 + ih - bh, bw, bh, kColAcc2);
        if (peaks && peaks[i] >= 0.f) {
            float p = peaks[i] > 1.f ? 1.f : peaks[i];
            const int py = y + 1 + ih - int(p * ih + 0.5f);
            c.hline(bx, py, bw, kColAcc4);
        }
    }
}

// ── SequencerWidget ─────────────────────────────────────────────────

void SequencerWidget::draw(Canvas565& c) const {
    const int rows = this->rows < 1 ? 1 : (this->rows > 4 ? 4 : this->rows);
    c.fill(x, y, 128, rows * rowH, kColBlack);
    for (int r = 0; r < rows; ++r) {
        const bool muted = (rowMuted >> r) & 1u;
        const uint16_t colAccent = muted ? kColDim  : kColAcc1;
        const uint16_t colSet    = muted ? kColDim  : kColGray;
        const uint16_t colEmpty  = kColDim;
        const int ry = y + r * rowH + (rowH - 8) / 2;
        for (int s = 0; s < 16; ++s) {
            const int cx = x + s * 8;
            const uint8_t st = cells[r][s];
            const bool ph = (s == playhead);
            if (ph) c.fill(cx, ry, 8, 8, muted ? kColDim : kColAcc4);
            if (st == kStepSet || st == kStepSetAlt) {
                c.fill(cx + 1, ry + 1, 6, 6, ph ? kColBlack : colSet);
                if (st == kStepSetAlt) // pitch override marker
                    c.fill(cx + 3, ry, 2, 1, ph ? kColWhite : colAccent);
            } else if (st == kStepAccent || st == kStepAccentAlt) {
                c.fill(cx, ry, 8, 8, colAccent);
                if (ph) c.frame(cx + 1, ry + 1, 6, 6, kColBlack);
                if (st == kStepAccentAlt) // pitch override marker
                    c.fill(cx + 3, ry, 2, 1, kColBlack);
            } else {
                c.fill(cx + 3, ry + 3, 2, 2, ph ? kColBlack : colEmpty);
            }
        }
    }
    // playhead underline across the grid
    if (playhead >= 0 && playhead < 16)
        c.hline(x + playhead * 8, y + rows * rowH - 1, 8, kColAcc4);
}

// ── MixerWidget ─────────────────────────────────────────────────────

void MixerWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, 80, h, kColBlack);
    const int barH = h - 16; // 8 px M/S row at the bottom
    for (int i = 0; i < 5; ++i) {
        const int sx = x + i * 16;
        float lv = level[i], pk = peak[i];
        if (lv < 0.f) lv = 0.f; if (lv > 1.f) lv = 1.f;
        if (pk < lv) pk = lv;   if (pk > 1.f) pk = 1.f;
        const int filled = int(lv * (barH - 2) + 0.5f);
        const int bx = sx + 4;
        // fader trough + fill + peak marker
        c.frame(bx, y, 8, barH, kColDim);
        if (filled > 0)
            c.fill(bx + 1, y + barH - 1 - filled, 6, filled,
                   mute[i] ? kColDim : kColAcc2);
        const int py = y + barH - 1 - int(pk * (barH - 2) + 0.5f);
        c.hline(bx, py, 8, kColGray);
        // M / S letters
        const int ly = y + barH + 4;
        if (mute[i]) {
            c.fill(sx + 1, ly - 1, 7, 8, kColWhite);
            c.text(font, sx + 2, ly, 1, "M", kColBlack);
        } else {
            c.text(font, sx + 2, ly, 1, "M", kColDim);
        }
        if (solo[i]) {
            c.fill(sx + 9, ly - 1, 7, 8, kColWhite);
            c.text(font, sx + 10, ly, 1, "S", kColBlack);
        } else {
            c.text(font, sx + 10, ly, 1, "S", kColDim);
        }
        // selected strip: bright underline
        if (i == selected) c.hline(sx + 1, y + h - 2, 14, kColAcc3);
    }
    // master separator
    c.vline(x + 4 * 16, y, barH, kColDim);
}

// ── WavetableWidget ─────────────────────────────────────────────────

void WavetableWidget::draw(Canvas565& c) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColDim);
    const int iw = w - 2, ih = h - 10; // 8 px strip under box: marker
    const int midY = y + 1 + ih / 2;
    if (table && size > 1) {
        int prevY = midY;
        for (int col = 0; col < iw; ++col) {
            const float v = table[col * (size - 1) / (iw - 1)];
            const int sy = midY - int(v * (ih / 2 - 1));
            if (col > 0) c.line(x + col, prevY, x + 1 + col, sy, kColWhite);
            prevY = sy;
        }
    }
    // frame-position marker: small triangle under the box
    const float p = framePos < 0.f ? 0.f : (framePos > 1.f ? 1.f : framePos);
    const int mx = x + 1 + int(p * (iw - 1));
    const int my = y + 1 + ih + 2;
    c.pixel(mx, my, kColWhite);
    c.hline(mx - 1, my + 1, 3, kColWhite);
    c.hline(mx - 2, my + 2, 5, kColWhite);
}

// ── KnobWidget ──────────────────────────────────────────────────────

void KnobWidget::draw(Canvas565& c, Font5x7& font) const {
    const int bh = size + (label ? 8 : 0);
    c.fill(x, y, size, bh, kColBlack);
    const float cx = x + size * 0.5f, cy = y + size * 0.5f;
    const float r = size * 0.5f - 1.0f;
    const float v = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
    // circle outline (dim): plot pixels on the radius
    const int steps = int(r * 8);
    for (int i = 0; i < steps; ++i) {
        const float a = float(i) / steps * 2.0f * 3.14159265f;
        c.pixel(int(cx + r * std::sin(a)), int(cy - r * std::cos(a)),
                kColDim);
    }
    // pointer: 300° sweep, 60° gap at bottom (value 0 at -150° from top)
    const float a = (v * 300.0f - 150.0f) * 3.14159265f / 180.0f;
    c.line(int(cx), int(cy), int(cx + (r - 1.0f) * std::sin(a)),
           int(cy - (r - 1.0f) * std::cos(a)), kColWhite);
    c.fill(int(cx) - 1, int(cy) - 1, 2, 2, kColWhite); // hub
    if (label) {
        const int tw = Canvas565::textWidth(label, 1);
        c.text(font, x + (size - tw) / 2, y + size + 1, 1, label, kColGray);
    }
}

// ── SwitchWidget ────────────────────────────────────────────────────

void SwitchWidget::draw(Canvas565& c) const {
    c.fill(x, y, 16, 8, kColBlack);
    c.frame(x, y, 16, 8, kColGray); // trough
    if (on) c.fill(x + 9, y + 1, 6, 6, kColWhite);       // nub right
    else    c.fill(x + 1, y + 1, 6, 6, kColDim);         // nub left
}

// ── RadioWidget ─────────────────────────────────────────────────────

void RadioWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, 8, 8, kColBlack);
    c.frame(x + 1, y + 1, 6, 6, kColWhite); // "circle" (6x6 ring at 8px)
    if (selected) c.fill(x + 3, y + 3, 2, 2, kColWhite); // dot
    if (label) c.text(font, x + 12, y + 1, 1, label, kColWhite);
}

void RadioWidget::drawGroup(Canvas565& c, Font5x7& font, int x, int y,
                            const char* const* items, int count,
                            int selected) {
    for (int i = 0; i < count; ++i) {
        RadioWidget r{x, y + i * 8, i == selected, items[i]};
        r.draw(c, font);
    }
}

// ── ListWidget ──────────────────────────────────────────────────────

void ListWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColDim);
    const int rows = (h - 2) / 8;
    for (int r = 0; r < rows; ++r) {
        const int idx = scroll + r;
        if (idx >= count) break;
        const int ry = y + 1 + r * 8;
        const bool sel = (idx == selected);
        if (sel) c.fill(x + 1, ry, w - 2, 8, kColWhite);
        if (items && items[idx])
            c.text(font, x + 3, ry + 1, 1, items[idx],
                   sel ? kColBlack : kColWhite);
    }
    if (count > rows) { // scrollbar
        const int sx = x + w - 3;
        c.vline(sx, y + 1, h - 2, kColDim);
        const int th = (h - 2) * rows / count;
        const int ty = y + 1 + (h - 2 - th) * scroll / (count - rows);
        c.vline(sx, ty, th, kColWhite);
    }
}

// ── FileBrowserWidget ───────────────────────────────────────────────

void FileBrowserWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColDim);
    if (path) c.text(font, x + 3, y + 1, 1, path, kColGray);
    c.hline(x + 1, y + 8, w - 2, kColDim);
    const int rows = (h - 10) / 8;
    for (int r = 0; r < rows; ++r) {
        const int idx = scroll + r;
        if (idx >= count) break;
        const int ry = y + 9 + r * 8;
        const bool sel = (idx == selected);
        if (sel) c.fill(x + 1, ry, w - 2, 8, kColWhite);
        if (entries && entries[idx]) {
            const uint16_t col = sel ? kColBlack : kColWhite;
            const int tx = c.text(font, x + 3, ry + 1, 1, entries[idx], col);
            if (isDir && isDir[idx])
                c.text(font, x + 3 + tx, ry + 1, 1, "/", sel ? kColBlack
                                                             : kColGray);
        }
    }
    if (count > rows) { // scrollbar
        const int sx = x + w - 3;
        c.vline(sx, y + 9, h - 10, kColDim);
        const int th = (h - 10) * rows / count;
        const int ty = y + 9 + (h - 10 - th) * scroll / (count - rows);
        c.vline(sx, ty, th, kColWhite);
    }
}

// ── ToastWidget ─────────────────────────────────────────────────────

void ToastWidget::draw(Canvas565& c, Font5x7& font) const {
    auto roundUp8 = [](int v) { return (v + 7) & ~7; };
    // content metrics: value at 2x (14 px), label at 1x (7 px)
    const int valueW = valueText ? Canvas565::textWidth(valueText, 2) : 0;
    const int labelW = label ? Canvas565::textWidth(label, 1) : 0;
    const int contentW = valueW > labelW ? valueW : labelW;
    // width: content + symmetric padding, rounded up to the 8px grid,
    // at least the declared minimum, clamped to the screen with margin
    int W = std::max(w, roundUp8(contentW + 16));
    W = std::min(W, c.w - 16);
    // height: 3 pad + 14 value + 4 gap + 7 label + 4 pad = 32 (grid ✓)
    int H = std::max(h, roundUp8(3 + 14 + 4 + 7 + 4));
    H = std::min(H, c.h - 16);
    // recenter the computed box on the minimum box's center
    int X = x + w / 2 - W / 2;
    int Y = y + h / 2 - H / 2;
    if (X < 4) X = 4;
    if (Y < 4) Y = 4;

    c.fill(X, Y, W, H, kColBlack);
    c.frame(X, Y, W, H, kColWhite);
    c.frame(X + 1, Y + 1, W - 2, H - 2, kColWhite); // double border
    if (valueText) {
        const int tw = Canvas565::textWidth(valueText, 2);
        c.text(font, X + (W - tw) / 2, Y + 3, 2, valueText, kColAcc1);
    }
    if (label) {
        const int tw = Canvas565::textWidth(label, 1);
        c.text(font, X + (W - tw) / 2, Y + 3 + 14 + 4, 1, label, kColGray);
    }
}

// ── TransportClockWidget ────────────────────────────────────────────

void TransportClockWidget::draw(Canvas565& c, Font5x7& font) const {
    const uint16_t fg = inverse ? kColBlack : kColWhite;
    const uint16_t bg = inverse ? kColWhite : kColBlack;
    c.fill(x, y, 72, 16, bg);
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%03d.%d.%02d", bar, beat, step);
    c.text(font, x, y + 5, 1, buf, fg);
    PhasePieWidget pie{x + 56, y, 16, beatPhase, false, inverse};
    pie.draw(c, font);
}

// ── NumericFieldWidget ──────────────────────────────────────────────

void NumericFieldWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, w, 8, kColBlack);
    char buf[12];
    char fmt[8];
    std::snprintf(fmt, sizeof(fmt), "%%0%dd", digits);
    std::snprintf(buf, sizeof(buf), fmt, value);
    for (int i = 0; i < digits; ++i) {
        const int dx = x + i * 8;
        const bool sel = (i == selDigit);
        if (sel) c.fill(dx, y, 8, 8, kColWhite);
        char d[2] = {buf[i], 0};
        c.text(font, dx + 1, y + 1, 1, d, sel ? kColBlack : kColWhite);
    }
    if (unit) c.text(font, x + digits * 8 + 4, y + 1, 1, unit, kColGray);
}

// ── DialogWidget ────────────────────────────────────────────────────

void DialogWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColWhite);
    // inverted title bar
    c.fill(x + 1, y + 1, w - 2, 10, kColWhite);
    if (title) {
        const int tw = Canvas565::textWidth(title, 1);
        c.text(font, x + (w - tw) / 2, y + 2, 1, title, kColBlack);
    }
    if (line1) c.text(font, x + 4, y + 16, 1, line1, kColWhite);
    if (line2) c.text(font, x + 4, y + 26, 1, line2, kColWhite);
    // soft-key hints on 60 px columns (aligned to the physical keys)
    const int colW = w / 4;
    for (int i = 0; i < 4; ++i) {
        if (!soft[i]) continue;
        const int tw = Canvas565::textWidth(soft[i], 1);
        c.text(font, x + i * colW + (colW - tw) / 2, y + h - 9, 1,
               soft[i], kColGray);
    }
}

// ── BatteryWidget ───────────────────────────────────────────────────

void BatteryWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, 16, 8, kColBlack);
    c.frame(x, y, 13, 8, kColWhite);        // body
    c.fill(x + 13, y + 2, 2, 4, kColWhite); // nub
    const float lv = level < 0.f ? 0.f : (level > 1.f ? 1.f : level);
    const int fw = int(lv * 11.0f + 0.5f);
    if (fw > 0) c.fill(x + 1, y + 1, fw, 6, lv > 0.2f ? kColAcc2
                                                      : kColGray);
    if (showText) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d%%", int(lv * 100.0f + 0.5f));
        c.text(font, x + 18, y + 1, 1, buf, kColWhite);
    }
}

// ── StatusIconsWidget ───────────────────────────────────────────────

void StatusIconsWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, 32, 8, kColBlack);
    const bool on[4] = {midi, link, sd, mic};
    const char* glyphs[4] = {"M", "L", "S", "R"};
    for (int i = 0; i < 4; ++i)
        c.text(font, x + i * 8 + 1, y + 1, 1, glyphs[i],
               on[i] ? kColWhite : kColDim);
}

// ── SliceWidget ─────────────────────────────────────────────────────

void SliceWidget::draw(Canvas565& c) const {
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, kColDim);
    const int iw = w - 2, ih = h - 2;
    float ts = trimStart < 0.f ? 0.f : trimStart;
    float te = trimEnd > 1.f ? 1.f : trimEnd;
    if (te < ts) { const float tmp = ts; ts = te; te = tmp; }
    const int trimX0 = x + 1 + int(ts * (iw - 1));
    const int trimX1 = x + 1 + int(te * (iw - 1));
    const int slices = numSlices < 1 ? 1 : numSlices;
    // selected slice region inverted (behind the waveform)
    int selX0 = 0, selX1 = 0;
    if (selectedSlice >= 0 && selectedSlice < slices) {
        selX0 = trimX0 + (trimX1 - trimX0) * selectedSlice / slices;
        selX1 = trimX0 + (trimX1 - trimX0) * (selectedSlice + 1) / slices;
        c.fill(selX0, y + 1, selX1 - selX0, ih, kColWhite);
    }
    const int midY = y + 1 + ih / 2;
    if (samples && count > 0) {
        for (int col = 0; col < iw; ++col) {
            const int i0 = col * count / iw;
            int i1 = (col + 1) * count / iw;
            if (i1 <= i0) i1 = i0 + 1;
            float lo = 1.0f, hi = -1.0f;
            for (int i = i0; i < i1 && i < count; ++i) {
                if (samples[i] < lo) lo = samples[i];
                if (samples[i] > hi) hi = samples[i];
            }
            const int y0 = midY - int(hi * (ih / 2 - 1));
            const int y1 = midY - int(lo * (ih / 2 - 1));
            const int px = x + 1 + col;
            const bool inSel = selectedSlice >= 0 && px >= selX0 &&
                               px < selX1;
            c.vline(px, y0, y1 - y0 + 1, inSel ? kColBlack : kColWhite);
        }
    }
    // slice ticks (bright), then trim markers (full-height + top notch)
    for (int s = 1; s < slices; ++s) {
        const int tx = trimX0 + (trimX1 - trimX0) * s / slices;
        c.vline(tx, y + 1, ih, kColGray);
    }
    c.vline(trimX0, y + 1, ih, kColWhite);
    c.vline(trimX1, y + 1, ih, kColWhite);
    c.hline(trimX0, y + 1, 3, kColWhite);
    c.hline(trimX1 - 2, y + 1, 3, kColWhite);
}

// ── StepLaneWidget ──────────────────────────────────────────────────

void StepLaneWidget::draw(Canvas565& c) const {
    c.fill(x, y, 128, 16, kColBlack);
    for (int s = 0; s < 16; ++s) {
        const int cx = x + s * 8;
        const int bh = values[s] * 12 / 127;
        if (bh > 0) c.fill(cx + 2, y + 14 - bh, 4, bh, kColAcc2);
        c.fill(cx + 2, y + 14, 4, 1, kColDim); // baseline ticks
        if (s == current) c.fill(cx, y, 8, 2, kColAcc4); // top marker
    }
}

// ── TunerWidget ─────────────────────────────────────────────────────

void TunerWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, w, 16, kColBlack);
    c.frame(x, y, w, 16, kColDim);
    if (note) {
        const int tw = Canvas565::textWidth(note, 1);
        c.text(font, x + (w - tw) / 2, y + 2, 1, note, kColWhite);
    }
    const int iw = w - 8;
    const int cx = x + w / 2;
    const int my = y + 11;
    c.hline(x + 4, my, iw, kColDim);          // meter line
    c.vline(cx, my - 2, 5, kColDim);          // center tick
    float cn = cents < -50.f ? -50.f : (cents > 50.f ? 50.f : cents);
    if (cn > -3.0f && cn < 3.0f) {
        c.fill(cx - 3, my - 2, 6, 5, kColAcc2); // in-tune block
    } else {
        const int nx = cx + int(cn * (iw / 2 - 2) / 50.0f);
        c.fill(nx - 1, my - 2, 3, 5, kColAcc2); // needle
    }
}

// ── PadGridWidget ───────────────────────────────────────────────────

void PadGridWidget::draw(Canvas565& c) const {
    c.fill(x, y, 64, 64, kColBlack);
    for (int i = 0; i < 16; ++i) {
        const int px = x + (i % 4) * 16;
        const int py = y + (i / 4) * 16;
        switch (pads[i]) {
        case kPadLoaded:
            c.frame(px, py, 16, 16, kColGray);
            c.fill(px + 4, py + 4, 8, 8, kColDim);
            break;
        case kPadPlaying:
            c.fill(px, py, 16, 16, kColWhite);
            break;
        case kPadMuted:
            c.frame(px, py, 16, 16, kColDim);
            c.line(px + 3, py + 3, px + 12, py + 12, kColDim);
            c.line(px + 12, py + 3, px + 3, py + 12, kColDim);
            break;
        default: // kPadEmpty
            c.frame(px, py, 16, 16, kColDim);
            break;
        }
    }
}

// ── TabBarWidget ────────────────────────────────────────────────────

void TabBarWidget::draw(Canvas565& c, Font5x7& font) const {
    const int w = tabW * count;
    c.fill(x, y, w, 8, kColBlack);
    for (int i = 0; i < count; ++i) {
        const int tx = x + i * tabW;
        const bool act = (i == active);
        if (act) c.fill(tx, y, tabW, 8, kColWhite);
        if (tabs && tabs[i]) {
            const int tw = Canvas565::textWidth(tabs[i], 1);
            c.text(font, tx + (tabW - tw) / 2, y + 1, 1, tabs[i],
                   act ? kColBlack : kColGray);
        }
    }
    c.hline(x, y + 7, w, kColDim); // separator under the strip
}

// ── VirtualKeyboardWidget ───────────────────────────────────────────

// Key rows: 10 digits, 10+9 letters, then 7 letters + space + backspace
// + enter = 39 keys total (matching the panel's 39-key count).
static const char* const kKbdRows[3] = {"0123456789", "QWERTYUIOP",
                                        "ASDFGHJKL"};
static const char* const kKbdBottom = "ZXCVBNM"; // + space/bksp/enter

char VirtualKeyboardWidget::keyAt(int index, bool shift) {
    if (index < 0 || index >= kKeyCount) return 0;
    if (index < 10) return kKbdRows[0][index];
    if (index < 20) {
        const char ch = kKbdRows[1][index - 10];
        return shift ? ch : char(ch - 'A' + 'a');
    }
    if (index < 29) {
        const char ch = kKbdRows[2][index - 20];
        return shift ? ch : char(ch - 'A' + 'a');
    }
    if (index < 36) {
        const char ch = kKbdBottom[index - 29];
        return shift ? ch : char(ch - 'A' + 'a');
    }
    if (index == 36) return ' ';
    if (index == 37) return '\b';
    return '\n'; // 38 = enter
}

void VirtualKeyboardWidget::draw(Canvas565& c, Font5x7& font) const {
    c.fill(x, y, 240, 64, kColBlack);
    // text field
    c.frame(x + 4, y + 2, 232, 12, kColDim);
    if (text) c.text(font, x + 8, y + 5, 1, text, kColWhite);
    const int curX = x + 8 + (text ? textCursor : 0) * 6;
    c.vline(curX, y + 4, 9, kColWhite); // cursor bar
    // key rows
    int idx = 0;
    for (int row = 0; row < 4; ++row) {
        const int ry = y + 18 + row * 12;
        int kx = x + 8;
        if (row < 3) {
            const char* keys = kKbdRows[row];
            for (const char* p = keys; *p; ++p, ++idx) {
                const bool cur = (idx == keyCursor);
                if (cur) c.fill(kx - 1, ry - 1, 10, 10, kColWhite);
                const char ch = shift ? *p : char(*p - (row ? 'A' - 'a' : 0));
                char s[2] = {row == 0 ? *p : ch, 0};
                c.text(font, kx, ry, 1, s, cur ? kColBlack : kColWhite);
                kx += 16;
            }
        } else {
            // bottom row: 7 letters + space + backspace + enter
            for (const char* p = kKbdBottom; *p; ++p, ++idx) {
                const bool cur = (idx == keyCursor);
                if (cur) c.fill(kx - 1, ry - 1, 10, 10, kColWhite);
                char s[2] = {shift ? *p : char(*p - 'A' + 'a'), 0};
                c.text(font, kx, ry, 1, s, cur ? kColBlack : kColWhite);
                kx += 16;
            }
            // space (wide framed cell)
            if (idx == keyCursor) c.fill(kx - 1, ry - 1, 50, 10, kColWhite);
            c.frame(kx, ry, 48, 8, idx == keyCursor ? kColBlack : kColGray);
            ++idx; kx += 52;
            // backspace (←)
            if (idx == keyCursor) c.fill(kx - 1, ry - 1, 10, 10, kColWhite);
            c.text(font, kx, ry, 1, "\xE2\x86\x90", // U+2190
                   idx == keyCursor ? kColBlack : kColWhite);
            ++idx; kx += 16;
            // enter (OK)
            if (idx == keyCursor) c.fill(kx - 1, ry - 1, 14, 10, kColWhite);
            c.text(font, kx, ry, 1, "OK",
                   idx == keyCursor ? kColBlack : kColWhite);
        }
    }
}

// ── EditBoxWidget ───────────────────────────────────────────────────

void EditBoxWidget::draw(Canvas565& c, Font5x7& font) const {
    if (label) c.text(font, x, y - 8, 1, label, kColDim); // above bbox
    c.fill(x, y, w, h, kColBlack);
    c.frame(x, y, w, h, focused ? kColWhite : kColDim);
    const int cells = visibleCells(w);
    const int ty = y + (h - Font5x7::kH) / 2;

    if ((!text || !text[0]) && placeholder) {
        // dim placeholder, clipped to the visible cell count
        const char* p = placeholder;
        for (int i = 0; i < cells && *p; ++i) {
            const uint32_t cp = utf8Next(p);
            for (int gx = 0; gx < Font5x7::kW; ++gx)
                for (int gy = 0; gy < Font5x7::kH; ++gy)
                    if (font.pixel(cp, gx, gy))
                        c.pixel(x + 1 + i * 6 + gx, ty + gy, kColDim);
        }
    }

    // visible window of text with an inverse block cursor
    int total = 0;   // total codepoints (for append-cursor detection)
    int drawn = 0;   // cells drawn in this pass
    const char* p = text ? text : "";
    while (*p) {
        const uint32_t cp = utf8Next(p);
        if (total >= scrollOffset && drawn < cells) {
            const int cellX = x + 1 + drawn * 6;
            const bool cur = (total == cursor);
            if (cur) c.fill(cellX, ty, 6, Font5x7::kH, kColWhite);
            for (int gx = 0; gx < Font5x7::kW; ++gx)
                for (int gy = 0; gy < Font5x7::kH; ++gy)
                    if (font.pixel(cp, gx, gy))
                        c.pixel(cellX + gx, ty + gy,
                                cur ? kColBlack : kColWhite);
            ++drawn;
        }
        ++total;
    }
    // append cursor: one past the last character, if in view
    if (cursor == total && cursor >= scrollOffset && drawn < cells)
        c.fill(x + 1 + drawn * 6, ty, 6, Font5x7::kH, kColWhite);
}

} // namespace gb
