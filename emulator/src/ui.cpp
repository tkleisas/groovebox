#include "ui.h"

#include <cstdio>

namespace gb {

// Palette comes from the live theme (widgets.h): kColBlack/White/Gray/
// Dim read g_theme every frame.

void Ui::fill(uint16_t* fb, int x, int y, int w, int h, uint16_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kDisplayW) w = kDisplayW - x;
    if (y + h > kDisplayH) h = kDisplayH - y;
    if (w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            fb[row * kDisplayW + col] = c;
}

void Ui::frame(uint16_t* fb, int x, int y, int w, int h, uint16_t c) {
    fill(fb, x, y, w, 1, c);
    fill(fb, x, y + h - 1, w, 1, c);
    fill(fb, x, y, 1, h, c);
    fill(fb, x + w - 1, y, 1, h, c);
}

int Ui::text(uint16_t* fb, int x, int y, int scale, const char* s,
             uint16_t fg, bool inv, uint16_t bg) {
    const int advance = (Font5x7::kW + 1) * scale;
    int cx = x;
    const char* p = s;
    while (*p) {
        const uint32_t cp = utf8Next(p);
        if (cp == ' ' && !inv) { cx += advance; continue; }
        for (int gx = 0; gx < Font5x7::kW + (inv ? 1 : 0); ++gx) {
            for (int gy = 0; gy < Font5x7::kH; ++gy) {
                bool on = gx < Font5x7::kW && m_font.pixel(cp, gx, gy);
                uint16_t c = on ? fg : (inv ? bg : fg);
                if (on || inv)
                    fill(fb, cx + gx * scale, y + gy * scale, scale, scale, c);
            }
        }
        cx += advance;
    }
    return cx - x;
}

// ── Shared chrome ───────────────────────────────────────────────────

// One parameter row (dim name, bright right-aligned value, bar).
// value < 0 → unbound: dim "—", no bar. y0 = row top (16 px stride).
void Ui::drawParamRow(const UiParam& p, int y0, bool sel, uint16_t* fb) {
    if (sel) fill(fb, 0, y0, kDisplayW, 16, kColWhite);
    const uint16_t fg     = sel ? kColBlack : kColWhite;
    const uint16_t dimCol = sel ? kColBlack : kColGray;
    text(fb, 4, y0 + 1, 2, p.name, dimCol);
    if (p.value < 0) {
        text(fb, 112, y0 + 1, 2, "-", sel ? kColBlack : kColDim);
        return;
    }
    char val[8];
    std::snprintf(val, sizeof(val), "%d", p.value);
    // right-aligned value ending at x=124, bar starts at 132 (spacing
    // fix: SUSTAIN/RELEASE rows' bars no longer touch 3-digit values)
    text(fb, 124 - textWidth(val, 2), y0 + 1, 2, val, fg, sel, kColWhite);
    const int bx = 132, bw = 102, by = y0 + 4, bh = 8;
    frame(fb, bx, by, bw, bh, fg);
    const int fw = (bw - 2) * p.value / 127;
    if (fw > 0) fill(fb, bx + 1, by + 1, fw, bh - 2, kColAcc2); // bar fill
}

void Ui::drawHeader(const UiState& s, uint16_t* fb) {
    fill(fb, 0, 0, kDisplayW, 16, kColWhite);
    const char* title = s.page == 1 ? "SEQ" : s.page == 2 ? "MIXER"
                      : s.page == 3 ? "FX" : s.page == 4 ? "SAMPLE"
                      : s.page == 5 ? "LOAD" : s.page == 6 ? "SET"
                      : s.pageName;
    text(fb, 4, 1, 2, title, kColBlack);
    if (s.page == 1) {
        // SEQ page: keys mode marker + live transport clock (inverse).
        text(fb, 44, 1, 2, s.nsr2
                 ? (s.mode == 0 ? "P" : s.mode == 1 ? "S" : "T")
                 : (s.scaleLock == 2 ? "C" : "M"),
             kColBlack);
        Canvas565 c{fb, kDisplayW, kDisplayH};
        TransportClockWidget clk{88, 0, s.clockBar, s.clockBeat,
                                 s.clockStep, s.clockPhase, true};
        clk.draw(c, m_font);
    } else {
        // transport / mode marker
        const char* marker = s.recording ? "R" : (s.playing ? ">" : "");
        if (*marker) text(fb, 96, 1, 2, marker, kColBlack);
        text(fb, 112, 1, 2,
             s.nsr2 ? (s.mode == 0 ? "PLAY" : s.mode == 1 ? "SEQ" : "TEXT")
                    : (s.scaleLock == 2 ? "CHRM" : "MAJ"),
             kColBlack);
    }
    // right-aligned track + BPM
    char right[16];
    std::snprintf(right, sizeof(right), "T%d %d", s.track + 1, int(s.bpm + 0.5));
    text(fb, kDisplayW - textWidth(right, 2) - 4, 1, 2, right, kColBlack);
}

void Ui::drawLeds(const UiState& s, uint16_t* fb) {
    for (int i = 0; i < 16; ++i) {
        const int x = 3 + i * 15;
        if (s.leds[i]) fill(fb, x, 122, 9, 8, kColWhite);
        else           frame(fb, x, 122, 9, 8, kColDim);
    }
}

void Ui::drawFooter(const UiState& s, uint16_t* fb) {
    fill(fb, 0, 140, kDisplayW, 1, kColDim);
    for (int i = 0; i < 4; ++i) {
        const int col = i * 60;
        int w = textWidth(s.encLabels[i], 1);
        text(fb, col + (60 - w) / 2, 144, 1, s.encLabels[i], kColGray);
        w = textWidth(s.softLabels[i], 1);
        text(fb, col + (60 - w) / 2, 152, 1, s.softLabels[i], kColWhite);
    }
}

// ── Pages ───────────────────────────────────────────────────────────

void Ui::pageSynth(const UiState& s, uint16_t* fb) {
    for (int i = 0; i < 6; ++i)
        drawParamRow(s.params[i], 18 + i * 16, i == s.selected, fb);
    // macro strip: current bindings of encoders 5-8 ("5:A" style)
    for (int i = 0; i < 4; ++i) {
        if (!s.macroLabels[i]) continue;
        text(fb, 4 + i * 60, 114, 1, s.macroLabels[i], kColDim);
    }
    // Optional UTF-8 overlay line (demo / status)
    if (s.overlayLine) {
        text(fb, (kDisplayW - textWidth(s.overlayLine, 1)) / 2, 132, 1,
             s.overlayLine, kColWhite);
    }
}

void Ui::pageFx(const UiState& s, uint16_t* fb) {
    // effect name (or EMPTY) + bypass tag
    if (s.fxName) {
        text(fb, 16, 24, 2, s.fxName, kColWhite);
        if (s.fxBypassed) text(fb, 16, 44, 1, "BYPASSED", kColDim);
    } else {
        text(fb, 16, 24, 2, "EMPTY", kColDim);
        text(fb, 16, 44, 1, "S1 = CHOOSE EFFECT", kColDim);
    }
    for (int i = 0; i < 4; ++i) {
        if (s.fxParams[i].name)
            drawParamRow(s.fxParams[i], 56 + i * 16, false, fb);
    }
    // effect chooser overlay
    if (s.chooserOpen && s.chooserItems) {
        Canvas565 c{fb, kDisplayW, kDisplayH};
        ListWidget list{56, 24, 128, 96, s.chooserItems, s.chooserCount,
                        s.chooserSel, s.chooserScroll};
        list.draw(c, m_font);
    }
}

void Ui::pageSample(const UiState& s, uint16_t* fb) {
    Canvas565 c{fb, kDisplayW, kDisplayH};
    if (s.sampleMsg) {
        text(fb, 8, 24, 2, s.sampleMsg, kColDim);
        return;
    }
    if (s.sampleState == 1) {
        // REC marker + elapsed + level bar
        fill(fb, 8, 20, 8, 8, kColWhite); // record dot
        char tb[16];
        std::snprintf(tb, sizeof(tb), "REC %.1f", double(s.sampleElapsed));
        text(fb, 20, 21, 2, tb, kColWhite);
        frame(fb, 112, 24, 120, 8, kColDim);
        fill(fb, 113, 25, int(118 * s.sampleLevel), 6, kColWhite);
        if (s.sampleWave && s.sampleWaveLen > 0) {
            WaveformWidget w{8, 48, 224, 48, s.sampleWave, s.sampleWaveLen,
                             -1};
            w.draw(c);
        }
        text(fb, 8, 104, 1, "S2 STOP", kColDim);
    } else if (s.sampleState == 2) {
        SliceWidget sl{8, 40, 224, 48, s.sampleWave, s.sampleWaveLen,
                       s.sampleTrim0, s.sampleTrim1, 1, -1};
        sl.draw(c);
        char tb[24];
        std::snprintf(tb, sizeof(tb), "GAIN %.2f", double(s.sampleGain));
        text(fb, 8, 24, 2, tb, kColWhite);
        text(fb, 8, 96, 1, "E1 TRIM-  E2 TRIM+  E3 GAIN", kColDim);
        text(fb, 8, 104, 1, "S3 NORM   S4 ASSIGN", kColDim);
    } else {
        text(fb, 8, 24, 2, "READY", kColGray);
        text(fb, 8, 48, 1, "S1 = RECORD (MIC IN)", kColDim);
    }
}

void Ui::pageLoad(const UiState& s, uint16_t* fb) {
    Canvas565 c{fb, kDisplayW, kDisplayH};
    FileBrowserWidget b = s.browser;
    b.x = 8; b.y = 24; b.w = 224; b.h = 112;
    b.draw(c, m_font);
}

void Ui::pageSettings(const UiState& s, uint16_t* fb) {
    static const char* kNames[4] = {"THEME", "VEL SRC", "LED DIM",
                                    "SCALE"};
    for (int i = 0; i < 4; ++i) {
        const int y0 = 32 + i * 16;
        const bool sel = (i == s.settingsSel);
        if (sel) fill(fb, 0, y0, kDisplayW, 16, kColWhite);
        text(fb, 8, y0 + 1, 2, kNames[i], sel ? kColBlack : kColGray);
        if (s.settingsVal[i]) {
            const int tw = textWidth(s.settingsVal[i], 2);
            text(fb, kDisplayW - tw - 8, y0 + 1, 2, s.settingsVal[i],
                 sel ? kColBlack : kColWhite, sel, kColWhite);
        }
    }
    text(fb, 8, 104, 1, "E1 SEL  E2 ADJ  S2 SAVE", kColDim);
}

void Ui::pageSeq(const UiState& s, uint16_t* fb) {
    Canvas565 c{fb, kDisplayW, kDisplayH};
    // NSR-2 text-entry mode: the grid becomes a quasi-keyboard feeding
    // an EditBox (pattern-name target for now).
    if (s.nsr2 && s.mode == 2) {
        text(fb, 8, 24, 1, "PATTERN NAME", kColDim);
        EditBoxWidget eb{8, 32, 224, 16,
                         s.textBuf ? s.textBuf : "", s.textCursor, 0,
                         true, nullptr, "TYPE ON GRID..."};
        eb.draw(c, m_font);
        text(fb, 8, 56, 1, "ROWS: QWERTYUI/ASDFGHJK/ZXCVBNM", kColDim);
        text(fb, 8, 64, 1, "SPACE=bar  BKSP=key29  ENTER=key31", kColDim);
        text(fb, 8, 72, 1, "MODE -> back to STEP", kColDim);
        return;
    }
    // row labels (inverse = selected track, dim = muted)
    static const char* kRowNames[4] = {"T1", "T2", "T3", "T4"};
    for (int r = 0; r < s.seq.rows && r < 4; ++r) {
        const int ly = s.seq.y + r * s.seq.rowH + (s.seq.rowH - 7) / 2;
        const bool sel = (r == s.track);
        const bool muted = (s.seq.rowMuted >> r) & 1u;
        if (sel) fill(fb, 2, ly - 1, 14, 9, kColWhite);
        text(fb, 4, ly, 1, kRowNames[r],
             sel ? kColBlack : (muted ? kColDim : kColGray));
    }
    SequencerWidget seq = s.seq; // local copy (const widget draw)
    seq.draw(c);
    // step ruler: absolute beat numbers (follow the window)
    for (int b = 0; b < 4; ++b) {
        char n[8];
        std::snprintf(n, sizeof(n), "%d", s.seqWindow / 4 + b + 1);
        text(fb, s.seq.x + b * 32 + 2, s.seq.y - 10, 1, n, kColDim);
    }
    // velocity lane for the selected track
    StepLaneWidget lane = s.lane;
    lane.draw(c);

    // right info block: shift-layer hint + page indicator + T1 readout
    text(fb, 160, 32, 1, "SHIFT+", kColDim);
    text(fb, 160, 40, 1, "1-4 TRK", kColDim);
    text(fb, 160, 48, 1, "5-8 MUTE", kColDim);
    if (s.seqLength > 16) {
        char pg[8];
        std::snprintf(pg, sizeof(pg), "PG %d/%d", s.seqWindow / 16 + 1,
                      (s.seqLength + 15) / 16);
        text(fb, 160, 56, 1, pg, kColGray);
    }
    if (s.track == 0) {
        static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B"};
        char buf[16];
        std::snprintf(buf, sizeof(buf), "NTE %s%d",
                      kNames[s.seqNote % 12], s.seqNote / 12 - 1);
        text(fb, 160, 72, 1, buf, kColGray);
        std::snprintf(buf, sizeof(buf), "GTE %d%%", s.seqGatePct);
        text(fb, 160, 80, 1, buf, kColGray);
    }
    if (s.lenEdit) { // length-edit state: numeric field
        NumericFieldWidget nf{160, 96, 48, 3, s.seqLength, "STP", -1};
        nf.draw(c, m_font);
        text(fb, 160, 88, 1, "LEN:", kColGray);
    }
}

void Ui::pageMixer(const UiState& s, uint16_t* fb) {
    Canvas565 c{fb, kDisplayW, kDisplayH};
    MixerWidget mix = s.mixer;
    mix.x = 80; mix.y = 32; mix.h = 80;
    mix.selected = s.track;
    mix.draw(c, m_font);
    // strip labels: track numbers + master (inverse = selected track)
    static const char* kNames[5] = {"1", "2", "3", "4", "M"};
    for (int i = 0; i < 5; ++i) {
        const int lx = mix.x + i * 16 + 6;
        const bool sel = (i == s.track);
        if (sel) fill(fb, lx - 1, 23, 8, 9, kColWhite);
        text(fb, lx, 24, 1, kNames[i], sel ? kColBlack : kColGray);
    }
}

void Ui::renderSplash(uint16_t* fb) {
    fill(fb, 0, 0, kDisplayW, kDisplayH, kColBlack);
    const char* title = "ПОЕХАЛИ!";
    text(fb, (kDisplayW - textWidth(title, 4)) / 2, 48, 4, title,
         kColWhite);
    const char* brand = "ВОКОИТЕР - ΟΡГАΝΟ 1";
    text(fb, (kDisplayW - textWidth(brand, 2)) / 2, 100, 2, brand,
         kColGray);
    const char* sub = "YAWN ENGINE // 48000 Hz";
    text(fb, (kDisplayW - textWidth(sub, 1)) / 2, 132, 1, sub, kColDim);
    text(fb, (kDisplayW - textWidth(GB_VERSION_STRING, 1)) / 2, 144, 1,
         GB_VERSION_STRING, kColDim);
}

void Ui::render(const UiState& s, uint16_t* fb) {
    fill(fb, 0, 0, kDisplayW, kDisplayH, kColBlack);
    drawHeader(s, fb);
    switch (s.page) {
    case 1:  pageSeq(s, fb);   break;
    case 2:  pageMixer(s, fb); break;
    case 3:  pageFx(s, fb);    break;
    case 4:  pageSample(s, fb); break;
    case 5:  pageLoad(s, fb);  break;
    case 6:  pageSettings(s, fb); break;
    default: pageSynth(s, fb); break;
    }
    drawLeds(s, fb);
    drawFooter(s, fb);
    // toast overlay reads over everything (transient encoder feedback)
    if (s.toastActive) {
        Canvas565 c{fb, kDisplayW, kDisplayH};
        ToastWidget toast{72, 48, 96, 32, s.toastLabel, s.toastValue};
        toast.draw(c, m_font);
    }
    // confirm dialog reads over everything, including toasts
    if (s.dialogActive) {
        Canvas565 c{fb, kDisplayW, kDisplayH};
        DialogWidget dlg{56, 48, 128, 64, s.dlgTitle, s.dlgLine, nullptr,
                         {"OK", "CANCEL", nullptr, nullptr}};
        dlg.draw(c, m_font);
    }
}

} // namespace gb
