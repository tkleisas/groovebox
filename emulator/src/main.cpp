// groovebox_sim — PC emulator for the groovebox control surface.
//
// Boots the yawn audio engine headlessly (PortAudio default output),
// a SubtractiveSynth on track 0 and a DrumRack on track 1, maps the
// simulated 39-key panel (key indices per docs/panel-protocol.md) to
// engine MIDI commands, and renders Elektron-style UI pages to the
// 240x160 framebuffer shown in an SDL3 window. Engine boot, sequencer
// triggers and note events are logged to stdout so the behavior is
// verifiable without listening.
//
// Tracks: T1 = SubtractiveSynth (melodic), T2/T3/T4 = DrumRack lanes
// (kick 36 / hat 38 / clap 39), all pattern-driven by the 16-step
// sequencer while the transport runs.
//
// Usage: groovebox_sim [--smoke|--seqtest|--uitest]
//                      [--fontchart [out.png]] [--widgets [out.png]]
//   --smoke     unattended boot + notes on both engine tracks + one
//               frame dump per page, clean shutdown.
//   --seqtest   unattended sequencer test: programs steps, plays at
//               120 BPM, changes BPM mid-play, mutes T3, records a
//               note, asserts trigger counts, dumps SEQ frames.
//   --uitest    unattended interaction test: hold-step pitch editing,
//               TRK+/TRK- wrap, shift-mute — all through the HAL event
//               path. Asserts fired notes per step.
//   --fontchart / --widgets  PNG review renderers, no engine.

#include "hal.h"
#include "SimBackend.h"
#include "ui.h"
#include "fontchart.h"
#include "widgetshowcase.h"
#include "pattern.h"
#include "PanelView.h"

#include "stb_image_write.h" // implementation TU: fontchart.cpp
#include <SDL3/SDL.h>        // --panelprobe injects synthetic mouse events

#include "audio/AudioEngine.h"
#include "util/Factory.h"
#include "util/MessageQueue.h"
#include "midi/MidiTypes.h"
#include "instruments/DrumRack.h"
#include "instruments/SubtractiveSynth.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

namespace {

const char* noteName(int note, char* buf, size_t n) {
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    std::snprintf(buf, n, "%s%d", kNames[note % 12], note / 12 - 1);
    return buf;
}

// Synthesize a minimal default kit so the DrumRack is audible without
// loading sample files: kick 36 (C2), hat 38 (D2), clap 39 (D#2),
// snare 40 (E2).
void loadDefaultDrums(yawn::instruments::DrumRack* rack, double sr) {
    {   // kick: decaying sine with downward pitch sweep
        const int frames = int(sr * 0.30);
        std::vector<float> buf(size_t(frames) * 2);
        double phase = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double t = i / sr;
            const double freq = 40.0 + 90.0 * std::exp(-t * 25.0);
            phase += 2.0 * 3.14159265358979 * freq / sr;
            const float s = float(std::sin(phase) * std::exp(-t * 11.0) * 0.9);
            buf[size_t(i) * 2] = buf[size_t(i) * 2 + 1] = s;
        }
        rack->loadPad(36, buf.data(), frames, 2);
    }
    {   // hat: short decaying noise burst
        const int frames = int(sr * 0.06);
        std::vector<float> buf(size_t(frames) * 2);
        uint32_t rng = 22222;
        for (int i = 0; i < frames; ++i) {
            const double t = i / sr;
            rng = rng * 1664525u + 1013904223u;
            const float s = float((int(rng >> 16) / 32768.0 - 1.0) *
                                  std::exp(-t * 90.0) * 0.4);
            buf[size_t(i) * 2] = buf[size_t(i) * 2 + 1] = s;
        }
        rack->loadPad(38, buf.data(), frames, 2);
    }
    {   // clap: three short noise bursts then a longer tail
        const int frames = int(sr * 0.25);
        std::vector<float> buf(size_t(frames) * 2);
        uint32_t rng = 777;
        for (int i = 0; i < frames; ++i) {
            const double t = i / sr;
            rng = rng * 1664525u + 1013904223u;
            const double n = int(rng >> 16) / 32768.0 - 1.0;
            const double burst = std::fmod(t, 0.03) < 0.012 && t < 0.09
                                     ? 1.0 : std::exp(-(t - 0.09) * 30.0);
            const float s = float(n * burst * 0.5);
            buf[size_t(i) * 2] = buf[size_t(i) * 2 + 1] = s;
        }
        rack->loadPad(39, buf.data(), frames, 2);
    }
    {   // snare: 180 Hz body + noise top
        const int frames = int(sr * 0.18);
        std::vector<float> buf(size_t(frames) * 2);
        uint32_t rng = 4242;
        for (int i = 0; i < frames; ++i) {
            const double t = i / sr;
            rng = rng * 1664525u + 1013904223u;
            const double n = int(rng >> 16) / 32768.0 - 1.0;
            const double body = std::sin(2.0 * 3.14159265358979 * 180.0 * t) *
                                std::exp(-t * 25.0);
            const float s = float((body * 0.6 + n * std::exp(-t * 20.0) * 0.4)
                                  * 0.6);
            buf[size_t(i) * 2] = buf[size_t(i) * 2 + 1] = s;
        }
        rack->loadPad(40, buf.data(), frames, 2);
    }
    std::printf("[engine] DrumRack: default kit loaded "
                "(kick=36, hat=38, clap=39, snare=40)\n");
}

} // namespace

