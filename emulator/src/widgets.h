#pragma once

// ── Grid constraint ─────────────────────────────────────────────────
// Every widget's bounding box (x, y, w, h) is specified in GRID
// units: all four are multiples of 8 px. Widget PLACEMENT on a page
// should prefer 16 px multiples where practical. The screen is
// 240x160 px = 30x20 cells of 8 px. Widgets draw into the framebuffer
// at their anchor and own their bounding box (including erasing their
// own background where they have one).
//
// All widgets are monochrome (white/gray on black, inverse video for
// emphasis), data-driven (plain values, no engine coupling), and
// allocation-free at draw time.

#include <cstdint>
#include "font5x7.h"

namespace gb {

// ── Theme ───────────────────────────────────────────────────────────
// The whole device-screen palette is four base slots (bg = background,
// fg = primary text/set cells, mid = secondary, dim = faintest) plus
// four ACCENT ROLES for the things that pop:
//   acc[0] values/toasts      acc[1] bars/levels/meters
//   acc[2] selection accents  acc[3] playhead / record markers
// MONO/RED/GREEN/AMBER map every role to fg (byte-identical look to the
// pre-role palette). ARCADE uses the roles for real color separation.
struct Theme { uint16_t bg, fg, mid, dim, acc[4]; };

enum ThemeId { kThemeMono = 0, kThemeRed, kThemeGreen, kThemeAmber,
               kThemeArcade, kNumThemes };

extern Theme g_theme;             // live palette (set via setTheme)
// Live aliases — existing widget/ui code reads these per draw call, so
// a theme switch takes effect on the next frame with no call-site churn.
extern uint16_t& kColBlack; // = g_theme.bg
extern uint16_t& kColWhite; // = g_theme.fg
extern uint16_t& kColGray;  // = g_theme.mid
extern uint16_t& kColDim;   // = g_theme.dim
extern uint16_t& kColAcc1;  // = g_theme.acc[0] (values/toasts)
extern uint16_t& kColAcc2;  // = g_theme.acc[1] (bars/levels)
extern uint16_t& kColAcc3;  // = g_theme.acc[2] (selection)
extern uint16_t& kColAcc4;  // = g_theme.acc[3] (playhead/rec)

void setTheme(ThemeId id);
ThemeId currentTheme();
const char* themeName(ThemeId id);


// ── Draw context ────────────────────────────────────────────────────
// Thin wrapper over a 240x160 (or larger) RGB565 framebuffer with
// clipping fills and UTF-8 text. All drawing is clipped to the canvas.
struct Canvas565 {
    uint16_t* fb = nullptr;
    int w = 0, h = 0;

