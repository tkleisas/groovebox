#pragma once

// Minimal embedded 5x7 bitmap font — no font files, no SDL_ttf.
// Glyphs are defined as ASCII art (5 columns x 7 rows, '#' = set) and
// packed into column bytes at startup.
//
// Coverage:
//   * ASCII 0x20..0x7E — full printable ASCII: A-Z, a-z (true
//     lowercase with descenders on g j p q y), digits, all punctuation
//   * Greek   U+0370..U+03FF — full alphabet upper+lowercase, final
//     sigma, tonos vowels (ά έ ή ί ό ύ ώ), dialytika (ΐ ΰ ϊ ϋ),
//     ano teleia U+0387, Greek question mark U+037E
//   * Cyrillic U+0400..U+04FF — Russian alphabet upper+lowercase,
//     Ё/ё, plus the Ukrainian extras (Єє Іі Її Ґґ)
//   * Symbols — sparse table (binary search): arrows U+2190-2193,
//     media/triangles (▶ ◀ ▲ ▼ ■ ● ⏸), step-sequencer squares
//     (■ □ ▪ ▫), block elements U+2581-2588 + ▌ ▐ for meters,
//     musical ♪ ♫, utility ✓ × ± •
// Glyphs identical to a Latin one (Β, Е, Н, А, О, Р, С, …) alias the
// Latin art. Most Cyrillic lowercase letters are small-caps derived
// programmatically from their uppercase shape (top 5 rows shifted to
// x-height); the genuinely distinct ones (б е у й ё) are hand-drawn.
// Anything unmapped renders as a hollow box.

#include <array>
#include <cstddef>
#include <cstdint>

