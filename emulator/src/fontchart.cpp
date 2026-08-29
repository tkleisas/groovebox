#include "fontchart.h"

#include "font5x7.h"

#include <cstdio>
#include <cstring>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gb {
namespace {

// Minimal RGB888 canvas with the same 5x7 glyph drawing style as the
// device renderer (which is RGB565-only and fixed at 240x160).
struct Canvas {
    int w, h;
    std::vector<unsigned char> px; // RGB888, black background

    Canvas(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * size_t(h_) * 3, 0) {}

    void fill(int x, int y, int rw, int rh, unsigned char v) {
        for (int yy = y; yy < y + rh; ++yy)
            for (int xx = x; xx < x + rw; ++xx) {
                if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                const size_t i = (size_t(yy) * size_t(w) + size_t(xx)) * 3;
                px[i] = px[i + 1] = px[i + 2] = v;
            }
    }

    void glyph(const Font5x7& f, uint32_t cp, int x, int y, int scale,
               unsigned char v) {
        for (int gx = 0; gx < Font5x7::kW; ++gx)
            for (int gy = 0; gy < Font5x7::kH; ++gy)
                if (f.pixel(cp, gx, gy))
                    fill(x + gx * scale, y + gy * scale, scale, scale, v);
    }

    void text(const Font5x7& f, int x, int y, int scale, const char* s,
              unsigned char v) {
        const char* p = s;
        int cx = x;
        while (*p) {
            const uint32_t cp = utf8Next(p);
            if (cp != ' ') glyph(f, cp, cx, y, scale, v);
            cx += (Font5x7::kW + 1) * scale;
        }
    }
};

struct Section {
    const char* title;
    uint32_t lo, hi;     // main codepoint range (ignored when useSymbols)
    uint32_t lo2, hi2;   // optional second range (skipped when lo2 == 0)
    bool useSymbols;     // enumerate the font's sparse symbol table
};

// Codepoints to draw for a section (undefined ones skipped).
std::vector<uint32_t> sectionCodepoints(const Font5x7& f, const Section& s) {
    std::vector<uint32_t> cps;
    if (s.useSymbols) {
        for (int i = 0; i < f.symbolCount(); ++i)
            cps.push_back(f.symbolAt(i));
        return cps;
    }
    for (uint32_t cp = s.lo; cp <= s.hi; ++cp)
        if (f.defined(cp)) cps.push_back(cp);
    for (uint32_t cp = s.lo2; cp <= s.hi2 && s.lo2 != 0; ++cp)
        if (f.defined(cp)) cps.push_back(cp);
    return cps;
}

} // namespace

bool writeFontChartPng(const char* path) {
    Font5x7 font;
    static const Section kSections[] = {
        {"LATIN ASCII",         0x21,  0x7E,  0,      0,      false},
        {"GREEK UPPERCASE",     0x391, 0x3A9, 0,      0,      false},
        {"GREEK LOWER+PUNCT",   0x3AC, 0x3CE, 0x37E,  0x390,  false},
        {"CYRILLIC UPPERCASE",  0x400, 0x42F, 0x490,  0x490,  false},
        {"CYRILLIC LOWERCASE",  0x430, 0x45F, 0x491,  0x491,  false},
        {"SYMBOLS",             0,     0,     0,      0,      true},
    };
    constexpr int kNumSections = int(sizeof(kSections) / sizeof(kSections[0]));

    // Coverage check: every printable ASCII codepoint must be defined.
    int missing = 0;
    for (uint32_t cp = 0x20; cp <= 0x7E; ++cp) {
        if (!font.defined(cp)) {
            std::printf("[fontchart] STRAGGLER: U+%04X not defined\n", cp);
            ++missing;
        }
    }
    std::printf("[fontchart] printable ASCII 0x20-0x7E: %s\n",
                missing == 0 ? "fully defined (95/95)"
                             : "stragglers found");

    const int kCols   = 12;
    const int kCellW  = 40;  // glyph 20px @4x + slack
    const int kCellH  = 50;  // glyph 28px @4x + 2 + label 7px + padding
    const int kMargin = 10;
    const int kHeaderH = 14; // section title at 2x
    const int kGapAfterHeader = 8;
    const int kGapAfterSection = 14;

    // Layout pass: canvas height from the number of drawn glyphs.
    std::vector<uint32_t> cps[kNumSections];
    int H = kMargin;
    for (int si = 0; si < kNumSections; ++si) {
        cps[si] = sectionCodepoints(font, kSections[si]);
        const int rows = (int(cps[si].size()) + kCols - 1) / kCols;
        H += kHeaderH + kGapAfterHeader + rows * kCellH + kGapAfterSection;
    }
    H += kMargin - kGapAfterSection;
    const int W = 2 * kMargin + kCols * kCellW;

    Canvas c(W, H);
    int y = kMargin;
    for (int si = 0; si < kNumSections; ++si) {
        c.text(font, kMargin, y, 2, kSections[si].title, 255);
        y += kHeaderH + kGapAfterHeader;
        int cx = kMargin, rowY = y, col = 0;
        for (const uint32_t cp : cps[si]) {
            c.glyph(font, cp, cx + (kCellW - Font5x7::kW * 4) / 2, rowY, 4, 255);
            char label[12];
            std::snprintf(label, sizeof(label), "U+%X", cp);
            const int lw = int(std::strlen(label)) * (Font5x7::kW + 1);
            c.text(font, cx + (kCellW - lw) / 2, rowY + Font5x7::kH * 4 + 3,
                   1, label, 150);
            if (++col == kCols) { col = 0; cx = kMargin; rowY += kCellH; }
            else cx += kCellW;
        }
        // If the last glyph exactly filled a row, the wrap above
        // advanced rowY one row too far — undo it.
        if (col == 0) rowY -= kCellH;
        y = rowY + kCellH + kGapAfterSection;
    }

    const int ok = stbi_write_png(path, c.w, c.h, 3, c.px.data(), c.w * 3);
    if (ok) std::printf("[fontchart] wrote %s (%dx%d)\n", path, c.w, c.h);
    else    std::fprintf(stderr, "[fontchart] FAILED to write %s\n", path);
    return ok != 0;
}

} // namespace gb