class App : public gb::HalHandler {
public:
    // testMode: 0 = interactive, 1 = --smoke, 2 = --seqtest
    int run(int testMode) {
        m_testMode = testMode;
        std::printf("groovebox_sim — yawn engine emulator%s\n",
                    testMode == 1 ? " (smoke test)"
                    : testMode == 2 ? " (sequencer test)"
                    : testMode == 3 ? " (UI interaction test)"
                    : testMode == 4 ? " (long-pattern test)"
                    : testMode == 5 ? " (panel mouse probe)" : "");

        if (!m_hal.init(*this)) return 1;

        // ── Boot the yawn engine (mirrors the real app's init) ──────
        m_engine = std::make_unique<yawn::audio::AudioEngine>();
        yawn::audio::AudioEngineConfig cfg; // 48 kHz, 256 frames, defaults
        if (!m_engine->init(cfg)) {
            std::fprintf(stderr, "[engine] init FAILED\n");
            return 1;
        }
        std::printf("[engine] init ok: %.0f Hz, %d frames, out=%dch in=%dch\n",
                    cfg.sampleRate, cfg.framesPerBuffer,
                    cfg.outputChannels, cfg.inputChannels);
        if (!m_engine->start()) {
            std::fprintf(stderr, "[engine] start FAILED (no audio device?)\n");
            return 1;
        }
        std::printf("[engine] started on default output device\n");

        m_engine->setInstrument(0, yawn::createInstrument("subsynth"));
        m_engine->setInstrument(1, yawn::createInstrument("drumrack"));
        m_engine->sendCommand(yawn::audio::SetTrackTypeMsg{0, 1}); // MIDI
        m_engine->sendCommand(yawn::audio::SetTrackTypeMsg{1, 1}); // MIDI
        m_engine->sendCommand(yawn::audio::TransportSetBPMMsg{m_state.bpm});
        std::printf("[engine] track 0: SubtractiveSynth (T1), track 1: "
                    "DrumRack (T2 kick/T3 hat/T4 clap)\n");

        if (auto* rack = dynamic_cast<yawn::instruments::DrumRack*>(
                m_engine->instrument(1)))
            loadDefaultDrums(rack, m_engine->sampleRate());

        // Push initial pot values into the synth params.
        for (int i = 0; i < 6; ++i) applyParam(i);

        // Static widget geometry (SEQ page: 4 tracks x 16 steps + lane).
        m_state.seq.x = 24; m_state.seq.y = 32;
        m_state.seq.rows = gb::Pattern::kTracks; m_state.seq.rowH = 16;
        m_state.lane.x = 24; m_state.lane.y = 104;
        setPage(testMode == 2 ? 1 : 0);

        // ── Main loop ───────────────────────────────────────────────
        std::printf("[app] running — %s\n",
                    testMode ? "test driver active" : "ESC quits");
        std::fflush(stdout);
        // Boot splash: interactive mode only (test drivers skip it so
        // timings stay exact).
        m_splash = (testMode == 0);
        m_splashStart = std::chrono::steady_clock::now();
        const auto t0 = std::chrono::steady_clock::now();
        auto lastBeat = t0;
        while (m_running) {
            m_hal.poll();

            m_engine->pollRetirements();
            yawn::audio::AudioEvent ev;
            while (m_engine->pollEvent(ev)) {
                // Live mixer levels: track 0-3 strips + master.
                if (auto* mu = std::get_if<yawn::audio::MeterUpdate>(&ev)) {
                    const float p = mu->peakL > mu->peakR ? mu->peakL
                                                          : mu->peakR;
                    const int slot = mu->trackIndex == -1 ? 4 : mu->trackIndex;
                    if (slot >= 0 && slot < 5) {
                        if (p > m_state.mixer.level[slot])
                            m_state.mixer.level[slot] = p;
                        if (p > m_state.mixer.peak[slot])
                            m_state.mixer.peak[slot] = p;
                    }
                }
            }
            // Meter decay (levels fast, peak markers slow).
            for (int i = 0; i < 5; ++i) {
                m_state.mixer.level[i] *= 0.92f;
                m_state.mixer.peak[i] *= 0.995f;
                if (m_state.mixer.peak[i] < m_state.mixer.level[i])
                    m_state.mixer.peak[i] = m_state.mixer.level[i];
            }

            updateSequencer();

            // Transport → UI clock + playing flag + REC indicator.
            const auto& tr = m_engine->transport();
            m_state.playing = tr.isPlaying();
            m_state.recording = m_recArmed && tr.isPlaying();
            const double beats = tr.positionInBeats();
            const double safeBeats = beats < 0.0 ? 0.0 : beats;
            m_state.clockBar   = int(safeBeats / 4.0) + 1;
            m_state.clockBeat  = int(safeBeats) % 4 + 1;
            m_state.clockStep  = int(safeBeats * 4.0) % 16 + 1;
            m_state.clockPhase = float(safeBeats - std::floor(safeBeats));

            syncPatternToUi();
            if (m_state.toastActive &&
                std::chrono::steady_clock::now() >= m_toastUntil)
                m_state.toastActive = false;
            if (m_testMode) testStep(t0); // after state updates

            if (m_splash) {
                m_ui.renderSplash(m_fb);
                if (std::chrono::steady_clock::now() - m_splashStart >=
                    std::chrono::milliseconds(1800)) {
                    m_splash = false;
                    setPage(1); // auto-advance to SEQ after the splash
                    std::printf("[ui] splash done -> SEQ page\n");
                }
            } else {
                m_ui.render(m_state, m_fb);
            }
            m_hal.presentFrame(m_fb);

            const auto now = std::chrono::steady_clock::now();
            if (now - lastBeat >= std::chrono::seconds(2)) {
                lastBeat = now;
                std::printf("[engine] alive: cpu=%.1f%% running=%d\n",
                            m_engine->cpuLoad() * 100.0,
                            int(m_engine->isRunning()));
                std::fflush(stdout);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }

        // ── Shutdown ────────────────────────────────────────────────
        std::printf("[engine] stopping...\n");
        m_engine->stop();
        m_engine->shutdown();
        m_hal.shutdown();
        std::printf("[engine] shutdown complete\n");
        std::fflush(stdout);
        if (m_testMode == 2) return seqtestVerdict();
        if (m_testMode == 3) return uitestVerdict();
        if (m_testMode == 4) return longtestVerdict();
        if (m_testMode == 5) return probeVerdict();
        return 0;
    }

    // ── HalHandler ──────────────────────────────────────────────────
    void onQuit() override { m_running = false; }

    void onThemeNext() override { // F12 in the sim
        gb::setTheme(gb::ThemeId((gb::currentTheme() + 1) % gb::kNumThemes));
        toast("THEME", gb::themeName(gb::currentTheme()));
        std::printf("[ui] theme -> %s\n", gb::themeName(gb::currentTheme()));
    }

    void onKey(int index, bool pressed) override {
        using namespace gb;
        // boot splash: any key skips
        if (pressed && m_splash) { m_splash = false; return; }
        if (index == kKeyShiftL || index == kKeyShiftR) {
            if (index == kKeyShiftL) m_shiftL = pressed;
            else                     m_shiftR = pressed;
            return;
        }
        // Length-edit state: any (non-shift) key press exits, swallowed.
        if (m_lenEdit && pressed) {
            m_lenEdit = false;
            std::printf("[seq] length edit done (LEN %d)\n",
                        m_pattern.length);
            return;
        }
        // Piano keys (white 0..15, black 16..26)
        if (index >= kKeyWhite0 && index <= 26) {
            if (shiftHeld()) {
                // shift + white 1-4 = track select, white 5-8 = mute
                if (pressed && index <= 7) {
                    if (index <= 3) selectTrack(index);
                    else            muteTrack(index - 4);
                }
                return; // shift swallows piano keys
            }
            if (m_state.mode == 1) {           // SEQUENCER mode
                // Hold-to-edit gesture: press arms the step; toggling
                // happens on RELEASE only if no encoder edit happened
                // while held (Elektron-style hold-step + turn).
                if (index <= 15) {
                    const int step = m_window + index; // window-relative
                    if (step >= m_pattern.length) return; // past pattern end
                    if (pressed) {
                        // Repeat-storm guard: a second down for an
                        // already-held key must not reset the edit flag
                        // (SimBackend filters SDL auto-repeat, but the
                        // app is robust even if one leaks through).
                        if (m_heldStep == index) return;
                        m_heldStep = index;
                        m_heldEdited = false;
                    } else {
                        if (m_heldStep == index) {
                            if (!m_heldEdited)
                                toggleStep(m_state.track, step);
                            m_heldStep = -1;
                        }
                    }
                } else if (pressed) {
                    const int step =
                        m_window + kBlackStepMap[index - kKeyBlack0];
                    if (step < m_pattern.length) toggleAccent(step);
                }
            } else {                           // PLAY mode
                playKey(index, pressed);
            }
            return;
        }
        if (!pressed) return;
        switch (index) {
        case kKeyPlay:  togglePlay(); break;
        case kKeyStop:  stopTransport(); break;
        case kKeyRec:   toggleRec(); break;
        case kKeyMode:  toggleMode(); break;
        case kKeyPrev:  prevNext(-1); break;
        case kKeyNext:  prevNext(+1); break;
        case kKeySoft1: softKey(0); break;
        case kKeySoft2: softKey(1); break;
        case kKeySoft3: softKey(2); break;
        case kKeySoft4: softKey(3); break;
        default: break;
        }
    }

    void onEncoderDelta(int index, int delta) override {
        // FIX (hold-step edit): a held step in SEQ mode takes priority
        // over page routing — holding a white key and turning encoder
        // 1/2 must edit that step even if the SYNTH page is showing
        // (previously the page switch ate the gesture: wheel → cutoff).
        if (m_heldStep >= 0 && m_state.track == 0 &&
            (index == 0 || index == 1)) {
            editHeldStep(index, delta);
            return;
        }
        // Length-edit state: encoder 1 adjusts, others ignored.
        if (m_lenEdit) {
            if (index == 0) adjustLength(delta);
            return;
        }
        switch (m_state.page) {
        case 0: { // SYNTH: CUT/RES/ATK/REL
            auto& p = m_state.params[index];
            p.value = std::max(0, std::min(127, p.value + delta * 2));
            std::printf("[panel] ENC%d %s = %d\n", index + 1, p.name, p.value);
            applyParam(index);
            break;
        }
        case 1:   // SEQ: NTE/GTE/BPM (held steps were handled above)
            if (index == 0) {
                const int n = std::max(24, std::min(84,
                    int(m_pattern.t1Note) + delta));
                m_pattern.t1Note = uint8_t(n);
                char nb[8];
                std::printf("[panel] T1 note = %s(%d)\n",
                            noteName(n, nb, sizeof(nb)), n);
            } else if (index == 1) {
                m_pattern.t1Gate = std::max(0.1f, std::min(1.0f,
                    m_pattern.t1Gate + delta * 0.05f));
                std::printf("[panel] T1 gate = %.2f\n",
                            double(m_pattern.t1Gate));
            } else if (index == 2) {
                setBpm(m_state.bpm + delta);
            }
            break;
        case 2: { // MIXER: LV1..LV4
            m_vol[index] = std::max(0.0f, std::min(1.0f,
                m_vol[index] + delta * 0.05f));
            m_engine->sendCommand(yawn::audio::SetTrackVolumeMsg{
                index, m_vol[index]});
            std::printf("[panel] T%d volume = %.2f\n", index + 1,
                        double(m_vol[index]));
            break;
        }
        }
    }

    void onAnalog(int index, float value) override {
        using namespace gb;
        if (index >= kAnalogPot0 && index < kAnalogPot0 + 6) {
            const int i = index - kAnalogPot0;
            m_state.params[i].value = int(value * 127.0f + 0.5f);
            std::printf("[panel] POT%d %s = %d\n", i + 1,
                        m_state.params[i].name, m_state.params[i].value);
            applyParam(i);
        } else if (index == kAnalogSlider) {
            m_slider = value;
            std::printf("[panel] SLIDER velocity = %d\n", velocity7());
        } else if (index == kAnalogJoyX) {
            // pitch bend on the selected track (32-bit value field —
            // SubtractiveSynth decodes via Convert::pb32toFloat)
            m_engine->sendCommand(yawn::audio::SendMidiToTrackMsg{
                gb::Pattern::engineTrack(m_state.track),
                uint8_t(yawn::midi::MidiMessage::Type::PitchBend),
                0, 0, 0, yawn::midi::Convert::floatToPb32(value), 0});
        } else if (index == kAnalogJoyY) {
            // mod wheel = CC1 on the selected track
            const uint8_t v7 = uint8_t(std::max(0.0f, value) * 127.0f);
            m_engine->sendCommand(yawn::audio::SendMidiToTrackMsg{
                gb::Pattern::engineTrack(m_state.track),
                uint8_t(yawn::midi::MidiMessage::Type::ControlChange),
                0, 0, 0, yawn::midi::Convert::cc7to32(v7), 1});
        }
    }

private:
    // Semitone offsets of the 16 white keys from the base note:
    // C D E F G A B | C D E F G A B C D (two octaves, C to D).
    static constexpr int kWhiteOffsets[16] =
        {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26};
    // 11 black keys: C# D# F# G# A# (oct 0), C# D# F# G# A# (oct 1), C#
    static constexpr int kBlackOffsets[11] =
        {1, 3, 6, 8, 10, 13, 15, 18, 20, 22, 25};
    // Black key (index 16..26) → white step at its left (accent target
    // in SEQ mode): black between whites i/i+1 accents white step i.
    static constexpr int kBlackStepMap[11] =
        {0, 1, 3, 4, 5, 7, 8, 10, 11, 12, 14};

    // ── Page framework ──────────────────────────────────────────────
    struct PageDef {
        const char* name;
        const char* enc[4];
        const char* soft[4];
    };
    // Page indices into UiState::page. SAMPLE/SETTINGS slot in here.
    static constexpr int kNumPages = 3;
    inline static const PageDef kPages[kNumPages] = {
        {"SYNTH", {"CUT", "RES", "ATK", "REL"},
                  {"OCT-", "OCT+", "TRK-", "TRK+"}},
        {"SEQ",   {"NTE", "GTE", "BPM", ""},
                  {"LEN", "4FLR", "TRK-", "TRK+"}},
        {"MIXER", {"LV1", "LV2", "LV3", "LV4"},
                  {"MUTE", "-", "TRK-", "TRK+"}},
    };

    void setPage(int p) {
        m_state.page = p;
        for (int i = 0; i < 4; ++i) {
            m_state.encLabels[i] = kPages[p].enc[i];
            m_state.softLabels[i] = kPages[p].soft[i];
        }
        std::printf("[panel] page -> %s\n", kPages[p].name);
    }

    void softKey(int i) {
        if (i == 2) { trackCycle(-1); return; } // S3 = TRK- on all pages
        if (i == 3) { trackCycle(+1); return; } // S4 = TRK+ on all pages
        switch (m_state.page) {
        case 0: // SYNTH
            if (i == 0)      setOctave(-1);
            else if (i == 1) setOctave(+1);
            break;
        case 1: // SEQ: S1 = LEN (shift+S1 = CLR), S2 = 4FLR
            if (i == 0) {
                if (shiftHeld()) {
                    m_pattern.clearTrack(m_state.track);
                    std::printf("[seq] T%d pattern cleared\n",
                                m_state.track + 1);
                } else {
                    m_lenEdit = true;
                    char vb[8];
                    std::snprintf(vb, sizeof(vb), "%03d", m_pattern.length);
                    toast("LEN", vb);
                    std::printf("[seq] length edit (ENC1 adjusts 1..256, "
                                "any key exits)\n");
                }
            } else if (i == 1) {
                m_pattern.fillFourFloor(m_state.track);
                std::printf("[seq] T%d four-on-the-floor fill\n",
                            m_state.track + 1);
            }
            break;
        case 2: // MIXER: S1 = mute selected track
            if (i == 0) muteTrack(m_state.track);
            break;
        }
    }

    void trackCycle(int dir) {
        selectTrack((m_state.track + dir + gb::Pattern::kTracks) %
                    gb::Pattern::kTracks);
    }

    void prevNext(int dir) {
        if (shiftHeld()) {
            if (m_state.page == 1) pageWindow(dir);  // SEQ: step window
            else setBpm(m_state.bpm + dir);          // elsewhere: BPM
        } else {
            setPage((m_state.page + dir + kNumPages) % kNumPages);
        }
    }

    void setBpm(double bpm) {
        m_state.bpm = std::max(30.0, std::min(300.0, bpm));
        m_engine->sendCommand(yawn::audio::TransportSetBPMMsg{m_state.bpm});
        std::printf("[engine] BPM = %.0f\n", m_state.bpm);
    }

    void setOctave(int dir) {
        m_baseNote = std::max(12, std::min(96, m_baseNote + dir * 12));
        char nb[8];
        std::printf("[panel] octave -> base %s(%d)\n",
                    noteName(m_baseNote, nb, sizeof(nb)), m_baseNote);
    }

    bool shiftHeld() const { return m_shiftL || m_shiftR; }

    // Held-step edit (SEQ mode, T1): encoder 1 = per-step pitch
    // (chromatic, C2..C6), encoder 2 = per-step gate (50..100%).
    // Absolute step = window + held white-key index.
    void editHeldStep(int enc, int delta) {
        const int step = m_window + m_heldStep;
        auto& s = m_pattern.steps[0][step];
        m_heldEdited = true;
        if (enc == 0) {
            const int note = std::max(36, std::min(96,
                m_pattern.t1Note + s.noteOffset + delta));
            s.noteOffset = int8_t(note - m_pattern.t1Note);
            char nb[8];
            toast("NOTE", noteName(note, nb, sizeof(nb)));
            std::printf("[seq] T1 step %02d note = %s(%d)\n",
                        step, nb, note);
        } else {
            int g = s.gate > 0 ? s.gate
                               : int(m_pattern.t1Gate * 100.0f + 0.5f);
            g = std::max(50, std::min(100, g + delta * 5));
            s.gate = uint8_t(g);
            char vb[8];
            std::snprintf(vb, sizeof(vb), "%d%%", g);
            toast("GATE", vb);
            std::printf("[seq] T1 step %02d gate = %d%%\n", step, g);
        }
    }

    // Pattern length edit (SEQ S1 = LEN; any key exits).
    void adjustLength(int delta) {
        m_pattern.length = std::max(1, std::min(gb::Pattern::kMaxSteps,
                                                m_pattern.length + delta));
        const int maxWin = (m_pattern.length - 1) / 16 * 16;
        if (m_window > maxWin) m_window = maxWin;
        char vb[8];
        std::snprintf(vb, sizeof(vb), "%03d", m_pattern.length);
        toast("LEN", vb);
        std::printf("[seq] LEN = %d\n", m_pattern.length);
    }

    // Window paging (SEQ page, shift+</>): manual ±16-step moves;
    // landing on the playhead's page re-engages follow mode.
    void pageWindow(int dir) {
        const int pages = m_pattern.pageCount();
        int pg = m_window / 16 + dir;
        pg = std::max(0, std::min(pages - 1, pg));
        m_window = pg * 16;
        m_follow = (m_playhead >= 0 && m_playhead / 16 == pg);
        char vb[8];
        std::snprintf(vb, sizeof(vb), "%d/%d", pg + 1, pages);
        toast("PAGE", vb);
        std::printf("[seq] window -> steps %d-%d (page %d/%d)%s\n",
                    m_window, m_window + 15, pg + 1, pages,
                    m_follow ? " [follow]" : "");
    }

    // Transient on-screen feedback (ToastWidget overlay, ~1.2 s).
    void toast(const char* label, const char* value) {
        std::snprintf(m_state.toastLabel, sizeof(m_state.toastLabel),
                      "%s", label);
        std::snprintf(m_state.toastValue, sizeof(m_state.toastValue),
                      "%s", value);
        m_state.toastActive = true;
        m_toastShown = true; // uitest assertion
        m_toastUntil = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(1200);
    }

    // ── Tracks ──────────────────────────────────────────────────────
    void selectTrack(int t) {
        m_state.track = t;
        m_state.pageName = (t == 0) ? "SYNTH" : "DRUMS";
        std::printf("[panel] track select -> T%d (%s)\n", t + 1,
                    t == 0 ? "SubtractiveSynth"
                    : t == 1 ? "DrumRack kick"
                    : t == 2 ? "DrumRack hat" : "DrumRack clap");
    }

    void muteTrack(int t) {
        m_muted[t] = !m_muted[t];
        // Drum lanes (T2-4) share yawn track 1, so their mute is
        // scheduler-side (skip firing). T1 also mutes the engine track.
        if (t == 0)
            m_engine->sendCommand(yawn::audio::SetTrackMuteMsg{
                0, m_muted[0]});
        std::printf("[panel] T%d %s\n", t + 1,
                    m_muted[t] ? "MUTED" : "unmuted");
    }

    void toggleMode() {
        m_state.mode ^= 1;
        std::printf("[panel] MODE -> %s (keyboard row)\n",
                    m_state.mode == 0 ? "PLAY" : "SEQ");
    }

    // ── Transport ───────────────────────────────────────────────────
    void togglePlay() {
        if (m_engine->transport().isPlaying()) {
            m_engine->sendCommand(yawn::audio::TransportStopMsg{});
            allNotesOff();
            std::printf("[engine] transport STOP\n");
        } else {
            m_engine->sendCommand(yawn::audio::TransportPlayMsg{});
            std::printf("[engine] transport PLAY\n");
        }
    }

    void stopTransport() {
        m_engine->sendCommand(yawn::audio::TransportStopMsg{});
        m_engine->sendCommand(yawn::audio::TransportSetPositionMsg{0});
        allNotesOff();
        std::printf("[engine] transport STOP + return-to-zero\n");
    }

    void toggleRec() {
        m_recArmed = !m_recArmed;
        std::printf("[panel] REC %s (play-mode notes record into the "
                    "selected track's pattern)\n",
                    m_recArmed ? "ARMED" : "off");
    }

    // ── Pattern editing ─────────────────────────────────────────────
    void toggleStep(int track, int step) {
        auto& s = m_pattern.steps[track][step];
        s.on = !s.on;
        if (s.on) s.vel = uint8_t(velocity7());
        else      s.accent = false;
        std::printf("[seq] T%d step %02d %s vel=%d\n", track + 1, step,
                    s.on ? "ON " : "off", s.vel);
    }

    void toggleAccent(int step) {
        auto& s = m_pattern.steps[m_state.track][step];
        if (!s.on) return; // accent only meaningful on set steps
        s.accent = !s.accent;
        std::printf("[seq] T%d step %02d accent %s\n", m_state.track + 1,
                    step, s.accent ? "ON" : "off");
    }

    // ── Play mode ───────────────────────────────────────────────────
    void playKey(int keyIndex, bool pressed) {
        const int t = m_state.track;
        const int note = (t == 0)
            ? m_baseNote + (keyIndex <= 15 ? kWhiteOffsets[keyIndex]
                                           : kBlackOffsets[keyIndex - 16])
            : m_pattern.noteForTrack(t);
        sendNote(t, note, pressed, pressed ? velocity7() : 0);
        if (pressed) {
            char nb[8];
            std::printf("[engine] NOTE ON  T%d %s(%d) vel%d\n", t + 1,
                        noteName(note, nb, sizeof(nb)), note, velocity7());
            // Live recording into the selected track's pattern
            // (quantize to the nearest 16th).
            if (m_recArmed && m_engine->transport().isPlaying()) {
                const double beats =
                    m_engine->transport().positionInBeats();
                const int step =
                    int(beats * 4.0 + 0.5) % m_pattern.length;
                auto& s = m_pattern.steps[t][step];
                s.on = true;
                s.vel = uint8_t(velocity7());
                if (t == 0) // keep the played pitch on T1
                    s.noteOffset = int8_t(note - m_pattern.t1Note);
                m_recordedNote = true;
                std::printf("[seq] REC T%d step %02d vel=%d @beat %.2f\n",
                            t + 1, step, s.vel, beats);
            }
        } else {
            std::printf("[engine] NOTE OFF T%d note %d\n", t + 1, note);
        }
        std::fflush(stdout);
    }

    void sendNote(int track, int note, bool on, int vel7) {
        if (on) ++m_probeNotes; // panelprobe assertion
        m_engine->sendCommand(yawn::audio::SendMidiToTrackMsg{
            gb::Pattern::engineTrack(track),
            uint8_t(on ? yawn::midi::MidiMessage::Type::NoteOn
                       : yawn::midi::MidiMessage::Type::NoteOff),
            0, uint8_t(note),
            yawn::midi::Convert::vel7to16(uint8_t(vel7)), 0});
    }

    // ── Sequencer scheduling (per frame, beat-derived) ──────────────
    void updateSequencer() {
        const auto& tr = m_engine->transport();
        if (!tr.isPlaying()) {
            allNotesOff();
            for (int t = 0; t < 4; ++t) m_lastStep[t] = -1;
            m_playhead = -1;
            return;
        }
        const double beats = tr.positionInBeats();
        const int64_t absStep = int64_t(beats * 4.0);
        // Global pattern length: the cycle wraps here (not at 16).
        const int step = int(absStep % m_pattern.length);
        m_playhead = step;
        for (int t = 0; t < 4; ++t) {
            // gate: NoteOff ~70% (or T1's gate) into the step
            if (m_live[t].active && beats >= m_live[t].offBeat) {
                sendNote(t, m_live[t].note, false, 0);
                m_live[t].active = false;
            }
            if (m_muted[t]) continue;
            // Strictly-increasing step counter: a mid-play BPM change
            // rescales positionInBeats slightly backward, which a `!=`
            // check would re-fire as a double trigger. Skipping until
            // the counter passes its old maximum is the safe side.
            if (absStep > m_lastStep[t]) {
                m_lastStep[t] = absStep;
                const auto& s = m_pattern.steps[t][step];
                if (s.on) fireStep(t, step, s, beats);
            }
        }
    }

    void fireStep(int t, int step, const gb::Pattern::Step& s,
                  double beats) {
        const int note = m_pattern.noteForStep(t, step);
        const int vel = s.accent ? 127 : s.vel;
        sendNote(t, note, true, vel);
        const float gate = m_pattern.gateForStep(t, step);
        m_live[t] = {true, note, beats + 0.25 * gate};
        ++m_fires[t];
        if (t == 0) { // T1 per-step assertions (seqtest/uitest/longtest)
            m_firedNote[step] = note;
            m_fireBeat[step] = beats;
            m_fireVel[step] = vel;
            ++m_fireCnt[step];
        }
        if (m_muteWatch && t == 2) ++m_muteViolations; // seqtest assertion
        std::printf("[seq] T%d step %02d note %d vel %d @beat %.2f\n",
                    t + 1, step, note, vel, beats);
        std::fflush(stdout);
    }

    void allNotesOff() {
        for (int t = 0; t < 4; ++t) {
            if (m_live[t].active) {
                sendNote(t, m_live[t].note, false, 0);
                m_live[t].active = false;
            }
        }
    }

    // ── UI sync (pattern → widgets, pattern+playhead → LEDs) ────────
    void syncPatternToUi() {
        // Follow mode: the 16-step window tracks the playhead's page.
        if (m_follow && m_playhead >= 0)
            m_window = (m_playhead / 16) * 16;
        const int sel = m_state.track;
        const int len = m_pattern.length;
        // playhead column within the window (-1 if on another page)
        const int ph = (m_playhead >= m_window &&
                        m_playhead < m_window + 16)
                           ? m_playhead - m_window : -1;
        m_state.seq.playhead = ph;
        m_state.seq.rowMuted = 0;
        m_state.seqLength = len;
        m_state.seqWindow = m_window;
        m_state.lenEdit = m_lenEdit;
        for (int t = 0; t < 4; ++t) {
            if (m_muted[t]) m_state.seq.rowMuted |= uint8_t(1u << t);
            for (int s = 0; s < 16; ++s) {
                const int idx = m_window + s;
                if (idx >= len) { // past pattern end: render empty
                    m_state.seq.cells[t][s] = gb::kStepEmpty;
                    continue;
                }
                const auto& st = m_pattern.steps[t][idx];
                const bool alt = (t == 0 && st.noteOffset != 0);
                m_state.seq.cells[t][s] =
                    !st.on    ? gb::kStepEmpty
                    : st.accent ? (alt ? gb::kStepAccentAlt : gb::kStepAccent)
                                : (alt ? gb::kStepSetAlt : gb::kStepSet);
            }
        }
        m_state.seqNote = m_pattern.t1Note;
        m_state.seqGatePct = int(m_pattern.t1Gate * 100.0f + 0.5f);
        for (int s = 0; s < 16; ++s) {
            const int idx = m_window + s;
            const auto& st = m_pattern.steps[sel][idx];
            m_state.lane.values[s] = (idx < len && st.on) ? st.vel : 0;
        }
        m_state.lane.current = ph;
        for (int i = 0; i < 4; ++i) m_state.mixer.mute[i] = m_muted[i];
        // LEDs: selected track's window steps, playhead inverts. Rev-A
        // LEDs are on/off only (no brightness), so accents read the
        // same as set steps.
        for (int i = 0; i < 16; ++i) {
            const int idx = m_window + i;
            const bool on = idx < len && m_pattern.steps[sel][idx].on;
            const bool v = on != (i == ph);
            m_state.leds[i] = v;
            m_hal.setLed(i, v);
        }
    }

    int velocity7() const {
        return std::max(1, std::min(127, int(m_slider * 127.0f)));
    }

    // Map UI params to the SubtractiveSynth on track 0.
    void applyParam(int i) {
        auto* inst = m_engine->instrument(0);
        if (!inst) return;
        using P = yawn::instruments::SubtractiveSynth;
        const float v = m_state.params[i].value / 127.0f;
        switch (i) {
        case 0: inst->setParameter(P::kFilterCutoff, v); break;
        case 1: inst->setParameter(P::kFilterResonance, v); break;
        case 2: inst->setParameter(P::kAmpAttack, 0.001f + v * 2.0f); break;
        case 3: inst->setParameter(P::kAmpDecay, 0.001f + v * 2.0f); break;
        case 4: inst->setParameter(P::kAmpSustain, v); break;
        case 5: inst->setParameter(P::kAmpRelease, 0.001f + v * 2.0f); break;
        }
    }

    // ── Test drivers ────────────────────────────────────────────────
    void testStep(std::chrono::steady_clock::time_point t0) {
        const double t = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();
        auto once = [this](int step) {
            if (m_testDone[step]) return false;
            m_testDone[step] = true; return true;
        };
        if (m_testMode == 1)      smokeStep(t, once);
        else if (m_testMode == 2) seqtestStep(t, once);
        else if (m_testMode == 3) uitestStep(t, once);
        else if (m_testMode == 4) longtestStep(t, once);
        else                      probeStep(t, once);
    }

    // --smoke: boot + notes on both engine tracks + frame per page.
    template <typename Once>
    void smokeStep(double t, Once& once) {
        if (t >= 1.5 && once(0)) { std::printf("[smoke] T1 note C4\n"); playKey(0, true); }
        if (t >= 2.0 && once(1)) playKey(0, false);
        if (t >= 2.2 && once(2)) { std::printf("[smoke] T1 note E4\n"); playKey(2, true); }
        if (t >= 2.7 && once(3)) playKey(2, false);
        if (t >= 3.0 && once(4)) { std::printf("[smoke] T2 kick\n"); selectTrack(1); playKey(0, true); }
        if (t >= 3.2 && once(5)) playKey(0, false);
        if (t >= 3.4 && once(6)) { std::printf("[smoke] T3 hat\n"); selectTrack(2); playKey(0, true); }
        if (t >= 3.6 && once(7)) { playKey(0, false); selectTrack(0); }
        if (t >= 4.0 && once(8)) {
            std::printf("[smoke] transport PLAY + held C4 (live meters)\n");
            togglePlay();
            playKey(0, true); // held through the page dumps
        }
        if (t >= 5.0 && once(9)) {
            setPage(0); m_state.selected = 2;
            // UTF-8 demo line: Greek + Cyrillic + Latin mixed.
            static const char* kDemo = "ΤΑΣΟΣ Привет SYNTH";
            m_state.overlayLine = kDemo;
            std::printf("[smoke] UTF-8 demo \"%s\": bytes=%zu codepoints=%d\n",
                        kDemo, std::strlen(kDemo), gb::utf8Length(kDemo));
            dumpFrame("smoke_frame.rgb565");
        }
        if (t >= 5.2 && once(10)) { setPage(1); m_state.overlayLine = nullptr; }
        if (t >= 5.4 && once(11)) dumpFrame("smoke_seq.rgb565");
        if (t >= 5.6 && once(12)) setPage(2);
        if (t >= 5.8 && once(13)) dumpFrame("smoke_mix.rgb565");
        if (t >= 6.0 && once(14)) playKey(0, false); // release held C4
        if (t >= 6.2 && once(15)) { std::printf("[smoke] done — shutting down\n"); m_running = false; }
    }

    // --seqtest: program steps, play, BPM change, mute, record, assert.
    template <typename Once>
    void seqtestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[seqtest] programming steps: T2 kick 0/4/8/12 "
                        "(accent), T3 hat 2/6/10/14, T1 C4 0/7\n");
            m_state.mode = 1; // SEQ mode
            for (int s : {0, 4, 8, 12}) {
                m_pattern.steps[1][s].on = true;
                m_pattern.steps[1][s].accent = true;
                m_pattern.steps[1][s].vel = 120;
            }
            for (int s : {2, 6, 10, 14}) {
                m_pattern.steps[2][s].on = true;
                m_pattern.steps[2][s].vel = 100;
            }
            for (int s : {0, 7}) {
                m_pattern.steps[0][s].on = true;
                m_pattern.steps[0][s].vel = 110;
            }
            selectTrack(1); // SEQ page shows T2 row selected... then T1
        }
        if (t >= 0.5 && once(1)) {
            std::printf("[seqtest] PLAY @ 120 BPM\n");
            togglePlay();
        }
        if (t >= 1.5 && once(2)) dumpFrame("seqtest_1.rgb565");
        if (t >= 2.0 && once(3)) {
            std::printf("[seqtest] BPM -> 90 mid-play\n");
            setBpm(90.0);
        }
        if (t >= 2.5 && once(4)) {
            std::printf("[seqtest] toggle T3 step 4 on (live edit)\n");
            toggleStep(2, 4);
        }
        if (t >= 3.0 && once(5)) {
            std::printf("[seqtest] shift-mute T3\n");
            muteTrack(2);
            m_muteWatch = true;
        }
        if (t >= 3.4 && once(6)) {
            std::printf("[seqtest] REC armed, play-mode note on T1\n");
            m_state.mode = 0; // PLAY mode
            selectTrack(0);
            toggleRec();
            playKey(0, true);
        }
        if (t >= 3.55 && once(7)) {
            playKey(0, false);
            toggleRec(); // disarm
            m_state.mode = 1;
        }
        if (t >= 4.2 && once(8)) dumpFrame("seqtest_2.rgb565");
        if (t >= 4.6 && once(9)) {
            std::printf("[seqtest] STOP\n");
            stopTransport();
        }
        if (t >= 4.8 && once(10)) {
            std::printf("[seqtest] done — asserting\n");
            m_running = false;
        }
    }

    // --uitest: hold-step pitch editing, TRK cycling, shift-mute — all
    // driven through the HAL event path (onKey/onEncoderDelta).
    template <typename Once>
    void uitestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[uitest] SEQ mode; select T1 via shift+white1; "
                        "toggle steps 0/3/7\n");
            m_state.mode = 1; // SEQ mode
            setPage(1);
            onKey(gb::kKeyShiftL, true);
            onKey(0, true); onKey(0, false);
            onKey(gb::kKeyShiftL, false);
            for (int s : {0, 3, 7}) { onKey(s, true); onKey(s, false); }
        }
        if (t >= 0.6 && once(1)) {
            std::printf("[uitest] hold step 3 + ENC1 x4 -> E4\n");
            onKey(3, true);
            for (int i = 0; i < 4; ++i) onEncoderDelta(0, +1);
            dumpFrame("uitest_toast.rgb565"); // toast visible mid-edit
            onKey(3, false);
        }
        if (t >= 0.9 && once(2)) {
            std::printf("[uitest] hold step 7 + ENC1 x7 -> G4, "
                        "ENC2 x3 -> gate 85%%\n");
            onKey(7, true);
            for (int i = 0; i < 7; ++i) onEncoderDelta(0, +1);
            onEncoderDelta(1, +3); // 70% -> 85%
            onKey(7, false);
        }
        if (t >= 1.1 && once(3)) { std::printf("[uitest] PLAY\n"); togglePlay(); }
        if (t >= 1.0 && once(9)) {
            // Repeat-storm regression: toggle step 5 on, then hold it,
            // edit pitch, interleave duplicate key-down events (an
            // auto-repeat storm leaking past the backend), release.
            // The edit must stick and the step must NOT toggle off.
            onKey(5, true); onKey(5, false);          // toggle on
            onKey(5, true);                            // hold
            onEncoderDelta(0, +2);                     // +2 semitones
            onKey(5, true); onKey(5, true); onKey(5, true); // storm
            onKey(5, false);                           // release
            const auto& st = m_pattern.steps[0][5];
            m_repeatOk = st.on && st.noteOffset == 2;
            std::printf("[uitest] repeat storm during hold: step5 on=%d "
                        "offset=%d\n", int(st.on), int(st.noteOffset));
        }
        if (t >= 2.0 && once(4)) dumpFrame("uitest_seq.rgb565");
        if (t >= 2.2 && once(5)) {
            std::printf("[uitest] TRK+/TRK- wrap test\n");
            m_trkOk = true;
            softKey(3); m_trkOk &= (m_state.track == 1); // TRK+ T1->T2
            softKey(3); softKey(3);                      // ->T3 ->T4
            softKey(3); m_trkOk &= (m_state.track == 0); // wraps T4->T1
            softKey(2); m_trkOk &= (m_state.track == 3); // TRK- wraps T1->T4
            softKey(2); m_trkOk &= (m_state.track == 2); // T4->T3
            selectTrack(0);
        }
        if (t >= 2.4 && once(6)) {
            std::printf("[uitest] shift-mute T3 via HAL path\n");
            onKey(gb::kKeyShiftL, true);
            onKey(6, true); onKey(6, false); // shift+white 7 -> mute T3
            onKey(gb::kKeyShiftL, false);
            m_muteOk = m_muted[2];
            onKey(gb::kKeyShiftL, true);
            onKey(6, true); onKey(6, false); // toggle back
            onKey(gb::kKeyShiftL, false);
            m_muteOk = m_muteOk && !m_muted[2];
        }
        if (t >= 2.6 && once(7)) stopTransport();
        if (t >= 2.8 && once(8)) {
            std::printf("[uitest] done — asserting\n");
            m_running = false;
        }
    }

    int uitestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[uitest] ASSERT %-44s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        std::printf("[uitest] T1 fired notes: step0=%d step3=%d step7=%d\n",
                    m_firedNote[0], m_firedNote[3], m_firedNote[7]);
        check(m_firedNote[0] == 60, "step 0 fired C4 (track default)");
        check(m_firedNote[3] == 64, "step 3 fired E4 (per-step pitch)");
        check(m_firedNote[7] == 67, "step 7 fired G4 (per-step pitch)");
        check(m_fires[0] >= 3, "T1 steps fired >= 3 times");
        check(m_trkOk, "TRK-/TRK+ cycle wraps (T1..T4->T1->T4->T3)");
        check(m_muteOk, "shift+white7 mutes/unmutes T3");
        check(m_toastShown, "toast shown during hold-step edit");
        check(m_repeatOk, "repeat storm during hold: edit kept, no toggle");
        std::printf("[uitest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --longtest: 24-step pattern across two pages, wrap, mid-play
    // shrink, window paging + follow — all via the HAL event path.
    template <typename Once>
    void longtestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[longtest] SEQ mode, T1; LEN gesture -> 24\n");
            m_state.mode = 1;
            setPage(1);
            selectTrack(0);
            softKey(0);                    // S1 = LEN
            for (int i = 0; i < 8; ++i) onEncoderDelta(0, +1); // 16+8
            onKey(gb::kKeySoft4, true); onKey(gb::kKeySoft4, false); // exit
            m_lenOk = (m_pattern.length == 24) && !m_lenEdit;
            // step 0 on page 1
            onKey(0, true); onKey(0, false);
            // shift+> to page 2 (manual window)
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyNext, true); onKey(gb::kKeyNext, false);
            onKey(gb::kKeyShiftL, false);
            m_windowOk = (m_window == 16) && !m_follow;
            // step 20 = white 5 on page 2; accent via the black key
            // left of white 5 (black index 19); pitch +4 (E4) via hold
            onKey(4, true); onKey(4, false);   // toggle step 20 on
            onKey(19, true); onKey(19, false); // accent step 20
            onKey(4, true);                    // hold step 20
            for (int i = 0; i < 4; ++i) onEncoderDelta(0, +1);
            onKey(4, false);
        }
        if (t >= 0.9 && once(1)) {
            std::printf("[longtest] PLAY @ 120 BPM\n");
            togglePlay();
        }
        // window still manual (16-23) — dump shows page 2/2
        if (t >= 2.0 && once(2)) dumpFrame("longtest_page2.rgb565");
        if (t >= 3.6 && once(3)) {
            std::printf("[longtest] shrink LEN -> 8 mid-play\n");
            softKey(0);
            for (int i = 0; i < 16; ++i) onEncoderDelta(0, -1);
            onKey(gb::kKeySoft4, true); onKey(gb::kKeySoft4, false);
            m_shrinkOk = (m_pattern.length == 8);
        }
        if (t >= 3.9 && once(4)) {
            // shift+< back to window 0 — playhead is in page 1 with
            // length 8, so follow mode must re-engage
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyPrev, true); onKey(gb::kKeyPrev, false);
            onKey(gb::kKeyShiftL, false);
            m_followOk = m_follow && m_window == 0;
            std::printf("[longtest] shift+< -> window 0, follow %s\n",
                        m_follow ? "ON" : "off");
        }
        if (t >= 4.6 && once(5)) stopTransport();
        if (t >= 4.8 && once(6)) {
            std::printf("[longtest] done — asserting\n");
            m_running = false;
        }
    }

    int longtestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[longtest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        std::printf("[longtest] step20: note=%d vel=%d beat=%.2f cnt=%d; "
                    "step0 cnt=%d\n", m_firedNote[20], m_fireVel[20],
                    m_fireBeat[20], m_fireCnt[20], m_fireCnt[0]);
        check(m_lenOk, "LEN gesture sets 24, any key exits");
        check(m_windowOk, "shift+> pages window to 16 (manual mode)");
        check(m_firedNote[20] == 64, "step 20 fires E4 (pitch across pages)");
        check(m_fireVel[20] == 127, "step 20 fires accented (vel 127)");
        check(std::fabs(m_fireBeat[20] - 5.0) < 0.15,
              "step 20 fires at beat 5.0 (24-step cycle)");
        check(m_fireCnt[20] == 1, "step 20 silent after shrink to 8");
        check(m_fireCnt[0] >= 2, "step 0 fires before+after shrink wrap");
        check(m_shrinkOk, "LEN shrink to 8 mid-play");
        check(m_followOk, "shift+< to playhead page re-engages follow");
        std::printf("[longtest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --panelprobe: injects synthetic SDL mouse events (real backend
    // hit-test + event path) — click white key 1, drag RES pot upward,
    // wheel over encoder 3 (ATK on the SYNTH page).
    template <typename Once>
    void probeStep(double t, Once& once) {
        auto pushButton = [](float x, float y, bool down) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                          : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.x = x; e.button.y = y;
            SDL_PushEvent(&e);
        };
        auto pushMotion = [](float x, float y) {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.x = x; e.motion.y = y;
            SDL_PushEvent(&e);
        };
        auto pushWheel = [](float x, float y, int d) {
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.mouse_x = x; e.wheel.mouse_y = y;
            e.wheel.integer_y = d; e.wheel.y = float(d);
            SDL_PushEvent(&e);
        };
        // panel coords: mm*4. White key 1 center ≈ (46mm, 134mm).
        if (t >= 0.5 && once(0)) {
            std::printf("[probe] click white key 1 (46,134)mm\n");
            pushButton(46.0f * 4, 134.0f * 4, true);
        }
        if (t >= 0.8 && once(1)) pushButton(46.0f * 4, 134.0f * 4, false);
        // RES pot center (158mm, 32mm); drag up 60 px = +0.5
        if (t >= 1.1 && once(2)) {
            std::printf("[probe] drag RES pot up 60px\n");
            pushButton(158.0f * 4, 32.0f * 4, true);
            for (int i = 1; i <= 6; ++i)
                pushMotion(158.0f * 4, (32.0f * 4) - i * 10.0f);
            pushButton(158.0f * 4, 32.0f * 4 - 60.0f, false);
        }
        // encoder 3 center (70mm, 88mm); wheel up twice
        if (t >= 1.5 && once(3)) {
            std::printf("[probe] wheel over ENC3\n");
            pushWheel(70.0f * 4, 88.0f * 4, +1);
            pushWheel(70.0f * 4, 88.0f * 4, +1);
        }
        if (t >= 1.8 && once(4)) {
            std::printf("[probe] done — asserting\n");
            m_running = false;
        }
    }

    int probeVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[probe] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_probeNotes == 1, "mouse click on white key 1 -> note event");
        check(m_state.params[1].value != 41, "RES pot drag -> analog delta");
        check(m_state.params[2].value != 3, "wheel over ENC3 -> encoder delta");
        std::printf("[probe] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    int seqtestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[seqtest] ASSERT %-40s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        std::printf("[seqtest] fires: T1=%d T2=%d T3=%d T4=%d\n",
                    m_fires[0], m_fires[1], m_fires[2], m_fires[3]);
        check(m_fires[1] >= 5, "T2 kick fired >= 5 times");
        check(m_fires[2] >= 4, "T3 hat fired >= 4 times (pre-mute)");
        check(m_fires[0] >= 2, "T1 C4 fired >= 2 times");
        check(m_muteViolations == 0, "no T3 triggers after mute");
        check(m_recordedNote, "play-mode note recorded into pattern");
        int t1On = 0;
        for (int s = 0; s < 16; ++s) if (m_pattern.steps[0][s].on) ++t1On;
        check(t1On >= 3, "T1 pattern holds recorded + programmed steps");
        std::printf("[seqtest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // Render current state and dump the raw RGB565 frame (headless
    // verification; convert with tools/rgb565_to_png.py).
    void dumpFrame(const char* path) {
        m_ui.render(m_state, m_fb);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(m_fb, sizeof(uint16_t),
                        gb::kDisplayW * gb::kDisplayH, f);
            std::fclose(f);
            std::printf("[test] frame dumped to %s\n", path);
        }
    }

    gb::SimBackend m_hal;
    gb::Ui m_ui;
    gb::UiState m_state;
    uint16_t m_fb[gb::kDisplayW * gb::kDisplayH] = {};
    std::unique_ptr<yawn::audio::AudioEngine> m_engine;

    gb::Pattern m_pattern;
    bool m_muted[4] = {};
    float m_vol[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool m_recArmed = false;

    // scheduler state
    struct LiveNote { bool active = false; int note = 0; double offBeat = 0.0; };
    LiveNote m_live[4];
    int64_t m_lastStep[4] = {-1, -1, -1, -1};

    int m_baseNote = 60; // C4
    float m_slider = 0.8f;
    bool m_shiftL = false, m_shiftR = false;
    bool m_running = true;
    int m_testMode = 0;
    bool m_testDone[16] = {};

    // hold-step edit gesture (SEQ mode)
    int m_heldStep = -1;
    bool m_heldEdited = false;

    // long patterns: 16-step window + follow + length edit
    int m_window = 0;          // first visible step (multiple of 16)
    bool m_follow = true;      // window tracks the playhead's page
    bool m_lenEdit = false;    // length-edit state (SEQ S1)
    int m_playhead = -1;       // pattern step playing now (-1 stopped)

    // toast overlay
    std::chrono::steady_clock::time_point m_toastUntil{};
    bool m_toastShown = false;

    // boot splash (interactive mode only)
    bool m_splash = false;
    std::chrono::steady_clock::time_point m_splashStart{};

    // seqtest/uitest/longtest assertion state
    int m_fires[4] = {};
    int m_firedNote[gb::Pattern::kMaxSteps] = {}; // T1 per step
    double m_fireBeat[gb::Pattern::kMaxSteps] = {};
    int m_fireVel[gb::Pattern::kMaxSteps] = {};
    int m_fireCnt[gb::Pattern::kMaxSteps] = {};
    bool m_trkOk = false;
    bool m_muteOk = false;
    bool m_muteWatch = false;
    int m_muteViolations = 0;
    bool m_recordedNote = false;
    bool m_lenOk = false, m_windowOk = false;
    bool m_shrinkOk = false, m_followOk = false;
    bool m_repeatOk = false;
    int m_probeNotes = 0;
};