    void pixel(int x, int y, uint16_t c);
    void fill(int x, int y, int rw, int rh, uint16_t c);
    void frame(int x, int y, int rw, int rh, uint16_t c);
    void hline(int x, int y, int len, uint16_t c) { fill(x, y, len, 1, c); }
    void vline(int x, int y, int len, uint16_t c) { fill(x, y, 1, len, c); }
    void line(int x0, int y0, int x1, int y1, uint16_t c); // Bresenham
    // Returns advance in px. ASCII space skips drawing (transparent).
    int text(Font5x7& f, int x, int y, int scale, const char* s, uint16_t fg);
    static int textWidth(const char* s, int scale) {
        return utf8Length(s) * (Font5x7::kW + 1) * scale;
    }
};

// ── PhasePieWidget ──────────────────────────────────────────────────
// Phase pie for PERIODIC events (sequencer cycle, LFO phase, beat/bar
// clock): circle outline with a filled wedge that grows clockwise from
// 12 o'clock as phase goes 0..1, fully filled at 1.0, resetting on the
// next cycle. A tick at 12 o'clock marks zero phase; a center dot
// anchors the sweep. No text by default.
struct PhasePieWidget {
    int x = 0, y = 0, size = 32;
    float phase = 0.0f;   // 0..1 (wraps each cycle)
    bool showText = false;
    bool inverse = false; // black-on-white (for the inverted header)
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── WaveformWidget ──────────────────────────────────────────────────
// Sample buffer as a waveform: one min/max vertical span per x column.
// playhead = sample index of the marker line; -1 = no marker.
struct WaveformWidget {
    int x = 0, y = 0, w = 112, h = 32;
    const float* samples = nullptr; // -1..1
    int count = 0;
    int playhead = -1;
    void draw(Canvas565& c) const;
};

// ── AdsrWidget ──────────────────────────────────────────────────────
// ADSR envelope polyline with node handles. attack/decay/release are
// times 0..1 (normalized for display), sustain is a level 0..1.
// selected = segment index 0..3 (A D S R), -1 = none; the selected
// segment is drawn bright, others dim.
struct AdsrWidget {
    int x = 0, y = 0, w = 64, h = 32;
    float attack = 0.1f, decay = 0.3f, sustain = 0.7f, release = 0.3f;
    int selected = -1;
    void draw(Canvas565& c) const;
};

// ── FmAlgoWidget ────────────────────────────────────────────────────
// FM algorithm diagram, bbox 128x64 (half screen width): 4 operator
// boxes (OP1..OP4, 20x12) on a 4x2 cell grid (32x24 cells) with
// modulator→carrier connection lines. Same-row links are straight,
// same-column links vertical, everything else is elbow-routed
// (vertical out of the modulator, horizontal into the carrier) with an
// arrowhead showing the modulation direction. Carriers get an output
// arrow stub toward the right edge. Algorithms are DATA (positions +
// edges + carrier flags), not code.
struct FmAlgo {
    const char* name;
    uint8_t opCol[4];      // grid column 0..3 per operator
    uint8_t opRow[4];      // grid row 0..1 per operator
    uint8_t edges[6][2];   // {modulator op 0..3, carrier op 0..3}
    int numEdges;
    bool carrier[4];       // carriers get an output stub to the right
};
extern const FmAlgo kFmAlgoSerial;    // OP4→OP3→OP2→OP1→out
extern const FmAlgo kFmAlgoParallel;  // all four are carriers
extern const FmAlgo kFmAlgoPair;      // OP2→OP1→out, OP4→OP3→out
extern const FmAlgo kFmAlgoStack;     // OP4→OP3→OP2→OP1 folded (elbow)

struct FmAlgoWidget {
    int x = 0, y = 0;    // bbox 128x64 (4x32 wide, 2x24 grid + name row)
    const FmAlgo* algo = nullptr;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── SpectrumWidget ──────────────────────────────────────────────────
// Bar spectrum from a magnitude array (0..1 per bin), with optional
// peak markers (same length, -1 disables a bin's marker).
struct SpectrumWidget {
    int x = 0, y = 0, w = 64, h = 32;
    const float* mags = nullptr;
    int bins = 0;
    const float* peaks = nullptr; // may be nullptr
    void draw(Canvas565& c) const;
};

// ── SequencerWidget ─────────────────────────────────────────────────
// The step grid: 16 columns x 1..4 rows. Each cell is 8x8 px, so a
// row is exactly 128 px wide; row stride 8 or 16 (rowH). States:
// empty / set / set+accent. playhead = column 0..15 drawn inverted;
// -1 = transport stopped (no playhead).
enum StepState : uint8_t { kStepEmpty = 0, kStepSet = 1, kStepAccent = 2,
                           // pitch-override variants (T1 per-step note):
                           // same body as set/accent + a marker dash in
                           // the cell's top pixel row
                           kStepSetAlt = 3, kStepAccentAlt = 4 };

struct SequencerWidget {
    int x = 0, y = 0;
    int rows = 2;                 // 1..4
    int rowH = 16;                // 8 or 16
    uint8_t cells[4][16] = {};    // StepState per cell
    int playhead = -1;
    uint8_t rowMuted = 0;         // bit per row: dimmed (track muted)
    void draw(Canvas565& c) const;
};

// ── MixerWidget ─────────────────────────────────────────────────────
// 4 channel strips + master: vertical fader bars with a peak marker
// line and per-strip M(ute)/S(olo) letters. Strip width 16 px,
// 5 strips = 80 px wide. level/peak are 0..1.
struct MixerWidget {
    int x = 0, y = 0, h = 64;
    float level[5] = {};   // 0..3 tracks, 4 = master
    float peak[5] = {};
    bool mute[5] = {};
    bool solo[5] = {};
    int selected = -1;     // strip gets a bright underline (-1 = none)
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── WavetableWidget ─────────────────────────────────────────────────
// One wavetable frame drawn as a polyline inside a frame box, with a
// small triangle marker under the box showing the frame position 0..1
// (morph position through the table).
struct WavetableWidget {
    int x = 0, y = 0, w = 64, h = 32;
    const float* table = nullptr; // single cycle, -1..1
    int size = 0;
    float framePos = 0.0f;        // 0..1
    void draw(Canvas565& c) const;
};

// ── KnobWidget ──────────────────────────────────────────────────────
// Rotary indicator: circle outline + pointer line from center at the
// value angle (300° sweep, 60° gap at the bottom). size = 16/24/32.
// With label, the bbox grows 8 px taller (label under the knob).
struct KnobWidget {
    int x = 0, y = 0, size = 32;
    float value = 0.0f;           // 0..1
    const char* label = nullptr;  // may be nullptr
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── SwitchWidget ────────────────────────────────────────────────────
// 16x8 on/off toggle: trough frame + nub left (off, dim) / right (on,
// filled bright).
struct SwitchWidget {
    int x = 0, y = 0;
    bool on = false;
    void draw(Canvas565& c) const;
};

// ── RadioWidget ─────────────────────────────────────────────────────
// 8x8 radio: empty circle (off) / filled dot (on) + label to the
// right (label extends past the 8x8 bbox). drawGroup renders a
// vertical list of options (8 px stride), one selected.
struct RadioWidget {
    int x = 0, y = 0;
    bool selected = false;
    const char* label = nullptr;
    void draw(Canvas565& c, Font5x7& font) const;
    static void drawGroup(Canvas565& c, Font5x7& font, int x, int y,
                          const char* const* items, int count, int selected);
};

// ── ListWidget ──────────────────────────────────────────────────────
// Generic vertical menu list: 8 px rows, inverse-video selected row,
// scrollbar thumb on the right edge when count exceeds the view.
// scroll = index of the first visible item.
struct ListWidget {
    int x = 0, y = 0, w = 112, h = 48;
    const char* const* items = nullptr;
    int count = 0;
    int selected = 0;
    int scroll = 0;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── FileBrowserWidget ───────────────────────────────────────────────
// Path bar at top (dim, 8 px) + scrollable entry list below (folders
// suffixed '/'), inverse selection, scrollbar when needed.
struct FileBrowserWidget {
    int x = 0, y = 0, w = 112, h = 64;
    const char* path = nullptr;
    const char* const* entries = nullptr;
    const bool* isDir = nullptr;  // may be nullptr (all files)
    int count = 0;
    int selected = 0;
    int scroll = 0;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── ToastWidget ─────────────────────────────────────────────────────
// Centered overlay box for transient encoder feedback: bright double
// border over black fill, big value text (2x) + small dim label.
// AUTO-SIZED to content: width = max(value line at 2x, label line at
// 1x) measured UTF-8-aware, + symmetric padding, rounded UP to the 8px
// grid, clamped to the screen with an 8px margin. The w/h fields are
// MINIMUMS — the widget computes its final size from content and
// recenters the computed box on the minimum box's center, so existing
// {72, 48, 96, 32} call sites keep their screen-center placement.
struct ToastWidget {
    int x = 0, y = 0, w = 96, h = 32;  // minimum box (its center is kept)
    const char* label = nullptr;
    const char* valueText = nullptr;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── TransportClockWidget ────────────────────────────────────────────
// "BAR.BEAT.STEP" text (003.2.13) + a 16 px PhasePieWidget showing the
// beat phase. bbox 72x16. inverse=true draws black-on-white (for the
// inverted header bar).
struct TransportClockWidget {
    int x = 0, y = 0;
    int bar = 1, beat = 1, step = 1;
    float beatPhase = 0.0f;       // 0..1 within the current beat
    bool inverse = false;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── NumericFieldWidget ──────────────────────────────────────────────
// Fixed-width zero-padded digit field + optional unit (for BPM/pitch
// entry). Digits in 8 px cells; selDigit (0-based from left, -1 =
// none) is inverse-video. w must cover digits*8 + unit text.
struct NumericFieldWidget {
    int x = 0, y = 0, w = 48;
    int digits = 3;
    int value = 0;
    const char* unit = nullptr;   // e.g. "BPM"
    int selDigit = -1;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── DialogWidget ────────────────────────────────────────────────────
// Centered modal frame: inverted title bar, 1-2 body lines, bottom row
// of soft-key hints aligned to the 4 physical soft keys (60 px columns
// like the page footer).
struct DialogWidget {
    int x = 0, y = 0, w = 128, h = 64;
    const char* title = nullptr;
    const char* line1 = nullptr;
    const char* line2 = nullptr;
    const char* soft[4] = {};     // S1..S4 labels, nullptr = empty
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── BatteryWidget ───────────────────────────────────────────────────
// 16x8 battery: outline + nub + fill level (0..1). With showText, a %
// readout is drawn right of the icon (caller leaves space; the 16x8
// bbox covers just the icon).
struct BatteryWidget {
    int x = 0, y = 0;
    float level = 1.0f;           // 0..1
    bool showText = false;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── StatusIconsWidget ───────────────────────────────────────────────
// Row of four 8x8 indicators (bbox 32x8): MIDI-in activity, Link/clock
// sync, SD card present, mic armed. On = bright, off = dim. Icons are
// letters for now: M (MIDI), L (Link), S (SD), R (mic aRmed).
struct StatusIconsWidget {
    int x = 0, y = 0;
    bool midi = false, link = false, sd = false, mic = false;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── SliceWidget ─────────────────────────────────────────────────────
// Waveform with vertical start/end trim markers and N slice division
// ticks between them. selectedSlice (-1 = none) inverts its region.
struct SliceWidget {
    int x = 0, y = 0, w = 240, h = 48;
    const float* samples = nullptr;
    int count = 0;
    float trimStart = 0.0f, trimEnd = 1.0f; // 0..1 of the buffer
    int numSlices = 8;
    int selectedSlice = -1;
    void draw(Canvas565& c) const;
};

// ── StepLaneWidget ──────────────────────────────────────────────────
// 16-column strip (128 px, matching SequencerWidget) with a tiny
// vertical bar per column (velocity/probability 0..127). current =
// column marked with a top tick; -1 = none.
struct StepLaneWidget {
    int x = 0, y = 0;             // bbox 128x16
    uint8_t values[16] = {};
    int current = -1;
    void draw(Canvas565& c) const;
};

// ── TunerWidget ─────────────────────────────────────────────────────
// Horizontal note bar: note name centered on top, meter line below,
// needle left/right of center (cents -50..+50). In-tune (|c| < 3)
// fills the center block.
struct TunerWidget {
    int x = 0, y = 0, w = 64;     // bbox w x 16
    const char* note = "A";
    float cents = 0.0f;
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── PadGridWidget ───────────────────────────────────────────────────
// 4x4 grid of 16x16 cells (bbox 64x64) for drum pads.
enum PadState : uint8_t { kPadEmpty = 0, kPadLoaded = 1,
                          kPadPlaying = 2, kPadMuted = 3 };
struct PadGridWidget {
    int x = 0, y = 0;
    uint8_t pads[16] = {};        // PadState per pad
    void draw(Canvas565& c) const;
};

// ── TabBarWidget ────────────────────────────────────────────────────
// Horizontal tab strip (top of a page): 2-4 tabs, tab width a multiple
// of 16, active tab inverted, dim separator line under the strip.
struct TabBarWidget {
    int x = 0, y = 0;
    const char* const* tabs = nullptr;
    int count = 0;
    int active = 0;
    int tabW = 48;                // multiple of 16
    void draw(Canvas565& c, Font5x7& font) const;
};

// ── VirtualKeyboardWidget ───────────────────────────────────────────
// Full-width on-screen keyboard (bbox 240x64): text field on top
// (frame, text, cursor bar), then 4 key rows — digits, QWERTY rows,
// bottom row with space/backspace/enter. The on-screen cursor is an
// inverse cell (keyCursor = flat index 0..38, moved by <>/encoders).
// shift switches letters to lowercase. The app owns the text buffer;
// keyAt() translates a flat key index (+shift) to a character
// ('\b' = backspace, '\n' = enter, ' ' = space).
struct VirtualKeyboardWidget {
    int x = 0, y = 0;             // bbox 240x64
    const char* text = nullptr;   // current field content
    int textCursor = 0;           // insertion point (char index)
    bool shift = false;
    int keyCursor = 0;            // 0..38
    void draw(Canvas565& c, Font5x7& font) const;

