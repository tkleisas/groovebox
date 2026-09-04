#include "widgetshowcase.h"

#include "widgets.h"
#include "ui.h" // Ui/UiState for the --themes composite

#include <cmath>
#include <cstdio>
#include <vector>

#include "stb_image_write.h" // implementation TU: fontchart.cpp

namespace gb {

namespace {

// Layout: 240 px wide (device width), tall canvas; every widget bbox
// on the 8 px grid, placement preferring 16 px multiples.
constexpr int kW = 240;
constexpr int kH = 824;

void label(Canvas565& c, Font5x7& font, int x, int y, const char* s) {
    c.text(font, x, y, 1, s, kColGray);
}

} // namespace

bool writeWidgetShowcasePng(const char* path) {
    std::vector<uint16_t> fb(size_t(kW) * kH, kColBlack);
    Canvas565 c{fb.data(), kW, kH};
    Font5x7 font;

    // ── Row A: phase pie / knobs / switch / radio ───────────────────
    label(c, font, 16, 8, "PHASE");
    label(c, font, 64, 8, "KNOB");
    label(c, font, 112, 8, "SWITCH");
    label(c, font, 160, 8, "RADIO");

    PhasePieWidget pie{16, 16, 32, 0.65f, false, false};
    pie.draw(c, font);

    KnobWidget knob32{56, 16, 32, 0.7f, "CUT"};
    knob32.draw(c, font);
    KnobWidget knob16{96, 32, 16, 0.3f, nullptr};
    knob16.draw(c, font);

    SwitchWidget sw{120, 32, true};
    sw.draw(c);

    static const char* kRadioItems[3] = {"MONO", "POLY", "LEGATO"};
    RadioWidget::drawGroup(c, font, 160, 16, kRadioItems, 3, 1);

    // ── Row B: wavetable / tuner / battery / status icons ───────────
    label(c, font, 16, 64, "WAVETABLE");
    label(c, font, 96, 64, "TUNER");
    label(c, font, 176, 64, "BATT/STATUS");

    static float wt[64];
    for (int i = 0; i < 64; ++i) {
        const float ph = float(i) / 64.0f;
        // sine-to-saw-ish morph shape
        wt[i] = 0.7f * std::sin(ph * 2.0f * 3.14159265f) +
                0.3f * (2.0f * ph - 1.0f);
    }
    WavetableWidget wtv{16, 72, 64, 32, wt, 64, 0.4f};
    wtv.draw(c);

    TunerWidget tuner{96, 80, 64, "A", 12.0f};
    tuner.draw(c, font);

    BatteryWidget batt{176, 80, 0.62f, true};
    batt.draw(c, font);

    StatusIconsWidget icons{176, 96, true, false, true, false};
    icons.draw(c, font);

    // ── Row C: FM algo / mixer ──────────────────────────────────────
    label(c, font, 16, 112, "FM ALGO");
    label(c, font, 160, 112, "MIXER");

    FmAlgoWidget fm{16, 120, &kFmAlgoStack};
    fm.draw(c, font);

    MixerWidget mix{160, 120, 64,
                    {0.80f, 0.55f, 0.30f, 0.65f, 0.85f},
                    {0.90f, 0.70f, 0.40f, 0.75f, 0.90f},
                    {false, false, true, false, false},
                    {false, true, false, false, false}};
    mix.draw(c, font);

    // ── Waveform / slices ───────────────────────────────────────────
    label(c, font, 0, 192, "WAVEFORM");
    static float wave[480];
    for (int i = 0; i < 480; ++i) {
        const float ph = std::fmod(i * 8.0f / 480.0f, 1.0f); // 8 cycles
        const float env = std::exp(-i / 700.0f);
        wave[i] = (2.0f * ph - 1.0f) * (0.3f + 0.7f * env);
    }
    WaveformWidget wf{0, 200, 240, 48, wave, 480, 310};
    wf.draw(c);

    label(c, font, 0, 256, "SLICES");
    SliceWidget slc{0, 264, 240, 48, wave, 480, 0.08f, 0.92f, 8, 3};
    slc.draw(c);

    // ── Sequencer / step lane / clock / numeric field ───────────────
    label(c, font, 8, 320, "SEQ + STEP LANE");
    label(c, font, 144, 320, "CLOCK");

    SequencerWidget seq;
    seq.x = 8; seq.y = 328; seq.rows = 2; seq.rowH = 16;
    for (int s = 0; s < 16; ++s)
        seq.cells[0][s] = (s % 4 == 0) ? kStepAccent
                        : (s % 2 == 0) ? kStepSet : kStepEmpty;
    seq.cells[1][0] = kStepAccent; seq.cells[1][3] = kStepSet;
    seq.cells[1][6] = kStepSet;    seq.cells[1][8] = kStepAccent;
    seq.cells[1][11] = kStepSet;   seq.cells[1][14] = kStepSet;
    seq.playhead = 5;
    seq.draw(c);

    StepLaneWidget lane;
    lane.x = 8; lane.y = 368;
    for (int s = 0; s < 16; ++s) lane.values[s] = uint8_t(40 + (s * 37) % 88);
    lane.current = 5;
    lane.draw(c);

    TransportClockWidget clock{144, 328, 3, 2, 13, 0.65f, false};
    clock.draw(c, font);

    label(c, font, 144, 352, "NUMERIC");
    NumericFieldWidget num{144, 360, 48, 3, 120, "BPM", 1};
    num.draw(c, font);

    // ── Pads / dialog ───────────────────────────────────────────────
    label(c, font, 8, 392, "PADS");
    label(c, font, 80, 392, "DIALOG");

    PadGridWidget pads;
    pads.x = 8; pads.y = 400;
    for (int i = 0; i < 16; ++i)
        pads.pads[i] = uint8_t(i == 0 ? kPadPlaying
                             : i % 5 == 0 ? kPadMuted
                             : i % 3 == 0 ? kPadEmpty : kPadLoaded);
    pads.draw(c);

    DialogWidget dlg{80, 400, 128, 64, "DELETE CLIP?",
                     "THIS CANNOT BE", "UNDONE.",
                     {"OK", "CANCEL", nullptr, nullptr}};
    dlg.draw(c, font);

    // ── Toast / tabs / list ─────────────────────────────────────────
    label(c, font, 8, 472, "TOAST");
    label(c, font, 128, 472, "TABS");
    label(c, font, 128, 496, "LIST");

    ToastWidget toast{8, 480, 96, 32, "CUTOFF", "87"};
    toast.draw(c, font);

    static const char* kTabs[3] = {"SYNTH", "SEQ", "MIX"};
    TabBarWidget tabs{128, 480, kTabs, 3, 1, 32};
    tabs.draw(c, font);

    static const char* kListItems[8] = {"DELAY", "REVERB", "CHORUS",
                                        "FILTER", "DRIVE", "PHASER",
                                        "FLANGER", "TREMOLO"};
    ListWidget list{128, 504, 104, 48, kListItems, 8, 3, 1};
    list.draw(c, font);

    // ── Virtual keyboard ────────────────────────────────────────────
    label(c, font, 0, 560, "KEYBOARD");
    VirtualKeyboardWidget kbd;
    kbd.x = 0; kbd.y = 568;
    kbd.text = "BASS LINE 01";
    kbd.textCursor = 12;
    kbd.shift = true;
    kbd.keyCursor = 17; // 'Q'-row, 'Y'
    kbd.draw(c, font);

    // ── File browser ────────────────────────────────────────────────
    label(c, font, 8, 640, "FILE BROWSER");
    static const char* kEntries[10] = {
        "DRUMS", "BASS", "KICK01.WAV", "SNARE01.WAV", "HAT01.WAV",
        "CLAP01.WAV", "RIM01.WAV", "TOM01.WAV", "CYM01.WAV", "FX01.WAV"};
    static const bool kIsDir[10] = {true, true, false, false, false,
                                    false, false, false, false, false};
    FileBrowserWidget fbw{8, 648, 112, 64, "/KITS/808", kEntries, kIsDir,
                          10, 2, 0};
    fbw.draw(c, font);

    // ── Edit boxes ──────────────────────────────────────────────────
    label(c, font, 8, 720, "EDIT BOX");

    // focused, labeled, cursor mid-text
    EditBoxWidget eb1{8, 736, 112, 16, "BASS LINE 01", 5, 0, true,
                      "PATTERN NAME", nullptr};
    eb1.draw(c, font);

    // focused, scrolled (long sample name), append cursor
    EditBoxWidget eb2{136, 736, 96, 16, "VERYLONGSAMPLE01",
                      16,
                      EditBoxWidget::ensureCursorVisible(16, 0, 96),
                      true, nullptr, nullptr};
    eb2.draw(c, font);

    // unfocused, empty with placeholder
    EditBoxWidget eb3{8, 760, 112, 8, "", 0, 0, false, nullptr,
                      "UNTITLED"};
    eb3.draw(c, font);

    // ── Toasts: worst-case long vs. short (auto-sized) ──────────────
    label(c, font, 8, 776, "TOAST AUTO-SIZE");
    // long: 1x label wins over the 2x value → box grows past the 96 min
    ToastWidget tLong{24, 784, 96, 32, "Filter Resonance", "19.9k"};
    tLong.draw(c, font);
    // short: stays at the 96 minimum, still balanced
    ToastWidget tShort{128, 784, 96, 32, "CUT", "87"};
    tShort.draw(c, font);

    // RGB565 → RGB888 and write.
    std::vector<unsigned char> rgb(size_t(kW) * kH * 3);
    for (size_t i = 0; i < fb.size(); ++i) {
        const uint16_t p = fb[i];
        rgb[i * 3 + 0] = uint8_t(((p >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = uint8_t(((p >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = uint8_t((p & 0x1F) * 255 / 31);
    }
    const int ok = stbi_write_png(path, kW, kH, 3, rgb.data(), kW * 3);
    if (ok) std::printf("[widgets] wrote %s (%dx%d)\n", path, kW, kH);
    else    std::fprintf(stderr, "[widgets] FAILED to write %s\n", path);
    return ok != 0;
}

// ── writeThemesPng: the SEQ page in all three themes, stacked ───────

bool writeThemesPng(const char* path) {
    constexpr int kLbl = 8;
    constexpr int kH = 3 * (kLbl + kDisplayH); // label + page per theme
    std::vector<uint16_t> big(size_t(kDisplayW) * kH, 0);

    // Representative SEQ page: pattern with accents/pitch marks, a
    // muted row, live playhead, velocity lane, LED row.
    UiState s;
    s.page = 1;
    s.mode = 1;
    s.playing = true;
    s.track = 0;
    s.bpm = 120.0;
    s.clockBar = 2; s.clockBeat = 3; s.clockStep = 9; s.clockPhase = 0.65f;
    s.seq.x = 24; s.seq.y = 32; s.seq.rows = 4; s.seq.rowH = 16;
    s.lane.x = 24; s.lane.y = 104;
    for (int st = 0; st < 16; ++st) {
        s.seq.cells[1][st] = (st % 4 == 0) ? kStepAccent
                           : (st % 2 == 0) ? kStepSet : kStepEmpty;
        s.lane.values[st] = uint8_t(40 + (st * 37) % 88);
    }
    s.seq.cells[0][0] = kStepAccentAlt; s.seq.cells[0][7] = kStepSetAlt;
    s.seq.rowMuted = 0b0100; // T3 muted
    s.seq.playhead = 5;
    s.lane.current = 5;
    for (int i = 0; i < 16; ++i) s.leds[i] = (i % 4 == 0) != (i == 5);

    Ui ui;
    uint16_t page[kDisplayW * kDisplayH];
    const ThemeId order[3] = {kThemeMono, kThemeRed, kThemeGreen};
    Canvas565 c{big.data(), kDisplayW, kH};
    Font5x7 font;
    for (int i = 0; i < 3; ++i) {
        setTheme(order[i]);
        ui.render(s, page);
        const int oy = i * (kLbl + kDisplayH);
        c.text(font, 4, oy, 1, themeName(order[i]), kColWhite);
        for (int row = 0; row < kDisplayH; ++row)
            for (int col = 0; col < kDisplayW; ++col)
                big[size_t(oy + kLbl + row) * kDisplayW + col] =
                    page[row * kDisplayW + col];
    }
    setTheme(kThemeMono); // restore default

    std::vector<unsigned char> rgb(big.size() * 3);
    for (size_t i = 0; i < big.size(); ++i) {
        const uint16_t p = big[i];
        rgb[i * 3 + 0] = uint8_t(((p >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = uint8_t(((p >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = uint8_t((p & 0x1F) * 255 / 31);
    }
    const int ok = stbi_write_png(path, kDisplayW, kH, 3, rgb.data(),
                                  kDisplayW * 3);
    if (ok) std::printf("[themes] wrote %s (%dx%d)\n", path, kDisplayW, kH);
    else    std::fprintf(stderr, "[themes] FAILED to write %s\n", path);
    return ok != 0;
}

} // namespace gb