namespace {

// --paneldump: render one panel frame (with a real device screen page
// and some demo control activity) to a PNG, no window/engine needed.
bool writePanelDumpPng(const char* path) {
    // device screen: the default SYNTH page
    gb::Ui ui;
    gb::UiState state; // defaults: SYNTH page, params, T1 120
    uint16_t screen[gb::kDisplayW * gb::kDisplayH];
    ui.render(state, screen);

    gb::PanelState ps;
    ps.keyDown[gb::kKeyPlay] = true;          // PLAY held
    ps.keyDown[gb::kKeyWhite0 + 4] = true;    // white 5 held
    const float pots[6] = {87.f / 127.f, 41.f / 127.f, 3.f / 127.f,
                           55.f / 127.f, 70.f / 127.f, 24.f / 127.f};
    for (int i = 0; i < 6; ++i) ps.pots[i] = pots[i];
    ps.slider = 0.8f;
    ps.joyX = 0.3f; ps.joyY = 0.2f;
    for (int i = 0; i < 16; i += 4) ps.leds[i] = true;
    ps.activeType = gb::PanelView::kHitPot;   // highlight CUT knob
    ps.activeIndex = 0;

    std::vector<uint16_t> fb(size_t(gb::PanelView::kW) * gb::PanelView::kH);
    gb::Canvas565 c{fb.data(), gb::PanelView::kW, gb::PanelView::kH};
    gb::Font5x7 font;
    gb::PanelView::render(c, font, ps, screen);

    std::vector<unsigned char> rgb(fb.size() * 3);
    for (size_t i = 0; i < fb.size(); ++i) {
        const uint16_t p = fb[i];
        rgb[i * 3 + 0] = uint8_t(((p >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = uint8_t(((p >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = uint8_t((p & 0x1F) * 255 / 31);
    }
    const int ok = stbi_write_png(path, gb::PanelView::kW, gb::PanelView::kH,
                                  3, rgb.data(), gb::PanelView::kW * 3);
    if (ok) std::printf("[paneldump] wrote %s (%dx%d)\n", path,
                        gb::PanelView::kW, gb::PanelView::kH);
    else    std::fprintf(stderr, "[paneldump] FAILED to write %s\n", path);
    return ok != 0;
}

// --splashdump: render the boot splash frame to a PNG.
bool writeSplashDumpPng(const char* path) {
    gb::Ui ui;
    uint16_t fb[gb::kDisplayW * gb::kDisplayH];
    ui.renderSplash(fb);
    std::vector<unsigned char> rgb(size_t(gb::kDisplayW) * gb::kDisplayH * 3);
    for (size_t i = 0; i < size_t(gb::kDisplayW) * gb::kDisplayH; ++i) {
        const uint16_t p = fb[i];
        rgb[i * 3 + 0] = uint8_t(((p >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = uint8_t(((p >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = uint8_t((p & 0x1F) * 255 / 31);
    }
    const int ok = stbi_write_png(path, gb::kDisplayW, gb::kDisplayH, 3,
                                  rgb.data(), gb::kDisplayW * 3);
    if (ok) std::printf("[splashdump] wrote %s (%dx%d)\n", path,
                        gb::kDisplayW, gb::kDisplayH);
    else    std::fprintf(stderr, "[splashdump] FAILED to write %s\n", path);
    return ok != 0;
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fontchart") == 0) {
            const char* out = (i + 1 < argc && argv[i + 1][0] != '-')
                                  ? argv[i + 1] : "fontchart.png";
            return gb::writeFontChartPng(out) ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--widgets") == 0) {
            const char* out = (i + 1 < argc && argv[i + 1][0] != '-')
                                  ? argv[i + 1] : "widgets.png";
            return gb::writeWidgetShowcasePng(out) ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--paneldump") == 0) {
            const char* out = (i + 1 < argc && argv[i + 1][0] != '-')
                                  ? argv[i + 1] : "panel.png";
            return writePanelDumpPng(out) ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--splashdump") == 0) {
            const char* out = (i + 1 < argc && argv[i + 1][0] != '-')
                                  ? argv[i + 1] : "bootsplash.png";
            return writeSplashDumpPng(out) ? 0 : 1;
        }
        if (std::strcmp(argv[i], "--themes") == 0) {
            const char* out = (i + 1 < argc && argv[i + 1][0] != '-')
                                  ? argv[i + 1] : "themes.png";
            return gb::writeThemesPng(out) ? 0 : 1;
        }
    }
    int testMode = 0;
    if (argc > 1 && std::strcmp(argv[1], "--smoke") == 0)    testMode = 1;
    if (argc > 1 && std::strcmp(argv[1], "--seqtest") == 0)  testMode = 2;
    if (argc > 1 && std::strcmp(argv[1], "--uitest") == 0)   testMode = 3;
    if (argc > 1 && std::strcmp(argv[1], "--longtest") == 0) testMode = 4;
    if (argc > 1 && std::strcmp(argv[1], "--panelprobe") == 0) testMode = 5;
    App app;
    return app.run(testMode);
}