    static constexpr int kKeyCount = 39;
    static char keyAt(int index, bool shift);
};

// ── EditBoxWidget ───────────────────────────────────────────────────
// Standalone single-line text edit field (backs inline renaming of
// pattern/sample names; pairs with VirtualKeyboardWidget for full text
// entry). The text buffer is caller-owned (fixed size) — nothing here
// allocates. cursor and scrollOffset are in CHARACTERS (codepoints),
// not bytes. The optional label is drawn dim ABOVE the box (at y-8),
// outside the widget's bbox.
struct EditBoxWidget {
    int x = 0, y = 0, w = 96;     // w multiple of 8
    int h = 8;                    // 8 or 16
    const char* text = nullptr;   // may be nullptr/empty
    int cursor = 0;               // codepoint index; == length = append
    int scrollOffset = 0;         // first visible character
    bool focused = false;         // bright border when focused
    const char* label = nullptr;  // dim, above the box (outside bbox)
    const char* placeholder = nullptr; // dim, shown when text is empty
    void draw(Canvas565& c, Font5x7& font) const;

    // Visible character cells inside the frame (1 px margins).
    static int visibleCells(int w) { return (w - 2) / 6; }
    // Minimal scroll offset that keeps cursor in view.
    static int ensureCursorVisible(int cursor, int scroll, int w) {
        const int cells = visibleCells(w);
        if (cursor < scroll) return cursor;
        if (cursor > scroll + cells - 1) return cursor - cells + 1;
        return scroll;
    }
};

} // namespace gb
