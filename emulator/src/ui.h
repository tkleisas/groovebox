#pragma once

// Tiny software renderer for the 240x160 RGB565 display, in the style
// of docs/ui_mockup.png (Elektron-like, monochrome): inverted header
// bar (page name + BPM), parameter rows (dim name, bright value, bar),
// inverse-video selected row, LED row, footer with encoder / soft-key
// assignments.
//
// Pages (UiState::page): 0 = SYNTH (param rows), 1 = SEQ
// (SequencerWidget), 2 = MIXER (MixerWidget). Widget geometry lives in
// widgets.h; all widget bboxes follow the 8 px grid rule.

#include <cstdint>
#include "font5x7.h"
#include "hal.h" // kDisplayW / kDisplayH
#include "widgets.h"

namespace gb {

struct UiParam {
    const char* name;
    int value; // 0..127
};

struct UiState {
    const char* pageName = "SYNTH"; // page 0 title (SYNTH / DRUMS)
    int page = 0;           // 0 = SYNTH, 1 = SEQ, 2 = MIXER
    int track = 0;          // 0-based engine track shown in header
    double bpm = 120.0;
    bool playing = false;
    bool recording = false;
    int mode = 0;           // 0 = PLAY, 1 = SEQ
    int selected = 0;       // selected parameter row (inverse video)
    UiParam params[6] = {
        {"CUTOFF",  87},
        {"RESO",    41},
        {"ATTACK",   3},
        {"DECAY",   55},
        {"SUSTAIN", 70},
        {"RELEASE", 24},
    };
    const char* encLabels[4]  = {"CUT", "RES", "ATK", "REL"};
    const char* softLabels[4] = {"TRK1", "TRK2", "PAGE", "SEL+"};
    // Optional one-liner drawn between the LED row and the footer
    // (UTF-8 — used by the smoke test to demo Greek/Cyrillic).
    const char* overlayLine = nullptr;
    bool leds[16] = {};

    // Transport clock (drawn in the SEQ page header, live data).
    int clockBar = 1, clockBeat = 1, clockStep = 1;
    float clockPhase = 0.0f;

    // SEQ page: T1 default note / gate readout (right info block).
    int seqNote = 60;
    int seqGatePct = 70;
    // SEQ page: pattern length + 16-step window (paging) state.
    int seqLength = 16;
    int seqWindow = 0;      // first step of the visible window
    bool lenEdit = false;   // length-edit state active (S1 = LEN)

    // Toast overlay (transient encoder feedback; app-owned text).
    bool toastActive = false;
    char toastLabel[16] = {};
    char toastValue[16] = {};

    // Page 1 / 2 widget state (app-owned, updated each frame).
    SequencerWidget seq;
    StepLaneWidget lane;   // per-step velocity of the selected track
    MixerWidget mixer;
};

class Ui {
public:
    // fb must point to 240x160 uint16 RGB565 pixels.
    void render(const UiState& s, uint16_t* fb);

    // Boot splash ("ПОЕХАЛИ!" + brand line), themed. Drawn instead of
    // any page for the first moments after startup.
    void renderSplash(uint16_t* fb);

private:
    Font5x7 m_font;

    void drawHeader(const UiState& s, uint16_t* fb);
    void drawLeds(const UiState& s, uint16_t* fb);
    void drawFooter(const UiState& s, uint16_t* fb);
    void pageSynth(const UiState& s, uint16_t* fb);
    void pageSeq(const UiState& s, uint16_t* fb);
    void pageMixer(const UiState& s, uint16_t* fb);

    static void fill(uint16_t* fb, int x, int y, int w, int h, uint16_t c);
    static void frame(uint16_t* fb, int x, int y, int w, int h, uint16_t c);
    // Returns advance in pixels. When bg is opaque (inv=true), every
    // glyph cell is painted fg-on-bg (used for inverse-video rows).
    // s is UTF-8; width is measured in codepoints, not bytes.
    int text(uint16_t* fb, int x, int y, int scale, const char* s,
             uint16_t fg, bool inv = false, uint16_t bg = 0);
    // Pixel width of a UTF-8 string at the given scale (codepoints).
    static int textWidth(const char* s, int scale) {
        return utf8Length(s) * (Font5x7::kW + 1) * scale;
    }
};

} // namespace gb