namespace gb {

// ── Tiny UTF-8 decoder ──────────────────────────────────────────────
// Reads one codepoint from *p (NUL-terminated) and advances p past it.
// Handles 1-4 byte sequences; malformed bytes yield U+FFFD and advance
// by one byte so a bad sequence can't stall the walker. Never reads
// past the NUL terminator (continuation checks short-circuit on it).
inline uint32_t utf8Next(const char*& p) {
    const auto* s = reinterpret_cast<const unsigned char*>(p);
    uint32_t cp;
    size_t len;
    if (s[0] < 0x80) {
        cp = s[0]; len = 1;
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = (uint32_t(s[0] & 0x1F) << 6) | uint32_t(s[1] & 0x3F); len = 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 &&
               (s[2] & 0xC0) == 0x80) {
        cp = (uint32_t(s[0] & 0x0F) << 12) | (uint32_t(s[1] & 0x3F) << 6) |
             uint32_t(s[2] & 0x3F);
        len = 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
               (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        cp = (uint32_t(s[0] & 0x07) << 18) | (uint32_t(s[1] & 0x3F) << 12) |
             (uint32_t(s[2] & 0x3F) << 6) | uint32_t(s[3] & 0x3F);
        len = 4;
    } else {
        cp = 0xFFFD; len = 1;
    }
    p += len;
    return cp;
}

// Number of codepoints in a NUL-terminated UTF-8 string.
inline int utf8Length(const char* s) {
    int n = 0;
    while (*s) { utf8Next(s); ++n; }
    return n;
}

class Font5x7 {
public:
    static constexpr int kW = 5;
    static constexpr int kH = 7;

    Font5x7() {
        m_ascii.fill({0, 0, 0, 0, 0});
        m_greek.fill({0, 0, 0, 0, 0});
        m_cyr.fill({0, 0, 0, 0, 0});
        buildAscii();
        buildGreek();
        buildCyrillic();
        buildSymbols();
    }

    // bit y of column x (y=0 is the top row) for Unicode codepoint cp
    bool pixel(uint32_t cp, int x, int y) const {
        const Glyph* g = findGlyph(cp);
        if (!g || isEmpty(*g)) g = &kBox;   // unknown → hollow box
        return (g->col[x] >> y) & 1u;
    }

    // True when cp maps to a glyph that has actual art (i.e. would not
    // render as the hollow replacement box). Space counts as defined
    // even though its glyph is all-zero.
    bool defined(uint32_t cp) const {
        if (cp == ' ') return true;
        const Glyph* g = findGlyph(cp);
        return g && !isEmpty(*g);
    }

    // Sparse symbol table enumeration (for the font chart).
    int symbolCount() const { return m_symCount; }
    uint32_t symbolAt(int i) const {
        return (i >= 0 && i < m_symCount) ? m_syms[i].cp : 0;
    }

private:
    struct Glyph { uint8_t col[kW]; };

    static bool isEmpty(const Glyph& g) {
        for (int i = 0; i < kW; ++i) if (g.col[i]) return false;
        return true;
    }

    // Hollow replacement box.
    static constexpr Glyph kBox = {{0x7F, 0x41, 0x41, 0x41, 0x7F}};

    // Range routing: flat tables for the dense blocks, binary search
    // over the sparse symbol table for everything else.
    const Glyph* findGlyph(uint32_t cp) const {
        if (cp < 0x80)                      return &m_ascii[cp];
        if (cp >= 0x370 && cp < 0x400)      return &m_greek[cp - 0x370];
        if (cp >= 0x400 && cp < 0x500)      return &m_cyr[cp - 0x400];
        return findSymbol(cp);
    }

    struct Sym { uint16_t cp; Glyph g; };
    const Glyph* findSymbol(uint32_t cp) const {
        int lo = 0, hi = m_symCount - 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            if (m_syms[mid].cp == cp) return &m_syms[mid].g;
            if (m_syms[mid].cp < cp) lo = mid + 1;
            else                     hi = mid - 1;
        }
        return nullptr;
    }
    // Entries MUST be appended in ascending codepoint order.
    void addSym(uint32_t cp, const Glyph& g) {
        if (m_symCount < kMaxSyms)
            m_syms[m_symCount++] = {static_cast<uint16_t>(cp), g};
    }

    using Rows = const char* const (&)[kH];

    static Glyph pack(const char* r0, const char* r1, const char* r2,
                      const char* r3, const char* r4, const char* r5, const char* r6) {
        const char* rows[kH] = {r0, r1, r2, r3, r4, r5, r6};
        Glyph g{};
        for (int x = 0; x < kW; ++x)
            for (int y = 0; y < kH; ++y)
                if (rows[y][x] == '#') g.col[x] |= uint8_t(1u << y);
        return g;
    }

    // Small-caps: top 5 rows of src shifted down to rows 2..6.
    static Glyph smallCaps(const Glyph& src) {
        Glyph g{};
        for (int x = 0; x < kW; ++x)
            g.col[x] = uint8_t((src.col[x] & 0x1Fu) << 2);
        return g;
    }

    // ── ASCII ───────────────────────────────────────────────────────
    void buildAscii() {
        auto def = [this](char c, const char* r0, const char* r1, const char* r2,
                          const char* r3, const char* r4, const char* r5, const char* r6) {
            m_ascii[static_cast<unsigned char>(c)] = pack(r0, r1, r2, r3, r4, r5, r6);
        };

        def(' ',
            "     ",
            "     ",
            "     ",
            "     ",
            "     ",
            "     ",
            "     ");
        def('0',
            " ### ",
            "#   #",
            "#  ##",
            "# # #",
            "##  #",
            "#   #",
            " ### ");
        def('1',
            "  #  ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def('2',
            " ### ",
            "#   #",
            "    #",
            "   # ",
            "  #  ",
            " #   ",
            "#####");
        def('3',
            "#####",
            "   # ",
            "  #  ",
            "   # ",
            "    #",
            "#   #",
            " ### ");
        def('4',
            "   # ",
            "  ## ",
            " # # ",
            "#  # ",
            "#####",
            "   # ",
            "   # ");
        def('5',
            "#####",
            "#    ",
            "#### ",
            "    #",
            "    #",
            "#   #",
            " ### ");
        def('6',
            "  ## ",
            " #   ",
            "#    ",
            "#### ",
            "#   #",
            "#   #",
            " ### ");
        def('7',
            "#####",
            "    #",
            "   # ",
            "  #  ",
            " #   ",
            " #   ",
            " #   ");
        def('8',
            " ### ",
            "#   #",
            "#   #",
            " ### ",
            "#   #",
            "#   #",
            " ### ");
        def('9',
            " ### ",
            "#   #",
            "#   #",
            " ####",
            "    #",
            "   # ",
            " ##  ");
        def('A',
            " ### ",
            "#   #",
            "#   #",
            "#####",
            "#   #",
            "#   #",
            "#   #");
        def('B',
            "#### ",
            "#   #",
            "#   #",
            "#### ",
            "#   #",
            "#   #",
            "#### ");
        def('C',
            " ### ",
            "#   #",
            "#    ",
            "#    ",
            "#    ",
            "#   #",
            " ### ");
        def('D',
            "###  ",
            "#  # ",
            "#   #",
            "#   #",
            "#   #",
            "#  # ",
            "###  ");
        def('E',
            "#####",
            "#    ",
            "#    ",
            "#### ",
            "#    ",
            "#    ",
            "#####");
        def('F',
            "#####",
            "#    ",
            "#    ",
            "#### ",
            "#    ",
            "#    ",
            "#    ");
        def('G',
            " ### ",
            "#   #",
            "#    ",
            "# ###",
            "#   #",
            "#   #",
            " ### ");
        def('H',
            "#   #",
            "#   #",
            "#   #",
            "#####",
            "#   #",
            "#   #",
            "#   #");
        def('I',
            " ### ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def('J',
            "  ###",
            "   # ",
            "   # ",
            "   # ",
            "   # ",
            "#  # ",
            " ##  ");
        def('K',
            "#   #",
            "#  # ",
            "# #  ",
            "##   ",
            "# #  ",
            "#  # ",
            "#   #");
        def('L',
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#####");
        def('M',
            "#   #",
            "## ##",
            "# # #",
            "# # #",
            "#   #",
            "#   #",
            "#   #");
        def('N',
            "#   #",
            "#   #",
            "##  #",
            "# # #",
            "#  ##",
            "#   #",
            "#   #");
        def('O',
            " ### ",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def('P',
            "#### ",
            "#   #",
            "#   #",
            "#### ",
            "#    ",
            "#    ",
            "#    ");
        def('Q',
            " ### ",
            "#   #",
            "#   #",
            "#   #",
            "# # #",
            "#  # ",
            " ## #");
        def('R',
            "#### ",
            "#   #",
            "#   #",
            "#### ",
            "# #  ",
            "#  # ",
            "#   #");
        def('S',
            " ####",
            "#    ",
            "#    ",
            " ### ",
            "    #",
            "    #",
            "#### ");
        def('T',
            "#####",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ");
        def('U',
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def('V',
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " # # ",
            "  #  ");
        def('W',
            "#   #",
            "#   #",
            "#   #",
            "# # #",
            "# # #",
            "## ##",
            "#   #");
        def('X',
            "#   #",
            "#   #",
            " # # ",
            "  #  ",
            " # # ",
            "#   #",
            "#   #");
        def('Y',
            "#   #",
            "#   #",
            " # # ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ");
        def('Z',
            "#####",
            "    #",
            "   # ",
            "  #  ",
            " #   ",
            "#    ",
            "#####");
        def(':',
            "     ",
            "  #  ",
            "  #  ",
            "     ",
            "  #  ",
            "  #  ",
            "     ");
        def('.',
            "     ",
            "     ",
            "     ",
            "     ",
            "     ",
            " ##  ",
            " ##  ");
        def('%',
            "##  #",
            "##  #",
            "   # ",
            "  #  ",
            " #   ",
            "#  ##",
            "#  ##");
        def('-',
            "     ",
            "     ",
            "     ",
            "#####",
            "     ",
            "     ",
            "     ");
        def('/',
            "    #",
            "    #",
            "   # ",
            "  #  ",
            " #   ",
            "#    ",
            "#    ");
        def('<',
            "   # ",
            "  #  ",
            " #   ",
            "#    ",
            " #   ",
            "  #  ",
            "   # ");
        def('>',
            " #   ",
            "  #  ",
            "   # ",
            "    #",
            "   # ",
            "  #  ",
            " #   ");
        def('(',
            "   # ",
            "  #  ",
            " #   ",
            " #   ",
            " #   ",
            "  #  ",
            "   # ");
        def(')',
            " #   ",
            "  #  ",
            "   # ",
            "   # ",
            "   # ",
            "  #  ",
            " #   ");
        def('+',
            "     ",
            "  #  ",
            "  #  ",
            "#####",
            "  #  ",
            "  #  ",
            "     ");
        def('#',
            " # # ",
            " # # ",
            "#####",
            " # # ",
            "#####",
            " # # ",
            " # # ");
        def('!',
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "     ",
            "  #  ");
        def('"',
            " # # ",
            " # # ",
            " # # ",
            "     ",
            "     ",
            "     ",
            "     ");
        def('$',
            "  #  ",
            " ####",
            "# #  ",
            " ### ",
            "  # #",
            "#### ",
            "  #  ");
        def('&',
            " ##  ",
            "#  # ",
            "# #  ",
            " #   ",
            "# # #",
            "#  # ",
            " ## #");
        def('\'',
            "  #  ",
            "  #  ",
            " #   ",
            "     ",
            "     ",
            "     ",
            "     ");
        def('*',
            "     ",
            "  #  ",
            "# # #",
            " ### ",
            "# # #",
            "  #  ",
            "     ");
        def(',',
            "     ",
            "     ",
            "     ",
            "     ",
            " ##  ",
            " ##  ",
            " #   ");
        def(';',
            "     ",
            "  #  ",
            "  #  ",
            "     ",
            " ##  ",
            " ##  ",
            " #   ");
        def('=',
            "     ",
            "     ",
            "#####",
            "     ",
            "#####",
            "     ",
            "     ");
        def('?',
            " ### ",
            "#   #",
            "    #",
            "   # ",
            "  #  ",
            "     ",
            "  #  ");
        def('@',
            " ### ",
            "#   #",
            "# ###",
            "# # #",
            "# ## ",
            "#    ",
            " ### ");
        def('[',
            " ### ",
            " #   ",
            " #   ",
            " #   ",
            " #   ",
            " #   ",
            " ### ");
        def('\\',
            "#    ",
            "#    ",
            " #   ",
            "  #  ",
            "   # ",
            "    #",
            "    #");
        def(']',
            " ### ",
            "   # ",
            "   # ",
            "   # ",
            "   # ",
            "   # ",
            " ### ");
        def('^',
            "  #  ",
            " # # ",
            "#   #",
            "     ",
            "     ",
            "     ",
            "     ");
        def('_',
            "     ",
            "     ",
            "     ",
            "     ",
            "     ",
            "     ",
            "#####");
        def('`',
            " #   ",
            "  #  ",
            "     ",
            "     ",
            "     ",
            "     ",
            "     ");
        def('{',
            "   ##",
            "  #  ",
            "  #  ",
            " ##  ",
            "  #  ",
            "  #  ",
            "   ##");
        def('|',
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ");
        def('}',
            "##   ",
            "  #  ",
            "  #  ",
            "  ## ",
            "  #  ",
            "  #  ",
            "##   ");
        def('~',
            "     ",
            "     ",
            " ## #",
            "# ## ",
            "     ",
            "     ",
            "     ");

        // Lowercase a-z: x-height rows 2..6 (matching the Greek /
        // Cyrillic lowercase), ascenders to row 0, descenders (g j p
        // q y) to row 6.
        def('a',
            "     ",
            "     ",
            " ### ",
            "    #",
            " ####",
            "#   #",
            " ####");
        def('b',
            "#    ",
            "#    ",
            "#### ",
            "#   #",
            "#   #",
            "#   #",
            "#### ");
        def('c',
            "     ",
            "     ",
            " ### ",
            "#   #",
            "#    ",
            "#   #",
            " ### ");
        def('d',
            "    #",
            "    #",
            " ####",
            "#   #",
            "#   #",
            "#   #",
            " ####");
        def('e',
            "     ",
            "     ",
            " ### ",
            "#   #",
            "#####",
            "#    ",
            " ### ");
        def('f',
            "  ## ",
            " #  #",
            " #   ",
            "###  ",
            " #   ",
            " #   ",
            " #   ");
        def('g',
            "     ",
            " ####",
            "#   #",
            "#   #",
            " ####",
            "    #",
            " ### ");
        def('h',
            "#    ",
            "#    ",
            "#### ",
            "#   #",
            "#   #",
            "#   #",
            "#   #");
        def('i',
            "  #  ",
            "     ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def('j',
            "   # ",
            "     ",
            "  ## ",
            "   # ",
            "   # ",
            "#  # ",
            " ##  ");
        def('k',
            "#    ",
            "#    ",
            "#  # ",
            "# #  ",
            "##   ",
            "# #  ",
            "#  # ");
        def('l',
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def('m',
            "     ",
            "     ",
            "## # ",
            "# # #",
            "# # #",
            "# # #",
            "# # #");
        def('n',
            "     ",
            "     ",
            "#### ",
            "#   #",
            "#   #",
            "#   #",
            "#   #");
        def('o',
            "     ",
            "     ",
            " ### ",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def('p',
            "     ",
            "     ",
            "#### ",
            "#   #",
            "#   #",
            "#### ",
            "#    ");
        def('q',
            "     ",
            "     ",
            " ####",
            "#   #",
            "#   #",
            " ####",
            "    #");
        def('r',
            "     ",
            "     ",
            "# ## ",
            "##  #",
            "#    ",
            "#    ",
            "#    ");
        def('s',
            "     ",
            "     ",
            " ####",
            "#    ",
            " ### ",
            "    #",
            "#### ");
        def('t',
            " #   ",
            " #   ",
            "###  ",
            " #   ",
            " #   ",
            " #  #",
            "  ## ");
        def('u',
            "     ",
            "     ",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " ####");
        def('v',
            "     ",
            "     ",
            "#   #",
            "#   #",
            "#   #",
            " # # ",
            "  #  ");
        def('w',
            "     ",
            "     ",
            "#   #",
            "#   #",
            "# # #",
            "# # #",
            " # # ");
        def('x',
            "     ",
            "     ",
            "#   #",
            " # # ",
            "  #  ",
            " # # ",
            "#   #");
        def('y',
            "     ",
            "     ",
            "#   #",
            "#   #",
            "  ###",
            "    #",
            " ### ");
        def('z',
            "     ",
            "     ",
            "#####",
            "   # ",
            "  #  ",
            " #   ",
            "#####");
    }

    // ── Greek (U+0370..U+03FF) ──────────────────────────────────────
    void buildGreek() {
        auto def = [this](uint32_t cp, const char* r0, const char* r1,
                          const char* r2, const char* r3, const char* r4,
                          const char* r5, const char* r6) {
            m_greek[cp - 0x370] = pack(r0, r1, r2, r3, r4, r5, r6);
        };
        auto alias = [this](uint32_t cp, char latin) {
            m_greek[cp - 0x370] = m_ascii[static_cast<unsigned char>(latin)];
        };

        // Uppercase — Latin-identical shapes alias the ASCII art.
        alias(0x391, 'A'); // Α
        alias(0x392, 'B'); // Β
        def(0x393, // Γ
            "#####",
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#    ");
        def(0x394, // Δ
            "  #  ",
            "  #  ",
            " # # ",
            " # # ",
            "#   #",
            "#   #",
            "#####");
        alias(0x395, 'E'); // Ε
        alias(0x396, 'Z'); // Ζ
        alias(0x397, 'H'); // Η
        def(0x398, // Θ
            " ### ",
            "#   #",
            "#   #",
            "#####",
            "#   #",
            "#   #",
            " ### ");
        alias(0x399, 'I'); // Ι
        alias(0x39A, 'K'); // Κ
        def(0x39B, // Λ
            "  #  ",
            "  #  ",
            " # # ",
            " # # ",
            "#   #",
            "#   #",
            "#   #");
        alias(0x39C, 'M'); // Μ
        alias(0x39D, 'N'); // Ν
        def(0x39E, // Ξ
            "#####",
            "     ",
            "     ",
            " ### ",
            "     ",
            "     ",
            "#####");
        alias(0x39F, 'O'); // Ο
        def(0x3A0, // Π
            "#####",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #");
        alias(0x3A1, 'P'); // Ρ
        def(0x3A3, // Σ
            "#####",
            "#    ",
            " #   ",
            "  #  ",
            " #   ",
            "#    ",
            "#####");
        alias(0x3A4, 'T'); // Τ
        alias(0x3A5, 'Y'); // Υ
        def(0x3A6, // Φ
            "  #  ",
            " ### ",
            "# # #",
            "# # #",
            "# # #",
            " ### ",
            "  #  ");
        alias(0x3A7, 'X'); // Χ
        def(0x3A8, // Ψ
            "# # #",
            "# # #",
            "# # #",
            " ### ",
            "  #  ",
            "  #  ",
            "  #  ");
        def(0x3A9, // Ω
            " ### ",
            "#   #",
            "#   #",
            "#   #",
            " # # ",
            " # # ",
            "## ##");

        // Lowercase — hand-drawn at x-height (rows 2..5, descenders to
        // row 6). Latin lowercase doesn't exist in this font, so there
        // is nothing to alias except shapes that stand on their own.
        def(0x3B1, // α
            "     ",
            "     ",
            " ##  ",
            "#  # ",
            "#  # ",
            "#  ##",
            " ## #");
        def(0x3B2, // β
            " ##  ",
            "#  # ",
            "#  # ",
            "###  ",
            "#  # ",
            "#  # ",
            "##   ");
        def(0x3B3, // γ
            "     ",
            "     ",
            "#  # ",
            "#  # ",
            " # # ",
            " # # ",
            "##   ");
        def(0x3B4, // δ
            "  ## ",
            " #   ",
            "  ## ",
            " ### ",
            "#   #",
            "#   #",
            " ### ");
        def(0x3B5, // ε
            "     ",
            "     ",
            " ### ",
            "#    ",
            " ##  ",
            "#    ",
            " ### ");
        def(0x3B6, // ζ
            " ####",
            "    #",
            "   # ",
            "  #  ",
            " #   ",
            "#    ",
            " ##  ");
        def(0x3B7, // η
            "     ",
            "     ",
            "## # ",
            "#  ##",
            "#   #",
            "#   #",
            "    #");
        def(0x3B8, // θ
            " ### ",
            "#   #",
            "#   #",
            "#####",
            "#   #",
            "#   #",
            " ### ");
        def(0x3B9, // ι
            "     ",
            "     ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def(0x3BA, // κ
            "     ",
            "     ",
            "#  # ",
            "# #  ",
            "##   ",
            "# #  ",
            "#  ##");
        def(0x3BB, // λ
            "#    ",
            " #   ",
            " #   ",
            "  #  ",
            " # # ",
            "#   #",
            "#   #");
        def(0x3BC, // μ
            "     ",
            "     ",
            "#   #",
            "#   #",
            "#   #",
            "#### ",
            "#    ");
        def(0x3BD, // ν
            "     ",
            "     ",
            "#   #",
            "#   #",
            "#   #",
            " # # ",
            "  #  ");
        def(0x3BE, // ξ
            " ### ",
            "#    ",
            " ##  ",
            "#    ",
            " ### ",
            "#    ",
            " ##  ");
        def(0x3BF, // ο
            "     ",
            "     ",
            " ### ",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def(0x3C0, // π
            "     ",
            "     ",
            "#####",
            " # # ",
            " # # ",
            " # # ",
            " # # ");
        def(0x3C1, // ρ
            "     ",
            "     ",
            " ##  ",
            "#  # ",
            "#  # ",
            "###  ",
            "#    ");
        def(0x3C2, // ς (final sigma)
            "     ",
            "     ",
            " ### ",
            "#    ",
            "#    ",
            " ### ",
            "  ## ");
        def(0x3C3, // σ
            "     ",
            "     ",
            " ####",
            "#  # ",
            "#   #",
            "#   #",
            " ### ");
        def(0x3C4, // τ
            "     ",
            "     ",
            "#####",
            "  #  ",
            "  #  ",
            "  #  ",
            "  ## ");
        def(0x3C5, // υ
            "     ",
            "     ",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def(0x3C6, // φ
            "     ",
            "  #  ",
            " ### ",
            "# # #",
            "# # #",
            " ### ",
            "  #  ");
        def(0x3C7, // χ
            "     ",
            "     ",
            "#   #",
            " # # ",
            "  #  ",
            " # # ",
            "##   ");
        def(0x3C8, // ψ
            "     ",
            "     ",
            "# # #",
            "# # #",
            " ### ",
            "  #  ",
            "  #  ");
        def(0x3C9, // ω
            "     ",
            "     ",
            "#   #",
            "# # #",
            "# # #",
            "# # #",
            " # # ");

        // Tonos-accented lowercase vowels: base shape + acute accent
        // tucked into the two free rows above x-height.
        def(0x3AC, // ά
            "    #",
            "   # ",
            " ##  ",
            "#  # ",
            "#  # ",
            "#  ##",
            " ## #");
        def(0x3AD, // έ
            "    #",
            "   # ",
            " ### ",
            "#    ",
            " ##  ",
            "#    ",
            " ### ");
        def(0x3AE, // ή
            "    #",
            "   # ",
            "## # ",
            "#  ##",
            "#   #",
            "#   #",
            "    #");
        def(0x3AF, // ί
            "   # ",
            "  #  ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def(0x3CC, // ό
            "    #",
            "   # ",
            " ### ",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def(0x3CD, // ύ
            "    #",
            "   # ",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def(0x3CE, // ώ
            "    #",
            "   # ",
            "#   #",
            "# # #",
            "# # #",
            "# # #",
            " # # ");

        // Punctuation + dialytika combos.
        def(0x387, // · ano teleia (mid dot)
            "     ",
            "     ",
            " ##  ",
            " ##  ",
            "     ",
            "     ",
            "     ");
        def(0x37E, // ; Greek question mark (erotimatiko)
            "     ",
            " ##  ",
            " ##  ",
            "     ",
            " ##  ",
            " #   ",
            "#    ");
        def(0x390, // ΐ (iota dialytika + tonos — approximation:
                   // diaeresis dots with a centre tick)
            " # # ",
            "  #  ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def(0x3B0, // ΰ (upsilon dialytika + tonos)
            " # # ",
            "  #  ",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
        def(0x3CA, // ϊ
            " # # ",
            "     ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def(0x3CB, // ϋ
            " # # ",
            "     ",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            " ### ");
    }

    // ── Cyrillic (U+0400..U+04FF) ───────────────────────────────────
    void buildCyrillic() {
        auto def = [this](uint32_t cp, const char* r0, const char* r1,
                          const char* r2, const char* r3, const char* r4,
                          const char* r5, const char* r6) {
            m_cyr[cp - 0x400] = pack(r0, r1, r2, r3, r4, r5, r6);
        };
        auto alias = [this](uint32_t cp, char latin) {
            m_cyr[cp - 0x400] = m_ascii[static_cast<unsigned char>(latin)];
        };
        // Lowercase small-caps derived from the uppercase Cyrillic.
        auto sc = [this](uint32_t dst, uint32_t srcUpper) {
            m_cyr[dst - 0x400] = smallCaps(m_cyr[srcUpper - 0x400]);
        };

        // Uppercase — Latin-identical shapes alias the ASCII art.
        alias(0x410, 'A'); // А
        def(0x411, // Б
            "#### ",
            "#    ",
            "#    ",
            "###  ",
            "#  # ",
            "#  # ",
            "###  ");
        alias(0x412, 'B'); // В
        def(0x413, // Г
            "#####",
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#    ");
        def(0x414, // Д
            "  ## ",
            " # # ",
            " # # ",
            " # # ",
            " # # ",
            "#####",
            "#   #");
        alias(0x415, 'E'); // Е
        def(0x401, // Ё
            "#   #",
            "     ",
            "#####",
            "#    ",
            "#### ",
            "#    ",
            "#####");
        def(0x416, // Ж
            "# # #",
            "# # #",
            " ### ",
            "#####",
            " ### ",
            "# # #",
            "# # #");
        def(0x417, // З
            " ### ",
            "#   #",
            "    #",
            "  ## ",
            "    #",
            "#   #",
            " ### ");
        def(0x418, // И
            "#   #",
            "#  ##",
            "#  ##",
            "# # #",
            "##  #",
            "##  #",
            "#   #");
        def(0x419, // Й
            " # # ",
            "  #  ",
            "#   #",
            "#  ##",
            "# # #",
            "##  #",
            "#   #");
        alias(0x41A, 'K'); // К
        def(0x41B, // Л
            " ### ",
            " #  #",
            " #  #",
            " #  #",
            " #  #",
            "#   #",
            "#   #");
        alias(0x41C, 'M'); // М
        alias(0x41D, 'H'); // Н
        alias(0x41E, 'O'); // О
        def(0x41F, // П
            "#####",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #");
        alias(0x420, 'P'); // Р
        alias(0x421, 'C'); // С
        alias(0x422, 'T'); // Т
        alias(0x423, 'Y'); // У (cap-height approximation of the Y-shape)
        def(0x424, // Ф
            "  #  ",
            " ### ",
            "# # #",
            "# # #",
            "# # #",
            " ### ",
            "  #  ");
        alias(0x425, 'X'); // Х
        def(0x426, // Ц
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#   #",
            "#####",
            "    #");
        def(0x427, // Ч
            "#   #",
            "#   #",
            "#   #",
            " ####",
            "    #",
            "    #",
            "    #");
        def(0x428, // Ш
            "# # #",
            "# # #",
            "# # #",
            "# # #",
            "# # #",
            "# # #",
            "#####");
        def(0x429, // Щ
            "# # #",
            "# # #",
            "# # #",
            "# # #",
            "# # #",
            "#####",
            "    #");
        def(0x42A, // Ъ
            "##   ",
            " #   ",
            " #   ",
            " ### ",
            " #  #",
            " #  #",
            " ### ");
        def(0x42B, // Ы
            "#   #",
            "#   #",
            "#   #",
            "### #",
            "#  ##",
            "#  ##",
            "### #");
        def(0x42C, // Ь
            "#    ",
            "#    ",
            "#    ",
            "###  ",
            "#  # ",
            "#  # ",
            "###  ");
        def(0x42D, // Э
            " ### ",
            "#   #",
            "    #",
            "  ###",
            "    #",
            "#   #",
            " ### ");
        def(0x42E, // Ю
            "#  ##",
            "# # #",
            "# # #",
            "### #",
            "# # #",
            "# # #",
            "#  ##");
        def(0x42F, // Я
            " ####",
            "#   #",
            "#   #",
            " ####",
            "  ## ",
            " #  #",
            "#   #");

        // Ukrainian extras (uppercase).
        def(0x404, // Є
            " ### ",
            "#   #",
            "#    ",
            "#### ",
            "#    ",
            "#   #",
            " ### ");
        alias(0x406, 'I'); // І
        def(0x407, // Ї
            "#   #",
            "     ",
            " ### ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def(0x490, // Ґ (Г with upturn)
            "#####",
            "    #",
            "#    ",
            "#    ",
            "#    ",
            "#    ",
            "#    ");

        // Lowercase — small-caps of the uppercase shape (rows 2..6),
        // except the genuinely distinct forms drawn by hand below.
        sc(0x430, 0x410); // а
        sc(0x432, 0x412); // в
        sc(0x433, 0x413); // г
        sc(0x434, 0x414); // д
        sc(0x436, 0x416); // ж
        sc(0x437, 0x417); // з
        sc(0x438, 0x418); // и
        sc(0x43A, 0x41A); // к
        sc(0x43B, 0x41B); // л
        sc(0x43C, 0x41C); // м
        sc(0x43D, 0x41D); // н
        sc(0x43E, 0x41E); // о
        sc(0x43F, 0x41F); // п
        sc(0x440, 0x420); // р
        sc(0x441, 0x421); // с
        sc(0x442, 0x422); // т
        sc(0x444, 0x424); // ф
        sc(0x445, 0x425); // х
        sc(0x446, 0x426); // ц
        sc(0x447, 0x427); // ч
        sc(0x448, 0x428); // ш
        sc(0x449, 0x429); // щ
        sc(0x44A, 0x42A); // ъ
        sc(0x44B, 0x42B); // ы
        sc(0x44C, 0x42C); // ь
        sc(0x44D, 0x42D); // э
        sc(0x44E, 0x42E); // ю
        sc(0x44F, 0x42F); // я
        sc(0x454, 0x404); // є
        sc(0x491, 0x490); // ґ

        def(0x431, // б
            "  #  ",
            " #   ",
            "#    ",
            "###  ",
            "#  # ",
            "#  # ",
            " ##  ");
        def(0x435, // е
            "     ",
            "     ",
            " ### ",
            "#   #",
            "#####",
            "#    ",
            " ### ");
        def(0x443, // у
            "     ",
            "     ",
            "#   #",
            "#   #",
            "  ###",
            "    #",
            " ### ");
        def(0x439, // й
            " # # ",
            "  #  ",
            "#   #",
            "#  ##",
            "# # #",
            "##  #",
            "#   #");
        def(0x451, // ё
            "#   #",
            "     ",
            " ### ",
            "#   #",
            "#####",
            "#    ",
            " ### ");

        // Ukrainian extras (lowercase, hand-drawn where distinct).
        def(0x456, // і
            "  #  ",
            "     ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
        def(0x457, // ї
            " # # ",
            "     ",
            " ##  ",
            "  #  ",
            "  #  ",
            "  #  ",
            " ### ");
    }

    // ── Symbols (sparse, binary-searched) ───────────────────────────
    // addSym entries MUST stay in ascending codepoint order.
    void buildSymbols() {
        auto def = [this](uint32_t cp, const char* r0, const char* r1,
                          const char* r2, const char* r3, const char* r4,
                          const char* r5, const char* r6) {
            addSym(cp, pack(r0, r1, r2, r3, r4, r5, r6));
        };
        // Vertical meter blocks: n rows filled from the bottom.
        auto block = [this](uint32_t cp, int rows) {
            Glyph g{};
            const uint8_t m = uint8_t(0x7Fu << (kH - rows)) & 0x7Fu;
            for (int x = 0; x < kW; ++x) g.col[x] = m;
            addSym(cp, g);
        };
        auto halfBlock = [this](uint32_t cp, bool left) {
            Glyph g{};
            for (int x = 0; x < kW; ++x)
                if (left ? x < 2 : x >= kW - 2) g.col[x] = 0x7F;
            addSym(cp, g);
        };

        def(0x00B1, // ±
            "  #  ",
            "  #  ",
            "#####",
            "  #  ",
            "  #  ",
            "     ",
            "#####");
        def(0x00D7, // ×
            "     ",
            "#   #",
            " # # ",
            "  #  ",
            " # # ",
            "#   #",
            "     ");
        def(0x2022, // •
            "     ",
            "     ",
            " ### ",
            " ### ",
            " ### ",
            "     ",
            "     ");
        def(0x2190, // ←
            "     ",
            "  #  ",
            " #   ",
            "#####",
            " #   ",
            "  #  ",
            "     ");
        def(0x2191, // ↑
            "  #  ",
            " ### ",
            "# # #",
            "  #  ",
            "  #  ",
            "  #  ",
            "     ");
        def(0x2192, // →
            "     ",
            "  #  ",
            "   # ",
            "#####",
            "   # ",
            "  #  ",
            "     ");
        def(0x2193, // ↓
            "  #  ",
            "  #  ",
            "  #  ",
            "# # #",
            " ### ",
            "  #  ",
            "     ");
        def(0x23F8, // ⏸ pause
            "     ",
            " # # ",
            " # # ",
            " # # ",
            " # # ",
            " # # ",
            "     ");
        // Block elements U+2581..U+2588. 7 pixel rows can't express 8
        // distinct levels: the ramp is 1,2,3,4,4,5,6,7 rows (▄/▅ share
        // a height; █ stays distinct from ▇).
        block(0x2581, 1); // ▁
        block(0x2582, 2); // ▂
        block(0x2583, 3); // ▃
        block(0x2584, 4); // ▄
        block(0x2585, 4); // ▅ (same as ▄ at this height)
        block(0x2586, 5); // ▆
        block(0x2587, 6); // ▇
        block(0x2588, 7); // █
        halfBlock(0x258C, true);  // ▌
        halfBlock(0x2590, false); // ▐
        def(0x25A0, // ■
            "     ",
            "#####",
            "#####",
            "#####",
            "#####",
            "#####",
            "     ");
        def(0x25A1, // □
            "     ",
            "#####",
            "#   #",
            "#   #",
            "#   #",
            "#####",
            "     ");
        def(0x25AA, // ▪
            "     ",
            "     ",
            " ### ",
            " ### ",
            " ### ",
            "     ",
            "     ");
        def(0x25AB, // ▫
            "     ",
            "     ",
            " ### ",
            " # # ",
            " ### ",
            "     ",
            "     ");
        def(0x25B2, // ▲
            "  #  ",
            "  #  ",
            " # # ",
            " # # ",
            "#   #",
            "#####",
            "     ");
        def(0x25B6, // ▶ play
            "#    ",
            "##   ",
            "###  ",
            "#### ",
            "###  ",
            "##   ",
            "#    ");
        def(0x25BC, // ▼
            "     ",
            "#####",
            "#   #",
            " # # ",
            " # # ",
            "  #  ",
            "  #  ");
        def(0x25C0, // ◀
            "    #",
            "   ##",
            "  ###",
            " ####",
            "  ###",
            "   ##",
            "    #");
        def(0x25CF, // ● record dot
            "     ",
            " ### ",
            "#####",
            "#####",
            "#####",
            " ### ",
            "     ");
        def(0x266A, // ♪
            "   ##",
            "   ##",
            "   # ",
            "   # ",
            "   # ",
            " ## #",
            " ##  ");
        def(0x266B, // ♫
            "  ###",
            "  # #",
            "  # #",
            "  # #",
            "  # #",
            "## ##",
            "## ##");
        def(0x2713, // ✓
            "     ",
            "    #",
            "    #",
            "   # ",
            "#  # ",
            " # # ",
            "  #  ");
    }

    static constexpr int kMaxSyms = 40;
    std::array<Glyph, 128>   m_ascii;
    std::array<Glyph, 0x90>  m_greek;  // U+0370..U+03FF
    std::array<Glyph, 0x100> m_cyr;    // U+0400..U+04FF
    std::array<Sym, kMaxSyms> m_syms{};
    int m_symCount = 0;
};

} // namespace gb
