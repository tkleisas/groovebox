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
#ifdef GB_PI_BACKEND
#include "PiBackend.h" // headless Pi: fbdev display + RtMidi panel
#else
#include "SimBackend.h"
#endif
#include "ui.h"
#include "fontchart.h"
#include "widgetshowcase.h"
#include "pattern.h"
#include "PanelView.h"
#include "PanelViewNSR2.h" // was transitive via SimBackend.h
#include "capture.h"
#include "sampleio.h"

#include "stb_image_write.h" // implementation TU: fontchart.cpp
#ifndef GB_PI_BACKEND
#include <SDL3/SDL.h>        // --panelprobe injects synthetic mouse events
#endif

#include "audio/AudioEngine.h"
#include "audio/OfflineRenderer.h"
#include "util/Factory.h"
#include "util/MessageQueue.h"
#include "midi/MidiTypes.h"
#include "instruments/DrumRack.h"
#include "instruments/DrumSynth.h"
#include "instruments/DrumSlop.h"
#include "instruments/SubtractiveSynth.h"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

// Backend selection (see emulator/CMakeLists.txt GB_PI_BACKEND): the
// SDL3 sim on desktop, the headless fbdev+RtMidi backend on the Pi.
// Both implement the gb::Hal interface from hal.h.
#ifdef GB_PI_BACKEND
using HalBackend = gb::PiBackend;
#else
using HalBackend = gb::SimBackend;
#endif

namespace {

const char* noteName(int note, char* buf, size_t n) {
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F",
                                     "F#", "G", "G#", "A", "A#", "B"};
    std::snprintf(buf, n, "%s%d", kNames[note % 12], note / 12 - 1);
    return buf;
}

// Synthesized drum one-shots (M2d: one pad per per-track DrumRack —
// T2 kick@36, T3 hat@38, T4 clap@39).
std::vector<float> makeDrumSample(int kind, double sr) {
    std::vector<float> buf;
    uint32_t rng = 22222 + uint32_t(kind) * 7777;
    auto noise = [&rng] {
        rng = rng * 1664525u + 1013904223u;
        return int(rng >> 16) / 32768.0 - 1.0;
    };
    int frames = 0;
    switch (kind) {
    case 0: frames = int(sr * 0.30); break;  // kick
    case 1: frames = int(sr * 0.06); break;  // hat
    case 2: frames = int(sr * 0.25); break;  // clap
    default: frames = int(sr * 0.18); break; // snare
    }
    buf.resize(size_t(frames) * 2);
    double phase = 0.0;
    for (int i = 0; i < frames; ++i) {
        const double t = i / sr;
        float s = 0.0f;
        if (kind == 0) {      // kick: swept decaying sine
            const double freq = 40.0 + 90.0 * std::exp(-t * 25.0);
            phase += 2.0 * 3.14159265358979 * freq / sr;
            s = float(std::sin(phase) * std::exp(-t * 11.0) * 0.9);
        } else if (kind == 1) // hat: short noise burst
            s = float(noise() * std::exp(-t * 90.0) * 0.4);
        else if (kind == 2) { // clap: three bursts + tail
            const double burst = std::fmod(t, 0.03) < 0.012 && t < 0.09
                                     ? 1.0 : std::exp(-(t - 0.09) * 30.0);
            s = float(noise() * burst * 0.5);
        } else {              // snare: 180 Hz body + noise top
            const double body = std::sin(2.0 * 3.14159265358979 * 180.0 * t) *
                                std::exp(-t * 25.0);
            s = float((body * 0.6 + noise() * std::exp(-t * 20.0) * 0.4) *
                      0.6);
        }
        buf[size_t(i) * 2] = buf[size_t(i) * 2 + 1] = s;
    }
    return buf;
}
} // namespace

class App : public gb::HalHandler {
public:
    void setPanelProfile(gb::PanelProfile p) {
        m_nsr2 = (p == gb::kPanelNSR2);
        m_winSize = m_nsr2 ? 8 : 16;
        if (m_nsr2) m_velSource = 1; // NSR-2 default: fixed 100
    }
    void setProfileSeconds(int s) { m_profileSecs = s; }

    // testMode: 0 = interactive, 1 = --smoke, 2 = --seqtest
    int run(int testMode) {
        m_testMode = testMode;
        m_hal.setPanelProfile(m_nsr2 ? gb::kPanelNSR2 : gb::kPanelNSR1);
        std::printf("groovebox_sim — yawn engine emulator%s\n",
                    testMode == 1 ? " (smoke test)"
                    : testMode == 2 ? " (sequencer test)"
                    : testMode == 3 ? " (UI interaction test)"
                    : testMode == 4 ? " (long-pattern test)"
                    : testMode == 5 ? " (panel mouse probe)"
                    : testMode == 6 ? " (parameter/FX test)"
                    : testMode == 7 ? " (sample/settings test)"
                    : testMode == 8 ? " (NSR-2 grid test)"
                    : testMode == 9 ? " (NSR-1 rev B test)"
                    : testMode == 10 ? " (macro encoder test)"
                    : testMode == 11 ? " (track/channel test)"
                    : testMode == 12 ? " (bounce/clip test)"
                    : testMode == 13 ? " (drum key-map test)"
                    : testMode == 14 ? " (last-played-pad edit test)" : "");

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

        // M2d: one engine track per UI track. T1 = SubtractiveSynth;
        // T2/T3/T4 each a DrumRack with its synthesized one-shot.
        m_engine->setInstrument(0, yawn::createInstrument("subsynth"));
        m_engine->sendCommand(yawn::audio::SetTrackTypeMsg{0, 1}); // MIDI
        static const char* kDrumNames[3] = {"kick", "hat", "clap"};
        for (int t = 1; t < 4; ++t) {
            auto rack = yawn::createInstrument("drumrack");
            auto* r = dynamic_cast<yawn::instruments::DrumRack*>(
                rack.get());
            if (r) {
                auto smp = makeDrumSample(t - 1, m_engine->sampleRate());
                r->loadPad(gb::Pattern::kPadNote[t], smp.data(),
                           int(smp.size()) / 2, 2);
            }
            m_engine->setInstrument(t, std::move(rack));
            m_engine->sendCommand(yawn::audio::SetTrackTypeMsg{t, 1});
        }
        m_engine->sendCommand(yawn::audio::TransportSetBPMMsg{m_state.bpm});
        std::printf("[engine] tracks 0-3: T1=SubtractiveSynth, "
                    "T2=kick T3=hat T4=clap (one DrumRack each)\n");
        for (int t = 0; t < 4; ++t) rebuildDrumVoices(t);

        // Bind pot/encoder params by name and sync the panel to the
        // engine: pots start engaged at the panel's default positions
        // (real panels adopt physical state at boot; pickup re-arms on
        // track/page changes only). ADSR encoders are relative.
        for (int t = 0; t < 4; ++t) // -1 = factory ADSR default
            for (int i = 0; i < 4; ++i)
                m_macroParamByTrack[t][i] = -1;
        rebindParams();
        for (int i = 0; i < 2; ++i) {
            if (m_potParam[i] >= 0) {
                setNormParam(m_potParam[i], m_state.params[i].value / 127.0f);
                m_potEngaged[i] = true;
                m_potPrev[i] = m_state.params[i].value / 127.0f;
            }
        }
        for (int i = 0; i < 4; ++i)
            if (m_adsrParam[i] >= 0)
                setNormParam(m_adsrParam[i], m_state.params[2 + i].value / 127.0f);

        // FX chooser list: descriptor display names + "NONE".
        for (const auto& d : yawn::audioEffectDescriptors())
            m_fxItems.push_back(d.displayName);
        m_fxItems.push_back("NONE");
        for (const auto& d : yawn::instrumentDescriptors())
            m_instItems.push_back(d.displayName);
        m_instItems.push_back("KEEP");
        for (const auto& d : yawn::midiEffectDescriptors())
            m_mfxItems.push_back(d.displayName);
        m_mfxItems.push_back("NONE");
        m_state.chooserItems = m_fxItems.data();
        m_state.chooserCount = int(m_fxItems.size());

        // settings + browser (persisted settings load interactive-only)
        m_state.nsr2 = m_nsr2;
        m_state.textBuf = m_textBuf;
        if (testMode == 0) loadSettings();
        syncSettingsUi();
        refreshBrowser();

        // Static widget geometry (SEQ page: 4 tracks x 16 steps + lane).
        m_state.seq.x = 24; m_state.seq.y = 32;
        m_state.seq.rows = gb::Pattern::kTracks; m_state.seq.rowH = 16;
        m_state.lane.x = 24; m_state.lane.y = 104;
        setPage(testMode == 2 ? 1 : 0);

        // --profile N: heavy-load harness runs INSTEAD of the
        // interactive/test loop, then falls through to shutdown.
        if (m_profileSecs > 0) {
            const int rc = runProfile(m_profileSecs);
            std::printf("[engine] stopping...\n");
            m_engine->stop();
            m_engine->shutdown();
            m_hal.shutdown();
            std::printf("[engine] shutdown complete\n");
            std::fflush(stdout);
            return rc;
        }

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
            syncParamsToUi();
            pollSample();
            if (m_state.toastActive &&
                std::chrono::steady_clock::now() >= m_toastUntil)
                m_state.toastActive = false;
            macroTimeout(); // 3 s macro-assign timeout
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
        if (m_testMode == 6) return paramtestVerdict();
        if (m_testMode == 7) return sampletestVerdict();
        if (m_testMode == 8) return nsr2testVerdict();
        if (m_testMode == 9) return revbtestVerdict();
        if (m_testMode == 10) return macrotestVerdict();
        if (m_testMode == 11) return tracktestVerdict();
        if (m_testMode == 12) return bouncetestVerdict();
        if (m_testMode == 13) return drumtestVerdict();
        if (m_testMode == 14) return padtestVerdict();
        return 0;
    }

    // ── HalHandler ──────────────────────────────────────────────────
    void onQuit() override {
        if (m_testMode || m_profileSecs > 0) { m_running = false; return; }
        if (m_confirmAction == 0) openConfirm(2); // ESC asks first
        else closeConfirm();                      // ESC again = cancel
    }

    void onThemeNext() override { // F12 in the sim
        gb::setTheme(gb::ThemeId((gb::currentTheme() + 1) % gb::kNumThemes));
        toast("THEME", gb::themeName(gb::currentTheme()));
        std::printf("[ui] theme -> %s\n", gb::themeName(gb::currentTheme()));
    }

    // Encoder push = reset that param to its default (SYNTH: bound
    // instrument param; FX: effect param).
    void onEncoderPush(int index, bool pressed) override {
        if (!pressed || index < 0 || index > 7 || m_state.chooserOpen)
            return;
        // Macro encoders 5-8: push = reset bound param to its default;
        // shift+push = enter ASSIGN (push again cancels).
        if (index >= 4) {
            const int slot = index - 4;
            if (shiftHeld()) { macroPushAssign(slot); return; }
            if (m_assignMacro == slot) { macroPushAssign(slot); return; }
            const int p = m_macroParam[slot];
            auto* inst = selInstrument();
            if (inst && p >= 0) {
                const auto& pi = inst->parameterInfo(p);
                inst->setParameter(p, pi.defaultValue);
                toast("RESET", pi.name);
                std::printf("[panel] macro %d %s -> default\n", slot + 1,
                            pi.name);
            }
            return;
        }
        if (m_state.page == 0) {
            const int p = m_encParam[index];
            auto* inst = selInstrument();
            if (inst && p >= 0) {
                const auto& pi = inst->parameterInfo(p);
                inst->setParameter(p, pi.defaultValue);
                toast("RESET", pi.name);
                std::printf("[panel] %s -> default\n", pi.name);
            }
        } else if (m_state.page == 3) {
            if (auto* fx = fxOnSelectedTrack()) {
                if (index < fx->parameterCount()) {
                    const auto& pi = fx->parameterInfo(index);
                    fx->setParameter(index, pi.defaultValue);
                    toast("RESET", pi.name);
                    std::printf("[fx] %s -> default\n", pi.name);
                }
            }
        }
    }

    void onKey(int index, bool pressed) override {
        using namespace gb;
        // boot splash: any key skips
        if (pressed && m_splash) { m_splash = false; return; }
        // NSR-2: translate panel key indices to NSR-1 logical keys;
        // grid keys (0..31) get their own handler below the modal gate.
        // NOTE: translation of MODE (35) lands on 31 — the grid branch
        // must use the RAW index, never the translated one.
        const int rawIndex = index;
        if (m_nsr2 && index > 31 && index != kN2Space) {
            static const int8_t kMap[45 - 32] = {
                kKeyPlay, kKeyStop, kKeyRec, kKeyMode, kKeyPrev, kKeyNext,
                kKeySoft1, kKeySoft2, kKeySoft3, kKeySoft4,
                kKeyShiftL, kKeyShiftR, -1};
            const int8_t mapped = kMap[index - 32];
            if (mapped < 0) return;
            index = mapped;
        }
        // confirm dialog is modal: S1 = OK, S2 = CANCEL, rest swallowed
        if (m_confirmAction != 0) {
            if (!pressed) return;
            if (index == kKeySoft1) doConfirm();
            else if (index == kKeySoft2) closeConfirm();
            return;
        }
        if (index == kKeyShiftL || index == kKeyShiftR) {
            // rev B: tapping SHIFT while a step is held = accent that
            // step (single gesture, works on all 16 step keys)
            if (pressed && m_heldStep >= 0 && m_heldRow < 0) {
                toggleAccent(m_window + m_heldStep);
                m_heldEdited = true;
            }
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
        // NSR-2 grid keys (0..31, RAW index) — step/iso-play/text modes
        if (m_nsr2 && rawIndex <= 31) {
            onGridKey(rawIndex, pressed);
            return;
        }
        if (m_nsr2 && index == kN2Space) { // wide bar
            if (pressed && m_state.mode == 2) textType(' ');
            return;
        }
        // NSR-1 rev B: the step row (40..55) is ALWAYS the sequencer.
        if (!m_nsr2 && index >= kKeyStep0 && index < kKeyStep0 + 16) {
            onStepRowKey(index - kKeyStep0, pressed);
            return;
        }
        // Piano keys (white 0..15, black 16..26)
        if (index >= kKeyWhite0 && index <= 26) {
            // rev B: the piano row is ALWAYS playable (MODE toggles
            // scale-lock, not play/step). While a step is held, a piano
            // tap p-locks that step's pitch (and sounds for feedback).
            pianoKey(index, pressed);
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
        if (m_heldStep >= 0 &&
            (m_state.track == 0 || isDrumTrack(m_state.track)) &&
            (index == 0 || index == 1)) {
            editHeldStep(index, delta);
            return;
        }
        // Length-edit state: encoder 1 adjusts, others ignored.
        if (m_lenEdit) {
            if (index == 0) adjustLength(delta);
            return;
        }
        // FX chooser: encoder 1 scrolls the list.
        if (m_state.chooserOpen) {
            if (index == 0) chooserScroll(delta);
            return;
        }
        // Macro ASSIGN mode: turning a pageable encoder (1-4, SYNTH
        // page) binds its parameter to the pending macro.
        if (m_assignMacro >= 0) {
            if (index <= 3 && m_state.page == 0 && m_encParam[index] >= 0)
                macroBind(m_assignMacro, m_encParam[index]);
            return; // other encoders do nothing while assigning
        }
        // Macro encoders 5-8 (HAL 4..7): relative edit, any page.
        if (index >= 4) {
            editMacro(index - 4, delta);
            return;
        }
        switch (m_state.page) {
        case 0: // SYNTH: encoders edit the current param page's params
            editBoundParam(m_encParam[index],
                           shiftHeld() ? delta / 128.0f : delta / 16.0f,
                           /*isEncoder=*/true);
            break;
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
        case 3: { // FX: encoders edit the first 4 effect params (slot)
            auto* fx = fxOnSelectedTrack();
            if (fx && index < fx->parameterCount())
                editFxParam(fx, index,
                            shiftHeld() ? delta / 128.0f : delta / 16.0f);
            break;
        }
        case 7: { // MFX: encoders edit the first 4 MIDI-effect params
            auto* fx = m_engine->midiEffectChain(m_state.track).effect(0);
            if (fx && index < fx->parameterCount()) {
                const auto& pi = fx->parameterInfo(index);
                const float range = pi.maxValue - pi.minValue;
                float norm = range > 0.0f
                    ? (fx->getParameter(index) - pi.minValue) / range
                    : 0.0f;
                norm = std::max(0.0f, std::min(1.0f,
                    norm + (shiftHeld() ? delta / 128.0f : delta / 16.0f)));
                fx->setParameter(index, pi.minValue + norm * range);
                char vb[16];
                formatParam(pi, norm, vb, sizeof(vb));
                toastLow(pi.name, vb);
                std::printf("[mfx] %s = %s\n", pi.name, vb);
            }
            break;
        }
        case 8:   // TRACK: VOL / PAN / input channel
            if (index == 0) {
                m_trackVol = std::max(0.0f, std::min(1.0f,
                    m_trackVol + delta * 0.05f));
                m_engine->sendCommand(yawn::audio::SetTrackVolumeMsg{
                    m_state.track, m_trackVol});
            } else if (index == 1) {
                m_trackPan = std::max(-1.0f, std::min(1.0f,
                    m_trackPan + delta * 0.05f));
                m_engine->sendCommand(yawn::audio::SetTrackPanMsg{
                    m_state.track, m_trackPan});
            } else if (index == 2) {
                m_trackInputCh = (m_trackInputCh + delta + 4) % 4;
                m_engine->sendCommand(yawn::audio::SetTrackAudioInputChMsg{
                    m_state.track, m_trackInputCh});
            }
            break;
        case 4:   // SAMPLE: trim start/end, gain (review state only)
            if (m_sampleState == 2 && m_takeLen > 0) {
                if (index == 0) {
                    m_trim0 = std::max(0.0f, std::min(m_trim1 - 0.01f,
                        m_trim0 + delta * 0.01f));
                    std::printf("[sample] trim start %.2f\n", double(m_trim0));
                } else if (index == 1) {
                    m_trim1 = std::max(m_trim0 + 0.01f, std::min(1.0f,
                        m_trim1 + delta * 0.01f));
                    std::printf("[sample] trim end %.2f\n", double(m_trim1));
                } else if (index == 2) {
                    m_gain = std::max(0.1f, std::min(4.0f,
                        m_gain + delta * 0.05f));
                    std::printf("[sample] gain %.2f\n", double(m_gain));
                }
            }
            break;
        case 5:   // LOAD: encoder 1 scrolls the browser
            if (index == 0) {
                auto& b = m_state.browser;
                b.selected = std::max(0, std::min(b.count - 1,
                                                  b.selected + delta));
                const int rows = (112 - 10) / 8;
                if (b.selected < b.scroll) b.scroll = b.selected;
                if (b.selected >= b.scroll + rows)
                    b.scroll = b.selected - rows + 1;
            }
            break;
        case 6:   // SET: enc1 = row, enc2 = adjust
            if (index == 0) {
                m_state.settingsSel = (m_state.settingsSel + delta + 4) % 4;
            } else if (index == 1) {
                settingsAdjust(delta);
            }
            break;
        }
    }

    void onAnalog(int index, float value) override {
        using namespace gb;
        if (index >= kAnalogPot0 && index < kAnalogPot0 + 2) { // 2 pots
            potMove(index - kAnalogPot0, value);
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
    // Page IDs (internal, stable): 0 INST, 1 SEQ, 2 MIXER, 3 FX,
    // 4 SAMPLE, 5 LOAD, 6 SET, 7 MFX, 8 TRACK.
    // Cycle order (M2d): SEQ / INST / MFX / FX / MIX / SAMPLE / LOAD /
    // SET / TRACK.
    static constexpr int kNumPages = 9;
    inline static const PageDef kPages[kNumPages] = {
        // INST/FX/MFX encoder labels are dynamic (bound param names).
        {"INST",   {"", "", "", ""}, {"INST", "PG+", "TRK-", "TRK+"}},
        {"SEQ",    {"NTE", "GTE", "BPM", ""}, {"LEN", "4FLR", "TRK-", "TRK+"}},
        {"MIXER",  {"LV1", "LV2", "LV3", "LV4"}, {"MUTE", "-", "TRK-", "TRK+"}},
        {"FX",     {"", "", "", ""}, {"LOAD", "BYP", "TRK-", "TRK+"}},
        {"SAMPLE", {"TRIM-", "TRIM+", "GAIN", ""}, {"REC", "STOP", "NORM", "ASSIGN"}},
        {"LOAD",   {"SEL", "", "", ""}, {"OPEN", "UP", "TRK-", "TRK+"}},
        {"SET",    {"SEL", "ADJ", "", ""}, {"ADJ", "SAVE", "TRK-", "TRK+"}},
        {"MFX",    {"", "", "", ""}, {"LOAD", "BYP", "TRK-", "TRK+"}},
        {"TRACK",  {"VOL", "PAN", "IN", ""}, {"TYP", "MON", "BOUNCE", "TRK+"}},
    };
    // prevNext cycles in this order
    inline static const int kPageOrder[kNumPages] =
        {1, 0, 7, 3, 2, 4, 5, 6, 8};

    void setPage(int p) {
        if (p != m_state.page)          // real page change re-arms
            for (int i = 0; i < 2; ++i) m_potEngaged[i] = false;
        m_state.page = p;
        for (int i = 0; i < 4; ++i) {
            m_state.encLabels[i] = kPages[p].enc[i];
            m_state.softLabels[i] = kPages[p].soft[i];
        }
        std::printf("[panel] page -> %s\n", kPages[p].name);
    }

    void softKey(int i) {
        // Macro ASSIGN modal: S1 = reset to factory ADSR, else cancel
        if (m_assignMacro >= 0) {
            const int slot = m_assignMacro;
            if (i == 0) macroResetDefault(slot);
            else {
                m_assignMacro = -1;
                toast("MACRO", "CANCEL");
            }
            return;
        }
        // S3/S4 = TRK-/TRK+ on most pages; SAMPLE (S3=NORM, S4=ASSIGN)
        // and TRACK (S3=BOUNCE) opt out.
        if (i == 2 && m_state.page != 4 && m_state.page != 8) {
            trackCycle(-1); return;
        }
        if (i == 3 && m_state.page != 4) { trackCycle(+1); return; }
        switch (m_state.page) {
        case 0: // INST: S1 = instrument picker, S2 = param page,
                //        shift+S1 = OCT-, shift+S2 = OCT+
            if (i == 0) {
                if (shiftHeld()) setOctave(-1);
                else             chooserKey(1); // instrument picker
            } else if (i == 1) {
                if (shiftHeld()) setOctave(+1);
                else             nextEncPage(+1);
            }
            break;
        case 1: // SEQ: S1 = LEN (shift+S1 = CLR), S2 = 4FLR
            if (i == 0) {
                if (shiftHeld()) {
                    openConfirm(1); // clear track asks first
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
        case 3: // FX: S1 = chooser, S2 = bypass (shift+S2 = slot 1/2)
            if (i == 0)      chooserKey(0);
            else if (i == 1) {
                if (shiftHeld()) {
                    m_fxSlot ^= 1;
                    std::printf("[fx] slot -> %d\n", m_fxSlot + 1);
                    toast("FX SLOT", m_fxSlot ? "2" : "1");
                } else fxBypassKey();
            }
            break;
        case 4: // SAMPLE: REC/STOP/NORM/ASSIGN
            if (i == 0)      sampleRecToggle();
            else if (i == 1) sampleStop();
            else if (i == 2) sampleNormalize();
            else if (i == 3) sampleAssign();
            break;
        case 5: // LOAD: S1 = enter/load, S2 = up
            if (i == 0)      browserActivate();
            else if (i == 1) browserUp();
            break;
        case 6: // SET: S1 = adjust, S2 = save
            if (i == 0)      settingsAdjust(+1);
            else if (i == 1) saveSettings();
            break;
        case 7: // MFX: S1 = chooser, S2 = bypass
            if (i == 0)      chooserKey(2);
            else if (i == 1) mfxBypassKey();
            break;
        case 8: // TRACK: S1 = type, S2 = monitor, S3 = bounce
            if (i == 0)      trackTypeToggle();
            else if (i == 1) trackMonitorToggle();
            else if (i == 2) bounceTrack(m_state.track);
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
            // cycle order table (SEQ/INST/MFX/FX/MIX/SAMPLE/LOAD/SET/TRACK)
            int pos = 0;
            for (int i = 0; i < kNumPages; ++i)
                if (kPageOrder[i] == m_state.page) { pos = i; break; }
            pos = (pos + dir + kNumPages) % kNumPages;
            setPage(kPageOrder[pos]);
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

    // Held-step edit (rev B): pitch comes from a piano tap (p-lock);
    // encoders while held adjust per-step GATE (50..100%) on melodic
    // tracks, or cycle the step's drum VOICE on drum tracks (drums are
    // one-shots — a gate edit is meaningless there).
    // Absolute step = window + held key index.
    void editHeldStep(int enc, int delta) {
        (void)enc; // ENC1 = gate/voice (any encoder drives it while held)
        const int t = m_state.track;
        const int step = m_window + m_heldStep;
        if (isDrumTrack(t)) {
            auto& s = m_pattern.steps[t][step];
            m_heldEdited = true;
            const int count = int(m_drumVoiceNotes[t].size());
            if (count == 0) return;
            const int idx = ((int(s.noteOffset) + delta) % count + count) % count;
            s.noteOffset = int8_t(idx);
            setEditPad(t, idx, false); // hold-step voice edit owns the pad
            char vb[16];
            toast("VOICE", drumVoiceName(t, idx, vb, sizeof(vb)));
            std::printf("[seq] T%d step %02d voice %s (note %d)\n",
                        t + 1, step, vb, drumVoiceNote(t, idx));
            return;
        }
        auto& s = m_pattern.steps[0][step];
        m_heldEdited = true;
        int g = s.gate > 0 ? s.gate
                           : int(m_pattern.t1Gate * 100.0f + 0.5f);
        g = std::max(50, std::min(100, g + delta * 5));
        s.gate = uint8_t(g);
        char vb[8];
        std::snprintf(vb, sizeof(vb), "%d%%", g);
        toast("GATE", vb);
        std::printf("[seq] T1 step %02d gate = %d%%\n", step, g);
    }

    // Pattern length edit (SEQ S1 = LEN; any key exits).
    void adjustLength(int delta) {
        m_pattern.length = std::max(1, std::min(gb::Pattern::kMaxSteps,
                                                m_pattern.length + delta));
        const int maxWin = (m_pattern.length - 1) / m_winSize * m_winSize;
        if (m_window > maxWin) m_window = maxWin;
        char vb[8];
        std::snprintf(vb, sizeof(vb), "%03d", m_pattern.length);
        toast("LEN", vb);
        std::printf("[seq] LEN = %d\n", m_pattern.length);
    }

    // Window paging (SEQ page, shift+</>): manual moves by one window
    // (16 steps on NSR-1, 8 on NSR-2); landing on the playhead's page
    // re-engages follow mode.
    void pageWindow(int dir) {
        const int pages = (m_pattern.length + m_winSize - 1) / m_winSize;
        int pg = m_window / m_winSize + dir;
        pg = std::max(0, std::min(pages - 1, pg));
        m_window = pg * m_winSize;
        m_follow = (m_playhead >= 0 && m_playhead / m_winSize == pg);
        char vb[8];
        std::snprintf(vb, sizeof(vb), "%d/%d", pg + 1, pages);
        toast("PAGE", vb);
        std::printf("[seq] window -> steps %d-%d (page %d/%d)%s\n",
                    m_window, m_window + m_winSize - 1, pg + 1, pages,
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

    // Low-priority toast (pot/encoder param feedback): suppressed
    // while a hold-step pitch/gate edit owns the toast.
    void toastLow(const char* label, const char* value) {
        if (m_heldStep >= 0) return;
        toast(label, value);
    }

    // ── Tracks ──────────────────────────────────────────────────────
    void selectTrack(int t) {
        m_state.track = t;
        m_state.pageName = (t == 0) ? "SYNTH" : "DRUMS";
        // DrumRack pad params (attack/decay etc.) target the edit pad —
        // the last-played voice (defaults to the lane pad at boot).
        if (t > 0)
            if (auto* rack = dynamic_cast<yawn::instruments::DrumRack*>(
                    m_engine->instrument(m_state.track)))
                rack->setSelectedPad(drumVoiceNote(t, m_editPad[t]));
        rebuildDrumVoices(t); // cheap catch-all (pad/param changes)
        rebindParams(); // name-lookup bindings + pickup re-arm
        std::printf("[panel] track select -> T%d (%s)\n", t + 1,
                    t == 0 ? "SubtractiveSynth"
                    : t == 1 ? "DrumRack kick"
                    : t == 2 ? "DrumRack hat" : "DrumRack clap");
    }

    void muteTrack(int t) {
        m_muted[t] = !m_muted[t];
        // M2d: one engine track per UI track — mute goes through the
        // engine for all tracks; the scheduler also skips firing so
        // the log/test path stays deterministic.
        m_engine->sendCommand(yawn::audio::SetTrackMuteMsg{
            t, m_muted[t]});
        std::printf("[panel] T%d %s\n", t + 1,
                    m_muted[t] ? "MUTED" : "unmuted");
    }

    void toggleMode() {
        if (m_nsr2) { // NSR-2: PLAY -> STEP -> TEXT -> PLAY
            m_state.mode = (m_state.mode + 1) % 3;
            static const char* kModes[3] = {"PLAY", "SEQ", "TEXT"};
            std::printf("[panel] MODE -> %s (keyboard row)\n",
                        kModes[m_state.mode]);
        } else {
            // NSR-1 rev B: MODE toggles keyboard scale-lock
            m_scaleLock = (m_scaleLock == 2) ? 0 : 2; // chromatic <-> major
            toast("SCALE", scaleName());
            std::printf("[panel] MODE -> scale-lock %s\n", scaleName());
        }
    }

    // ── NSR-2 grid (0..31) ──────────────────────────────────────────
    void onGridKey(int index, bool pressed) {
        const int row = index >> 3, col = index & 7;
        if (shiftHeld()) {
            // shift + grid 1-4 = track select, 5-8 = mute (NSR-1 rule)
            if (pressed && row == 0) {
                if (col <= 3) selectTrack(col);
                else          muteTrack(col - 4);
            }
            return;
        }
        switch (m_state.mode) {
        case 1:  gridStepKey(index, pressed, row, col); break;
        case 2:  if (pressed) textType(textKeyChar(index)); break;
        default: gridPlayKey(index, pressed, row, col); break;
        }
    }

    // Step mode: grid = 4 tracks x 8-step window into the pattern.
    void gridStepKey(int index, bool pressed, int row, int col) {
        (void)index;
        const int step = m_window + col;
        if (step >= m_pattern.length) return;
        // hold-to-edit gesture, same as NSR-1 white keys
        if (pressed) {
            if (m_heldStep == col && m_heldRow == row) return; // storm
            m_heldStep = col;
            m_heldRow = row;
            m_heldEdited = false;
        } else {
            if (m_heldStep == col && m_heldRow == row) {
                if (!m_heldEdited) toggleStep(row, step);
                m_heldStep = -1;
                m_heldRow = -1;
            }
        }
    }

    // Play mode: scale-locked isomorphic grid.
    // note = base C3 + col + 4 * rowFromBottom (row up = +4 semitones)
    static bool inScale(int pc, int scale) {
        // scale: 0=major 1=minor 2=chromatic (C-based pitch-class masks)
        static const uint16_t kMasks[2] = {0b101011010101,
                                           0b101101010101};
        return scale == 2 || ((kMasks[scale] >> (pc % 12)) & 1u);
    }
    static int snapToScale(int note, int scale) {
        if (inScale(note % 12, scale)) return note;
        for (int d = 1; d <= 6; ++d) {
            if (inScale((note + d) % 12, scale)) return note + d; // up first
            if (inScale((note - d) % 12, scale)) return note - d;
        }
        return note;
    }

    void gridPlayKey(int index, bool pressed, int row, int col) {
        const int rowFromBottom = 3 - row;
        if (isDrumTrack(m_state.track)) {
            // Drum track: the 4x8 grid is one bottom-up strip of 32
            // positions walking the voice list (with wraparound).
            const int vidx = col + 8 * rowFromBottom;
            const int note = drumVoiceNote(m_state.track, vidx);
            if (pressed) setEditPad(m_state.track, vidx, true);
            playNote(m_state.track, note, pressed, index);
            return;
        }
        const int raw = 48 + col + 4 * rowFromBottom; // base C3
        const int note = snapToScale(raw, m_scaleLock);
        if (pressed) m_lastGridNote = note;
        if (pressed && note != raw) {
            char nb[8];
            std::printf("[panel] scale-lock: raw %d -> %s\n", raw,
                        noteName(note, nb, sizeof(nb)));
        }
        playNote(m_state.track, note, pressed, index);
    }

    // note on/off with per-key tracking (chords + LEDs)
    void playNote(int track, int note, bool pressed, int ledKey) {
        sendNote(track, note, pressed, pressed ? velocity7() : 0);
        if (ledKey >= 0) m_gridNoteHeld[ledKey] = pressed;
        char nb[8];
        std::printf("[engine] NOTE %-3s T%d %s(%d) vel%d\n",
                    pressed ? "ON " : "OFF", track + 1,
                    noteName(note, nb, sizeof(nb)), note,
                    pressed ? velocity7() : 0);
        std::fflush(stdout);
    }

    // ── NSR-2 text-entry mode ───────────────────────────────────────
    // Grid rows: QWERTYUI / ASDFGHJK / ZXCVBNM. / punct+enter+bksp
    static char textKeyChar(int index) {
        static const char* const kRows[4] = {
            "QWERTYUI", "ASDFGHJK", "ZXCVBNM.", ",/-?!\x27"  ""};
        if (index < 23) return kRows[index >> 3][index & 7];
        switch (index) {
        case 29: return '\b';
        case 30: return 0;    // (unused slot)
        case 31: return '\n'; // enter
        default: return kRows[3][index - 24];
        }
    }

    void textType(char c) {
        if (c == '\n') { // enter: done -> back to step mode
            m_state.mode = 1;
            std::printf("[text] entry done: \"%s\"\n", m_textBuf);
            return;
        }
        if (c == '\b') {
            if (m_textLen > 0) m_textBuf[--m_textLen] = 0;
        } else if (c && m_textLen < int(sizeof(m_textBuf)) - 1) {
            m_textBuf[m_textLen++] = c;
            m_textBuf[m_textLen] = 0;
        }
        m_state.textBuf = m_textBuf;
        m_state.textCursor = m_textLen;
        std::printf("[text] \"%s\"\n", m_textBuf);
    }

    // ── Transport ───────────────────────────────────────────────────
    void togglePlay() {
        if (m_engine->transport().isPlaying()) {
            m_engine->sendCommand(yawn::audio::TransportStopMsg{});
            allNotesOff();
            recordArmAudioStop(); // finalize a clip recording
            std::printf("[engine] transport STOP\n");
        } else {
            launchPendingClips(); // before PLAY so clips start on time
            m_engine->sendCommand(yawn::audio::TransportPlayMsg{});
            recordArmAudioStart(); // REC-armed AUDIO track captures input
            std::printf("[engine] transport PLAY\n");
        }
    }

    void stopTransport() {
        m_engine->sendCommand(yawn::audio::TransportStopMsg{});
        m_engine->sendCommand(yawn::audio::TransportSetPositionMsg{0});
        allNotesOff();
        recordArmAudioStop();
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

    // ── Drum voice mapping ──────────────────────────────────────────
    // Drum instruments (drumsynth/drumrack/drumslop) do NOT respond to
    // chromatic notes — they trigger per-voice notes (DrumSynth: GM
    // kit notes 36..54; DrumRack: loaded pad notes; DrumSlop: 16 slice
    // pads at baseNote+i). On a drum track the 27 piano keys therefore
    // map to the instrument's VOICE LIST instead of semitones:
    //
    //   * the key row is treated as one physical left→right strip
    //     (blacks interleave between whites by their panel position);
    //   * key at physical position p plays voice (p % voiceCount) —
    //     adjacent keys walk adjacent voices and past the last voice
    //     the list WRAPS, so every one of the 27 keys sounds;
    //   * velocity is unchanged (data slider / fixed / host).
    //
    // The voice list is rebuilt from the actual instrument state on
    // boot, instrument swap, pad load and track select (selectTrack
    // rebuild is the cheap catch-all for param changes like DrumSlop
    // base note / slice count).
    static bool isDrumInstrument(const char* id) {
        return std::strcmp(id, "drumsynth") == 0 ||
               std::strcmp(id, "drumrack") == 0 ||
               std::strcmp(id, "drumslop") == 0;
    }
    bool isDrumTrack(int t) const {
        return t >= 0 && t < 4 && isDrumInstrument(m_trackInstId[t].c_str());
    }

    // Physical left→right position (0..26) of a piano key index
    // (whites 0..15, blacks 16..26 interleaved by semitone offset).
    static int pianoPhysPos(int keyIndex) {
        const int off = keyIndex <= 15 ? kWhiteOffsets[keyIndex]
                                       : kBlackOffsets[keyIndex - 16];
        int pos = 0;
        for (int w : kWhiteOffsets) if (w < off) ++pos;
        for (int b : kBlackOffsets) if (b < off) ++pos;
        return pos;
    }

    void rebuildDrumVoices(int t) {
        auto& v = m_drumVoiceNotes[t];
        v.clear();
        const std::string& id = m_trackInstId[t];
        if (id == "drumsynth") {
            for (int n : yawn::instruments::DrumSynth::kDrumNotes)
                v.push_back(n);
        } else if (id == "drumrack") {
            if (auto* r = dynamic_cast<yawn::instruments::DrumRack*>(
                    m_engine->instrument(gb::Pattern::engineTrack(t))))
                for (int n = 0; n < yawn::instruments::DrumRack::kNumPads; ++n)
                    if (r->hasSample(n)) v.push_back(n);
            if (v.empty()) // pad not loaded yet — fall back to the lane
                v.push_back(gb::Pattern::kPadNote[t]);
        } else if (id == "drumslop") {
            int base = 36, count = 8; // DrumSlop defaults
            if (auto* inst = m_engine->instrument(gb::Pattern::engineTrack(t))) {
                base = int(inst->getParameter(yawn::instruments::DrumSlop::kBaseNote));
                count = int(inst->getParameter(yawn::instruments::DrumSlop::kSliceCount));
            }
            for (int i = 0; i < count; ++i) v.push_back(base + i);
        }
        // Edit pad can outlive a smaller voice list (instrument swap) —
        // clamp it so binding math never goes out of range.
        if (!v.empty() && m_editPad[t] >= int(v.size())) m_editPad[t] = 0;
    }

    // DrumSynth exposes NO selected-slot setter: its 36 params are
    // globally indexed per voice (DrumSynth.h) — kick = 7 params at
    // index 0 (Tune/Atk/Dec/Sine/White/Pink/Drive), every other voice
    // = 4 params (Tune/Atk/Dec/Drive) at 7 + (slot-1)*4, +1 global
    // (OS 2x at 35). The edit pad is therefore pure index arithmetic.
    static int drumSynthBase(int slot) {
        return slot == 0 ? 0 : 7 + (slot - 1) * 4;
    }

    // Last-played-pad tracking: make `vidx` the edit pad on drum track
    // `t`. DrumRack/DrumSlop get it via their selectedPad setter (note
    // number / pad index); DrumSynth needs nothing (see above). When
    // `t` is the selected track the INST bindings are rebuilt to the
    // new pad and a loud "EDIT <voice>" toast confirms the switch.
    void setEditPad(int t, int vidx, bool loud) {
        if (!isDrumTrack(t) || m_drumVoiceNotes[t].empty()) return;
        const int n = int(m_drumVoiceNotes[t].size());
        vidx = ((vidx % n) + n) % n;
        if (vidx == m_editPad[t]) return;
        m_editPad[t] = vidx;
        const int note = m_drumVoiceNotes[t][vidx];
        auto* inst = m_engine->instrument(gb::Pattern::engineTrack(t));
        if (m_trackInstId[t] == "drumrack") {
            if (auto* r = dynamic_cast<yawn::instruments::DrumRack*>(inst))
                r->setSelectedPad(note);
        } else if (m_trackInstId[t] == "drumslop") {
            if (auto* s = dynamic_cast<yawn::instruments::DrumSlop*>(inst))
                s->setSelectedPad(vidx);
        }
        char vb[16];
        drumVoiceName(t, vidx, vb, sizeof(vb));
        if (loud)
            std::printf("[panel] EDIT pad T%d -> %s (note %d)\n",
                        t + 1, vb, note);
        if (t == m_state.track) {
            rebindParams(); // pots/ADSR/pages follow the edit pad
            if (loud) toast("EDIT", vb);
        }
    }

    // Voice note with wraparound — ANY index maps to a real voice.
    int drumVoiceNote(int t, int voiceIdx) const {
        const auto& v = m_drumVoiceNotes[t];
        if (v.empty()) return m_pattern.noteForTrack(t);
        const int n = int(v.size());
        return v[((voiceIdx % n) + n) % n];
    }
    // Reverse lookup for REC quantization; -1 when not a voice note.
    int drumVoiceIndexForNote(int t, int note) const {
        const auto& v = m_drumVoiceNotes[t];
        for (int i = 0; i < int(v.size()); ++i) if (v[i] == note) return i;
        return -1;
    }
    // Short display name for toasts/logs. DrumSynth has no name API —
    // names follow the DrumSlot order documented in DrumSynth.h.
    const char* drumVoiceName(int t, int voiceIdx, char* buf, size_t n) const {
        if (m_trackInstId[t] == "drumsynth") {
            static const char* kNames[yawn::instruments::DrumSynth::kNumDrums] =
                {"KICK", "SNARE", "CLAP", "TOM1", "CHH", "OHH", "TOM2", "TAMB"};
            const int i = ((voiceIdx % 8) + 8) % 8;
            std::snprintf(buf, n, "%s", kNames[i]);
        } else if (m_trackInstId[t] == "drumslop") {
            std::snprintf(buf, n, "SL%d", voiceIdx + 1);
        } else {
            std::snprintf(buf, n, "PAD%d", drumVoiceNote(t, voiceIdx));
        }
        return buf;
    }

    // MIDI note a pattern step fires. On drum tracks Step::noteOffset
    // is repurposed as a VOICE INDEX (0 = first voice = kick); on
    // melodic tracks it stays a semitone offset from the track note.
    int stepNote(int t, int step) const {
        if (!isDrumTrack(t)) return m_pattern.noteForStep(t, step);
        return drumVoiceNote(t, m_pattern.steps[t][step].noteOffset);
    }

    // ── NSR-1 rev B: piano row (always playable, scale-lockable) ────
    void pianoKey(int keyIndex, bool pressed) {
        const int t = m_state.track;
        if (isDrumTrack(t)) {
            // Drum track: physical strip position -> voice (wraps).
            const int vidx = pianoPhysPos(keyIndex);
            const int note = drumVoiceNote(t, vidx);
            // last-played pad becomes the INST edit pad (manual play)
            if (pressed) setEditPad(t, vidx, true);
            // hold-step + piano tap = p-lock the step's drum voice
            if (pressed && m_heldStep >= 0) {
                const int step = m_window + m_heldStep;
                m_pattern.steps[t][step].noteOffset = int8_t(vidx);
                m_heldEdited = true;
                char vb[16];
                toast("VOICE", drumVoiceName(t, vidx, vb, sizeof(vb)));
                std::printf("[seq] T%d step %02d voice %s (%d)\n",
                            t + 1, step, vb, note);
            }
            playKey(keyIndex, pressed, note);
            return;
        }
        const int raw = (t == 0)
            ? m_baseNote + (keyIndex <= 15 ? kWhiteOffsets[keyIndex]
                                           : kBlackOffsets[keyIndex - 16])
            : m_pattern.noteForTrack(t);
        const int note = snapToScale(raw, m_scaleLock);
        // p-lock: held step + piano tap = set that step's pitch (T1)
        if (pressed && m_heldStep >= 0 && t == 0) {
            const int step = m_window + m_heldStep;
            auto& s = m_pattern.steps[0][step];
            s.noteOffset = int8_t(note - m_pattern.t1Note);
            m_heldEdited = true;
            char nb[8];
            toast("NOTE", noteName(note, nb, sizeof(nb)));
            std::printf("[seq] T1 step %02d p-lock %s(%d)\n", step, nb,
                        note);
        }
        if (pressed && note != raw) {
            char nb[8];
            std::printf("[panel] scale-lock: raw %d -> %s\n", raw,
                        noteName(note, nb, sizeof(nb)));
        }
        playKey(keyIndex, pressed, note);
    }

    // ── NSR-1 rev B: step row (always the sequencer) ────────────────
    void onStepRowKey(int i, bool pressed) {
        if (shiftHeld()) {
            // shift+step 1-4 = select track, 5-8 = mute, 9-16 = accent
            if (!pressed) return;
            if (i <= 3)      selectTrack(i);
            else if (i <= 7) muteTrack(i - 4);
            else             toggleAccent(m_window + i);
            return;
        }
        const int step = m_window + i;
        if (step >= m_pattern.length) return;
        // hold-to-edit: release toggles only if nothing was edited
        if (pressed) {
            if (m_heldStep == i) return; // repeat-storm guard
            m_heldStep = i;
            m_heldRow = -1;
            m_heldEdited = false;
        } else {
            if (m_heldStep == i) {
                if (!m_heldEdited) toggleStep(m_state.track, step);
                m_heldStep = -1;
            }
        }
    }

    // ── Play mode ───────────────────────────────────────────────────
    void playKey(int keyIndex, bool pressed, int noteOverride = -1) {
        const int t = m_state.track;
        const int note = noteOverride >= 0 ? noteOverride : (t == 0)
            ? m_baseNote + (keyIndex <= 15 ? kWhiteOffsets[keyIndex]
                                           : kBlackOffsets[keyIndex - 16])
            : m_pattern.noteForTrack(t);
        // white-key LEDs (rev B ch2 notes 16-31) mirror held notes
        if (keyIndex <= 15) m_gridNoteHeld[keyIndex] = pressed;
        if (pressed) m_lastGridNote = note;
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
                if (isDrumTrack(t)) { // keep the played drum voice
                    const int vi = drumVoiceIndexForNote(t, note);
                    s.noteOffset = int8_t(vi >= 0 ? vi : 0);
                } else if (t == 0) // keep the played pitch on T1
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
        if (on) { ++m_probeNotes; m_lastNoteOn = note; } // probe/drumtest
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
        const int note = stepNote(t, step);
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
        // Follow mode: the window tracks the playhead's page.
        if (m_follow && m_playhead >= 0)
            m_window = (m_playhead / m_winSize) * m_winSize;
        const int sel = m_state.track;
        const int len = m_pattern.length;
        // playhead column within the window (-1 if on another page)
        const int ph = (m_playhead >= m_window &&
                        m_playhead < m_window + m_winSize)
                           ? m_playhead - m_window : -1;
        m_state.seq.playhead = ph;
        m_state.seq.rowMuted = 0;
        m_state.seqLength = len;
        m_state.seqWindow = m_window;
        m_state.lenEdit = m_lenEdit;
        for (int t = 0; t < 4; ++t) {
            if (m_muted[t]) m_state.seq.rowMuted |= uint8_t(1u << t);
            for (int s = 0; s < 16; ++s) {
                // NSR-2's 8-step window occupies columns 0..7 only
                if (s >= m_winSize) {
                    m_state.seq.cells[t][s] = gb::kStepEmpty;
                    continue;
                }
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
        m_state.scaleLock = m_scaleLock;
        for (int s = 0; s < 16; ++s) {
            const int idx = m_window + s;
            const auto& st = m_pattern.steps[sel][idx];
            m_state.lane.values[s] =
                (s < m_winSize && idx < len && st.on) ? st.vel : 0;
        }
        m_state.lane.current = ph;
        for (int i = 0; i < 4; ++i) m_state.mixer.mute[i] = m_muted[i];
        syncLeds(ph);
    }

    // LEDs: NSR-1 lights the 16 white-key LEDs (selected track's window
    // steps, playhead inverts). NSR-2 lights the 32 grid LEDs: step
    // mode = all 4 tracks' window steps (grid mirrors the SEQ page),
    // play mode = held notes, text mode = enter/backspace markers.
    void syncLeds(int ph) {
        if (!m_nsr2) {
            // NSR-1 rev B: LEDs 0-15 = step row (selected track's window
            // steps, playhead inverts); LEDs 16-31 = white keys (held
            // notes).
            const int len = m_pattern.length;
            const int sel = m_state.track;
            for (int i = 0; i < 16; ++i) {
                const int idx = m_window + i;
                const bool on = idx < len && m_pattern.steps[sel][idx].on;
                const bool v = on != (i == ph);
                m_state.leds[i] = v;
                m_hal.setLed(i, v);
                m_hal.setLed(16 + i, m_gridNoteHeld[i]);
            }
            return;
        }
        for (int i = 0; i < gb::kN2NumLeds; ++i) {
            bool v = false;
            if (m_state.mode == 1) {
                const int idx = m_window + (i & 7);
                v = idx < m_pattern.length && m_pattern.steps[i >> 3][idx].on;
                if ((i & 7) == ph) v = !v; // playhead inverts
            } else if (m_state.mode == 0) {
                v = m_gridNoteHeld[i];
            } else {
                v = (i == 29 || i == 31); // bksp + enter markers
            }
            if (i < 16) m_state.leds[i] = v;
            m_hal.setLed(i, v);
        }
    }

    // ── Parameter binding (name-lookup based, engine-change-proof) ──
    static bool containsCI(const char* hay, const char* needle) {
        auto lower = [](char c) {
            return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
        };
        for (const char* h = hay; *h; ++h) {
            const char* n = needle;
            while (*n && h[n - needle] && lower(h[n - needle]) == lower(*n))
                ++n;
            if (!*n) return true;
        }
        return false;
    }

    yawn::instruments::Instrument* selInstrument() {
        return m_engine->instrument(gb::Pattern::engineTrack(m_state.track));
    }

    // Find a param index on the selected track's instrument by
    // case-insensitive name substring ("" = match nothing). -1 = unbound.
    int findParam(const char* nameSub) {
        auto* inst = selInstrument();
        if (!inst || !nameSub || !*nameSub) return -1;
        for (int i = 0; i < inst->parameterCount(); ++i)
            if (containsCI(inst->parameterInfo(i).name, nameSub)) return i;
        return -1;
    }

    // Normalized 0..1 access via the selected track's ParameterInfo.
    float getNormParam(int pidx) {
        auto* inst = selInstrument();
        if (!inst || pidx < 0) return 0.0f;
        const auto& pi = inst->parameterInfo(pidx);
        const float range = pi.maxValue - pi.minValue;
        if (range == 0.0f) return 0.0f;
        return std::max(0.0f, std::min(1.0f,
            (inst->getParameter(pidx) - pi.minValue) / range));
    }

    void setNormParam(int pidx, float v) {
        auto* inst = selInstrument();
        if (!inst || pidx < 0) return;
        const auto& pi = inst->parameterInfo(pidx);
        v = std::max(0.0f, std::min(1.0f, v));
        inst->setParameter(pidx, pi.minValue + v * (pi.maxValue - pi.minValue));
    }

    static void formatParam(const yawn::ParameterInfo& pi, float norm,
                            char* buf, int n) {
        const float v = pi.minValue + norm * (pi.maxValue - pi.minValue);
        if (pi.formatFn) { pi.formatFn(v, buf, n); return; }
        if (pi.isBoolean) {
            std::snprintf(buf, n, "%s", norm >= 0.5f ? "ON" : "OFF");
            return;
        }
        if (pi.valueLabels && pi.valueLabelCount > 0) {
            const int li = std::max(0, std::min(pi.valueLabelCount - 1,
                int(norm * pi.valueLabelCount)));
            std::snprintf(buf, n, "%s", pi.valueLabels[li]);
            return;
        }
        std::snprintf(buf, n, "%.3g%s", double(v), pi.unit);
    }

    // Pots (CUT RES) bind by name on the selected track. Rev B2: amp
    // A/D/S/R moved to dedicated encoders 5-8 (relative, no pickup).
    static constexpr const char* kPotNames[2] = {"cutoff", "reso"};
    static constexpr const char* kPotLabel[2] = {"CUT", "RES"};
    // Dedicated amp-ADSR encoders (HAL encoders 4..7).
    static constexpr const char* kAdsrNames[4] =
        {"attack", "decay", "sustain", "release"};
    static constexpr const char* kAdsrLabel[4] = {"A", "D", "S", "R"};

    // ── Encoder param pages (built dynamically, pot-owned excluded) ──
    // Design rule: each parameter belongs to exactly ONE control. The
    // pots own filter cutoff/resonance; dedicated encoders 5-8 own amp
    // A/D/S/R — on drum tracks that's the EDIT PAD's attack/decay (the
    // last-played pad, see setEditPad). Encoder pages are built from
    // the instrument's parameterInfo MINUS those, partitioned:
    //   melodic: OSC (osc/wave/sub level) / MOD (lfo/env/noise/filter
    //            type) / MISC (everything else)
    //   drums:   PAD/PAD2 (edit pad's params) / KIT (globals)
    // Pages with <4 params show "---" slots; empty pages are skipped
    // when cycling (S2 = PG+).
    struct EncPage { int count = 0; int param[4] = {-1, -1, -1, -1}; };
    static constexpr const char* kEncPageTitles[3] = {"OSC", "MOD", "MISC"};

    static bool nameHasAny(const char* name, const char* const* kws,
                           int n) {
        for (int i = 0; i < n; ++i)
            if (containsCI(name, kws[i])) return true;
        return false;
    }
    // Pot ownership is INDEX-precise: dedicated controls (2 pots + 4
    // ADSR encoders) own the params they are bound to; pageable
    // encoders never see them. "Filt Attack" is NOT the amp-A "Amp
    // Attack".
    bool isDedicatedIndex(int pidx) const {
        for (int i = 0; i < 2; ++i)
            if (m_potParam[i] == pidx) return true;
        for (int i = 0; i < 4; ++i)
            if (m_adsrParam[i] == pidx) return true;
        return false;
    }

    void rebindEncoders() {
        static const char* kOscKw[] = {"osc", "wave", "sub level"};
        static const char* kModKw[] = {"lfo", "env", "noise", "filter type"};
        for (auto& pg : m_encPages) pg = EncPage{};
        auto claim = [&](int section, int pidx) {
            if (m_encPages[section].count < 4)
                m_encPages[section].param[m_encPages[section].count++] =
                    pidx;
        };
        if (isDrumTrack(m_state.track)) {
            // Drum pages: PAD/PAD2 = the EDIT PAD's params (dedicated
            // A/D/S/R excluded — the macros own those), KIT = global.
            const std::string& id = m_trackInstId[m_state.track];
            const int slot = m_editPad[m_state.track];
            if (id == "drumsynth") {
                const int b = drumSynthBase(slot);
                claim(0, b + 0); // Tune
                if (slot == 0) { // kick adds the noise-mix trio + Drive
                    claim(0, b + 3); claim(0, b + 4); claim(0, b + 5);
                    claim(1, b + 6);
                } else {
                    claim(0, b + 3); // Drive
                }
                claim(2, yawn::instruments::DrumSynth::pOversample);
            } else if (id == "drumrack") {
                namespace dr = yawn::instruments;
                claim(0, dr::DrumRack::kPadVolume);
                claim(0, dr::DrumRack::kPadPan);
                claim(0, dr::DrumRack::kPadPitch);
                claim(0, dr::DrumRack::kPadChoke);
                claim(1, dr::DrumRack::kPadStart);
                claim(1, dr::DrumRack::kPadEnd);
                claim(2, dr::DrumRack::kVolume);
            } else { // drumslop
                namespace dr = yawn::instruments;
                claim(0, dr::DrumSlop::kPadVolume);
                claim(0, dr::DrumSlop::kPadPan);
                claim(0, dr::DrumSlop::kPadPitch);
                claim(0, dr::DrumSlop::kPadReverse);
                claim(1, dr::DrumSlop::kPadFilterCutoff);
                claim(1, dr::DrumSlop::kPadFilterReso);
                claim(2, dr::DrumSlop::kVolume);
                claim(2, dr::DrumSlop::kSliceCount);
                claim(2, dr::DrumSlop::kSliceMode);
                claim(2, dr::DrumSlop::kOriginalBPM);
                // (Base Note + Swing overflow the 4-slot KIT page —
                //  Base Note is reachable via track reselect rebuild.)
            }
        } else if (auto* inst = selInstrument()) {
            const int n = inst->parameterCount();
            for (int i = 0; i < n; ++i) {
                const char* nm = inst->parameterInfo(i).name;
                if (!isDedicatedIndex(i) && nameHasAny(nm, kOscKw, 3))
                    claim(0, i);
            }
            for (int i = 0; i < n; ++i) {
                const char* nm = inst->parameterInfo(i).name;
                if (!isDedicatedIndex(i) && !nameHasAny(nm, kOscKw, 3) &&
                    nameHasAny(nm, kModKw, 4))
                    claim(1, i);
            }
            for (int i = 0; i < n; ++i) {
                const char* nm = inst->parameterInfo(i).name;
                if (!isDedicatedIndex(i) && !nameHasAny(nm, kOscKw, 3) &&
                    !nameHasAny(nm, kModKw, 4))
                    claim(2, i);
            }
        }
        if (m_encPages[m_paramPage].count == 0) nextEncPage(+1);
        else applyEncPage();
    }

    // Encoder page titles: PAD/PAD2/KIT on drum tracks, OSC/MOD/MISC
    // on melodic instruments.
    const char* encPageTitle(int p) const {
        if (isDrumTrack(m_state.track)) {
            static constexpr const char* kDrumTitles[3] =
                {"PAD", "PAD2", "KIT"};
            return kDrumTitles[p];
        }
        return kEncPageTitles[p];
    }

    void applyEncPage() {
        for (int i = 0; i < 4; ++i)
            m_encParam[i] = i < m_encPages[m_paramPage].count
                                ? m_encPages[m_paramPage].param[i] : -1;
    }

    void nextEncPage(int dir) {
        for (int tries = 0; tries < 3; ++tries) {
            m_paramPage = (m_paramPage + dir + 3) % 3;
            if (m_encPages[m_paramPage].count > 0) break;
        }
        applyEncPage();
        std::printf("[panel] param page -> %s\n", encPageTitle(m_paramPage));
    }

    void rebindParams() {
        for (int i = 0; i < 2; ++i) m_potParam[i] = findParam(kPotNames[i]);
        for (int i = 0; i < 2; ++i) m_potEngaged[i] = false;
        if (isDrumTrack(m_state.track)) {
            // Drum tracks: ADSR macros own the EDIT PAD's attack/decay
            // (last-played pad — setEditPad rebuilds these). Bound by
            // INDEX, not name: DrumSynth repeats "Atk"/"Dec" per voice
            // and DrumSlop prefixes "Pad ", so findParam can't work.
            // S/R stay inert except on DrumSlop (the only one that has
            // them). Pots keep their name bindings (CUT/RES exist only
            // on DrumSlop pads; inert on DrumSynth/DrumRack).
            const std::string& id = m_trackInstId[m_state.track];
            if (id == "drumsynth") {
                const int b = drumSynthBase(m_editPad[m_state.track]);
                m_adsrParam[0] = b + 1; // voice Atk
                m_adsrParam[1] = b + 2; // voice Dec
                m_adsrParam[2] = m_adsrParam[3] = -1;
            } else if (id == "drumrack") {
                m_adsrParam[0] = yawn::instruments::DrumRack::kPadAttack;
                m_adsrParam[1] = yawn::instruments::DrumRack::kPadDecay;
                m_adsrParam[2] = m_adsrParam[3] = -1;
            } else { // drumslop
                m_adsrParam[0] = yawn::instruments::DrumSlop::kPadAttack;
                m_adsrParam[1] = yawn::instruments::DrumSlop::kPadDecay;
                m_adsrParam[2] = yawn::instruments::DrumSlop::kPadSustain;
                m_adsrParam[3] = yawn::instruments::DrumSlop::kPadRelease;
            }
        } else {
            for (int i = 0; i < 4; ++i)
                m_adsrParam[i] = findParam(kAdsrNames[i]);
        }
        // macros: per-track assignment, factory = ADSR
        for (int i = 0; i < 4; ++i) {
            const int assigned = m_macroParamByTrack[m_state.track][i];
            m_macroParam[i] = assigned >= 0 ? assigned : m_adsrParam[i];
            updateMacroLabel(i);
        }
        rebindEncoders();
    }

    // ── Macro encoders (HAL 4..7) ───────────────────────────────────
    // Factory default: amp A/D/S/R of the selected track. Assignable:
    // shift+push enters ASSIGN for that macro; the next param control
    // touched (pageable encoder 1-4 on SYNTH, or a pot) binds its
    // parameter; push again or 3 s timeout cancels; S1 while assigning
    // resets to the factory ADSR binding. Bindings persist per track in
    // settings.json. Edits are relative (1/64 detent, shift 1/256).
    void editMacro(int slot, int delta) {
        const int p = m_macroParam[slot];
        if (p < 0) return; // inert (unbound, e.g. S/R on drum lanes)
        auto* inst = selInstrument();
        const auto& pi = inst->parameterInfo(p);
        const float step = shiftHeld() ? delta / 256.0f : delta / 64.0f;
        const float nv = std::max(0.0f, std::min(1.0f,
            getNormParam(p) + step));
        setNormParam(p, nv);
        char vb[16];
        formatParam(pi, nv, vb, sizeof(vb));
        toastLow(pi.name, vb);
        std::printf("[panel] ENC%d %s = %s\n", slot + 5, pi.name, vb);
    }

    void macroPushAssign(int slot) {
        if (m_assignMacro == slot) { // push again = cancel
            m_assignMacro = -1;
            toast("MACRO", "CANCEL");
            std::printf("[panel] macro %d assign cancelled\n", slot + 1);
            return;
        }
        m_assignMacro = slot;
        m_assignTime = std::chrono::steady_clock::now();
        char vb[8];
        std::snprintf(vb, sizeof(vb), "M%d", slot + 1);
        toast("ASSIGN", vb);
        std::printf("[panel] macro %d: ASSIGN — touch a param control "
                    "(S1 = reset to ADSR)\n", slot + 1);
    }

    void macroBind(int slot, int pidx) {
        auto* inst = selInstrument();
        m_macroParamByTrack[m_state.track][slot] = pidx;
        m_macroParam[slot] = pidx;
        m_assignMacro = -1;
        updateMacroLabel(slot);
        const char* nm = (inst && pidx >= 0) ? inst->parameterInfo(pidx).name
                                             : "?";
        toast("MACRO", nm);
        std::printf("[panel] macro %d <- %s (T%d)\n", slot + 1, nm,
                    m_state.track + 1);
        saveSettings();
    }

    void macroResetDefault(int slot) {
        m_macroParamByTrack[m_state.track][slot] = -1; // factory
        m_macroParam[slot] = m_adsrParam[slot];
        m_assignMacro = -1;
        updateMacroLabel(slot);
        char vb[8];
        std::snprintf(vb, sizeof(vb), "M%d", slot + 1);
        toast("MACRO->ADSR", vb);
        std::printf("[panel] macro %d reset to ADSR\n", slot + 1);
        saveSettings();
    }

    void updateMacroLabel(int slot) {
        auto* inst = selInstrument();
        const int p = m_macroParam[slot];
        if (inst && p >= 0 && m_macroParamByTrack[m_state.track][slot] >= 0)
            std::snprintf(m_macroLabel[slot], sizeof(m_macroLabel[slot]),
                          "%d:%.7s", slot + 5, inst->parameterInfo(p).name);
        else
            std::snprintf(m_macroLabel[slot], sizeof(m_macroLabel[slot]),
                          "%d:%s", slot + 5, kAdsrLabel[slot]);
    }

    void macroTimeout() { // per-frame: 3 s assign timeout
        if (m_assignMacro >= 0 &&
            std::chrono::steady_clock::now() - m_assignTime >
                std::chrono::seconds(3)) {
            std::printf("[panel] macro %d assign timed out\n",
                        m_assignMacro + 1);
            m_assignMacro = -1;
            toastLow("MACRO", "TIMEOUT");
        }
    }

    // Pot move with soft pickup: disengaged pots only watch until the
    // physical position crosses the stored value; then they engage and
    // the param follows.
    void potMove(int pot, float value) {
        // Macro ASSIGN: touching a pot binds its parameter
        if (m_assignMacro >= 0) {
            if (m_potParam[pot] >= 0)
                macroBind(m_assignMacro, m_potParam[pot]);
            m_potPrev[pot] = value;
            return;
        }
        const int p = m_potParam[pot];
        if (p < 0) { // unbound (e.g. drum tracks: CUT/RES/S/R)
            m_potPrev[pot] = value;
            return;
        }
        const float cur = getNormParam(p);
        if (!m_potEngaged[pot]) {
            const float prev = m_potPrev[pot];
            const float eps = 0.02f;
            const bool crossed = (prev < cur - eps && value >= cur - eps) ||
                                 (prev > cur + eps && value <= cur + eps) ||
                                 std::fabs(value - cur) < eps;
            if (!crossed) {
                char vb[16];
                std::snprintf(vb, sizeof(vb), "%d|%d",
                              int(cur * 127 + 0.5f), int(value * 127 + 0.5f));
                toastLow(kPotLabel[pot], vb);
                std::printf("[panel] POT%d %s pickup: target=%d pot=%d\n",
                            pot + 1, kPotLabel[pot], int(cur * 127 + 0.5f),
                            int(value * 127 + 0.5f));
                m_potPrev[pot] = value;
                return;
            }
            m_potEngaged[pot] = true;
            std::printf("[panel] POT%d %s engaged\n", pot + 1,
                        kPotLabel[pot]);
        }
        m_potPrev[pot] = value;
        setNormParam(p, value);
        auto* inst = selInstrument();
        char vb[16];
        formatParam(inst->parameterInfo(p), value, vb, sizeof(vb));
        toastLow(kPotLabel[pot], vb);
        std::printf("[panel] POT%d %s = %s\n", pot + 1, kPotLabel[pot], vb);
    }

    // Encoder edit of a bound instrument param (step in normalized
    // units; coarse 1/16, fine 1/128 via shift).
    void editBoundParam(int pidx, float step, bool isEncoder) {
        (void)isEncoder;
        if (pidx < 0) return;
        const float nv = std::max(0.0f, std::min(1.0f,
            getNormParam(pidx) + step));
        setNormParam(pidx, nv);
        auto* inst = selInstrument();
        const auto& pi = inst->parameterInfo(pidx);
        char vb[16];
        formatParam(pi, nv, vb, sizeof(vb));
        toastLow(pi.name, vb);
        std::printf("[panel] ENC %s = %s\n", pi.name, vb);
    }

    // ── FX (two insert slots per UI track) / MFX / INST chooser ─────
    yawn::effects::AudioEffect* fxOnSelectedTrack() {
        return m_engine->mixer()
            .trackEffects(gb::Pattern::engineTrack(m_state.track))
            .effectAt(m_fxSlot);
    }

    void editFxParam(yawn::effects::AudioEffect* fx, int pidx,
                     float step) {
        const auto& pi = fx->parameterInfo(pidx);
        const float range = pi.maxValue - pi.minValue;
        float norm = range > 0.0f
            ? (fx->getParameter(pidx) - pi.minValue) / range : 0.0f;
        norm = std::max(0.0f, std::min(1.0f, norm + step));
        fx->setParameter(pidx, pi.minValue + norm * range);
        char vb[16];
        formatParam(pi, norm, vb, sizeof(vb));
        toastLow(pi.name, vb);
        std::printf("[panel] FX %s = %s\n", pi.name, vb);
    }

    // Unified chooser: kind 0 = audio FX insert, 1 = instrument swap,
    // 2 = MIDI effect. S1 opens/confirms, S2 cancels.
    void chooserKey(int kind) {
        auto& ch = m_state;
        if (!ch.chooserOpen) {
            m_chooserKind = kind;
            ch.chooserOpen = true;
            ch.chooserSel = 0;
            ch.chooserScroll = 0;
            ch.chooserItems = kind == 0 ? m_fxItems.data()
                            : kind == 1 ? m_instItems.data()
                                        : m_mfxItems.data();
            ch.chooserCount = int(kind == 0 ? m_fxItems.size()
                              : kind == 1 ? m_instItems.size()
                                          : m_mfxItems.size());
            static const char* kWhat[3] = {"FX", "INST", "MFX"};
            std::printf("[ui] %s chooser open (%d entries)\n",
                        kWhat[kind], ch.chooserCount);
            return;
        }
        chooserConfirm();
    }

    void chooserConfirm() {
        auto& ch = m_state;
        const int sel = ch.chooserSel;
        ch.chooserOpen = false;
        const int t = m_state.track;
        if (m_chooserKind == 0) {
            const auto& descs = yawn::audioEffectDescriptors();
            auto& chain = m_engine->mixer()
                .trackEffects(gb::Pattern::engineTrack(t));
            if (sel >= int(descs.size())) {
                if (chain.effectAt(m_fxSlot)) {
                    chain.removeRetired(m_fxSlot);
                    std::printf("[fx] T%d slot %d cleared\n", t + 1,
                                m_fxSlot + 1);
                }
            } else {
                auto fx = yawn::createAudioEffect(descs[sel].id);
                if (fx) {
                    chain.insert(m_fxSlot, std::move(fx));
                    std::printf("[fx] T%d slot %d <- %s\n", t + 1,
                                m_fxSlot + 1, descs[sel].displayName);
                    toast("FX", descs[sel].displayName);
                }
            }
        } else if (m_chooserKind == 1) {
            const auto& descs = yawn::instrumentDescriptors();
            if (sel >= int(descs.size())) return; // KEEP entry
            const char* id = descs[sel].id;
            m_trackInstId[t] = id;
            m_trackType[t] = 1; // instrument swap implies MIDI track
            m_engine->sendCommand(yawn::audio::SetTrackTypeMsg{t, 1});
            m_engine->setInstrument(t, yawn::createInstrument(id));
            if (std::strcmp(id, "drumrack") == 0)
                if (auto* r = dynamic_cast<yawn::instruments::DrumRack*>(
                        m_engine->instrument(t))) {
                    auto smp = makeDrumSample(t - 1, m_engine->sampleRate());
                    r->loadPad(gb::Pattern::kPadNote[t], smp.data(),
                               int(smp.size()) / 2, 2);
                }
            // fresh instrument: macros reset to factory ADSR, all
            // bindings rebuilt from its parameterInfo
            for (int i = 0; i < 4; ++i) m_macroParamByTrack[t][i] = -1;
            rebindParams();
            rebuildDrumVoices(t); // voice map follows the instrument
            std::printf("[inst] T%d <- %s\n", t + 1,
                        descs[sel].displayName);
            toast("INST", descs[sel].displayName);
        } else {
            const auto& descs = yawn::midiEffectDescriptors();
            auto& chain = m_engine->midiEffectChain(t);
            if (sel >= int(descs.size())) {
                if (chain.effect(0)) {
                    chain.removeEffectRetired(0);
                    std::printf("[mfx] T%d cleared\n", t + 1);
                }
            } else {
                auto fx = yawn::createMidiEffect(descs[sel].id);
                if (fx && chain.addEffect(std::move(fx))) {
                    std::printf("[mfx] T%d <- %s\n", t + 1,
                                descs[sel].displayName);
                    toast("MFX", descs[sel].displayName);
                }
            }
        }
    }

    void chooserScroll(int delta) {
        auto& ch = m_state;
        const int count = ch.chooserCount;
        if (count <= 0) return;
        ch.chooserSel = (ch.chooserSel + delta + count) % count;
        const int rows = 11;
        if (ch.chooserSel < ch.chooserScroll)
            ch.chooserScroll = ch.chooserSel;
        if (ch.chooserSel >= ch.chooserScroll + rows)
            ch.chooserScroll = ch.chooserSel - rows + 1;
    }

    void fxBypassKey() {
        if (m_state.chooserOpen) { // cancel
            m_state.chooserOpen = false;
            std::printf("[ui] chooser cancelled\n");
            return;
        }
        if (auto* fx = fxOnSelectedTrack()) {
            fx->setBypassed(!fx->bypassed());
            std::printf("[fx] T%d slot %d %s %s\n", m_state.track + 1,
                        m_fxSlot + 1, fx->name(),
                        fx->bypassed() ? "BYPASSED" : "active");
        }
    }

    void mfxBypassKey() {
        if (m_state.chooserOpen) {
            m_state.chooserOpen = false;
            return;
        }
        auto& chain = m_engine->midiEffectChain(m_state.track);
        if (auto* fx = chain.effect(0)) {
            fx->setBypassed(!fx->bypassed());
            std::printf("[mfx] T%d %s %s\n", m_state.track + 1, fx->name(),
                        fx->bypassed() ? "BYPASSED" : "active");
        }
    }

    // ── TRACK page: channel type, monitor, bounce ───────────────────
    void trackTypeToggle() {
        const int t = m_state.track;
        if (m_trackType[t] == 1) { // MIDI -> AUDIO
            m_trackType[t] = 0;
            m_engine->sendCommand(yawn::audio::SetTrackTypeMsg{t, 0});
            m_engine->setInstrument(t, nullptr); // audio track: no inst
            std::printf("[track] T%d -> AUDIO (instrument removed)\n",
                        t + 1);
        } else {                   // AUDIO -> MIDI: restore instrument
            m_trackType[t] = 1;
            m_engine->sendCommand(yawn::audio::SetTrackTypeMsg{t, 1});
            m_engine->setInstrument(t,
                yawn::createInstrument(m_trackInstId[t]));
            if (m_trackInstId[t] == "drumrack")
                if (auto* r = dynamic_cast<yawn::instruments::DrumRack*>(
                        m_engine->instrument(t))) {
                    auto smp = makeDrumSample(t - 1, m_engine->sampleRate());
                    r->loadPad(gb::Pattern::kPadNote[t], smp.data(),
                               int(smp.size()) / 2, 2);
                }
            std::printf("[track] T%d -> MIDI (%s)\n", t + 1,
                        m_trackInstId[t].c_str());
        }
        for (int i = 0; i < 4; ++i) m_macroParamByTrack[t][i] = -1;
        rebindParams();
        saveSettings();
    }

    void trackMonitorToggle() {
        m_trackMonitor = !m_trackMonitor;
        m_engine->sendCommand(yawn::audio::SetTrackMonitorMsg{
            m_state.track, uint8_t(m_trackMonitor ? 1 : 2)});
        std::printf("[track] T%d monitor %s\n", m_state.track + 1,
                    m_trackMonitor ? "IN" : "OFF");
    }

    // Bounce: render this (MIDI) track's pattern offline to a WAV and
    // load it as a looping clip on the first AUDIO track, then mute
    // the source. Blocks briefly (offline render is non-RT by design).
    void bounceTrack(int src) {
        if (m_trackType[src] != 1) {
            toast("BOUNCE", "MIDI ONLY");
            return;
        }
        int dst = -1;
        for (int t = 0; t < 4; ++t)
            if (t != src && m_trackType[t] == 0) { dst = t; break; }
        if (dst < 0) {
            toast("BOUNCE", "NO AUDIO TRK");
            std::printf("[bounce] T%d: no AUDIO track to land on\n",
                        src + 1);
            return;
        }
        stopTransport();
        allNotesOff();
        // render only the source track: mute the others during the
        // offline render (they keep playing clips/notes otherwise);
        // restored after — the source itself is muted at the end.
        for (int t = 0; t < 4; ++t)
            if (t != src && !m_muted[t])
                m_engine->sendCommand(yawn::audio::SetTrackMuteMsg{t, true});
        const double beats = m_pattern.length / 4.0;
        yawn::audio::RenderConfig rcfg{0.0, beats, 48000, 2};
        yawn::audio::RenderProgress prog;
        m_bounceLast = -1;
        m_bounceLive.active = false;
        std::printf("[bounce] rendering T%d pattern (%.1f beats)...\n",
                    src + 1, beats);
        auto buf = yawn::audio::OfflineRenderer::render(
            *m_engine, rcfg, prog,
            [this, src](double beat) {
                // fire the pattern's steps on the source track
                const int64_t absStep = int64_t(beat * 4.0);
                if (absStep != m_bounceLast) {
                    m_bounceLast = absStep;
                    const int step = int(absStep % m_pattern.length);
                    const auto& st = m_pattern.steps[src][step];
                    if (st.on) {
                        const int note = stepNote(src, step);
                        m_engine->sendCommand(yawn::audio::SendMidiToTrackMsg{
                            src, uint8_t(yawn::midi::MidiMessage::Type::NoteOn),
                            0, uint8_t(note),
                            yawn::midi::Convert::vel7to16(
                                st.accent ? 127 : st.vel), 0});
                        m_bounceLive = {true, note,
                                        beat + 0.25 *
                                            m_pattern.gateForStep(src, step)};
                    }
                }
                if (m_bounceLive.active && beat >= m_bounceLive.offBeat) {
                    m_engine->sendCommand(yawn::audio::SendMidiToTrackMsg{
                        src, uint8_t(yawn::midi::MidiMessage::Type::NoteOff),
                        0, uint8_t(m_bounceLive.note), 0, 0});
                    m_bounceLive.active = false;
                }
            });
        // unmute the others regardless of outcome (source is muted
        // below via muteTrack on success only)
        for (int t = 0; t < 4; ++t)
            if (t != src && !m_muted[t])
                m_engine->sendCommand(yawn::audio::SetTrackMuteMsg{t, false});
        if (!buf || prog.failed.load()) {
            toast("BOUNCE", "FAILED");
            std::printf("[bounce] render FAILED\n");
            return;
        }
        // WAV record of the bounce
        std::error_code ec;
        std::filesystem::create_directories("samples", ec);
        char name[64];
        std::snprintf(name, sizeof(name), "samples/bounce_T%d.wav", src + 1);
        {
            const int n = buf->numFrames();
            std::vector<float> mono;
            mono.resize(size_t(n));
            for (int i = 0; i < n; ++i)
                mono[size_t(i)] = (buf->sample(0, i) + buf->sample(1, i))
                                  * 0.5f;
            gb::writeWavMono(name, mono.data(), n, 48000);
        }
        // load as a looping clip on the target AUDIO track
        m_clipBuf[dst] = buf;
        m_clip[dst].buffer = buf;
        m_clip[dst].looping = true;
        m_clipActive[dst] = true;
        m_engine->sendCommand(yawn::audio::LaunchClipMsg{
            dst, 0, &m_clip[dst], yawn::audio::QuantizeMode::None});
        muteTrack(src); // mute the MIDI source
        m_engine->sendCommand(yawn::audio::TransportPlayMsg{});
        std::printf("[bounce] T%d -> T%d (%s, %d frames, clip looping)\n",
                    src + 1, dst + 1, name, int(buf->numFrames()));
        toast("BOUNCE", name);
        m_bounceDone = true;
    }

    // Record: REC-armed + AUDIO track + PLAY captures input into the
    // track's clip slot. Stop finalizes the take; the clip launches on
    // the NEXT play — never in the same command window as a transport
    // stop (the engine's stop triggers a ~5 ms clip fade that would
    // clobber a clip launched inside it).
    void recordArmAudioStart() {
        if (!m_recArmed || m_trackType[m_state.track] != 0) return;
        m_capture = (m_testMode != 0)
            ? std::unique_ptr<gb::CaptureSource>(new gb::SineCaptureSource())
            : std::unique_ptr<gb::CaptureSource>(
                  new gb::EngineCaptureSource(*m_engine));
        if (m_capture->start(kTakeFrames)) {
            m_clipRecording = true;
            std::printf("[track] T%d clip record start\n",
                        m_state.track + 1);
        }
    }

    void recordArmAudioStop() {
        if (!m_clipRecording || !m_capture) return;
        m_capture->stop();
        const int n = m_capture->framesWritten();
        const int t = m_state.track;
        if (n > 0) {
            m_clipBuf[t] =
                std::make_shared<yawn::audio::AudioBuffer>(2, n);
            for (int i = 0; i < n; ++i) {
                m_clipBuf[t]->sample(0, i) = m_capture->data()[i];
                m_clipBuf[t]->sample(1, i) = m_capture->data()[i];
            }
            m_clip[t].buffer = m_clipBuf[t];
            m_clip[t].looping = true;
            m_clipPending[t] = true; // launched on next PLAY
            std::printf("[track] T%d clip recorded (%d frames, pending "
                        "launch)\n", t + 1, n);
            m_clipRecorded = true;
        }
        m_capture.reset();
        m_clipRecording = false;
    }

    void launchPendingClips() {
        for (int t = 0; t < 4; ++t) {
            if (!m_clipPending[t]) continue;
            m_engine->sendCommand(yawn::audio::LaunchClipMsg{
                t, 0, &m_clip[t], yawn::audio::QuantizeMode::None});
            m_clipPending[t] = false;
            m_clipActive[t] = true;
            std::printf("[track] T%d clip launched (looping)\n", t + 1);
        }
    }

    // ── SAMPLE page ─────────────────────────────────────────────────
    static constexpr int kTakeFrames = 48000 * 10; // 10 s @ 48 kHz mono

    void sampleRecToggle() {
        if (m_sampleState == 1) { sampleStop(); return; }
        if (m_testMode == 0 &&
            yawn::audio::AudioEngine::defaultInputDevice() < 0) {
            m_state.sampleMsg = "NO INPUT";
            std::printf("[sample] NO INPUT (no default capture device)\n");
            return;
        }
        m_state.sampleMsg = nullptr;
        m_capture = (m_testMode != 0)
            ? std::unique_ptr<gb::CaptureSource>(new gb::SineCaptureSource())
            : std::unique_ptr<gb::CaptureSource>(
                  new gb::EngineCaptureSource(*m_engine));
        if (!m_capture->start(kTakeFrames)) {
            m_state.sampleMsg = "NO INPUT";
            m_capture.reset();
            std::printf("[sample] capture start FAILED\n");
            return;
        }
        m_sampleState = 1;
        std::printf("[sample] REC start\n");
    }

    void sampleStop() {
        if (m_sampleState != 1 || !m_capture) return;
        m_capture->stop();
        const int n = m_capture->framesWritten();
        m_take.assign(m_capture->data(), m_capture->data() + n);
        m_takeLen = n;
        m_capture.reset();
        m_sampleState = 2;
        // repoint the UI immediately — its state-1 waveform pointer was
        // into the just-destroyed capture buffer
        m_state.sampleState = 2;
        m_state.sampleWave = m_takeLen > 0 ? m_take.data() : nullptr;
        m_state.sampleWaveLen = m_takeLen;
        m_state.sampleLevel = 0.0f;
        m_trim0 = 0.0f; m_trim1 = 1.0f; m_gain = 1.0f;
        std::printf("[sample] REC stop: %d frames (%.1f s)\n", n,
                    n / 48000.0);
    }

    void sampleNormalize() {
        if (m_sampleState != 2 || m_takeLen == 0) return;
        const int i0 = int(m_trim0 * (m_takeLen - 1));
        const int i1 = int(m_trim1 * (m_takeLen - 1)) + 1;
        float peak = 0.0f;
        for (int i = i0; i < i1 && i < m_takeLen; ++i)
            peak = std::max(peak, std::fabs(m_take[size_t(i)]));
        if (peak > 0.001f) m_gain = std::min(4.0f, 0.95f / peak);
        std::printf("[sample] normalize: peak %.3f gain %.2f\n",
                    double(peak), double(m_gain));
        toastLow("NORM", "done");
    }

    void sampleAssign() {
        if (m_sampleState != 2 || m_takeLen == 0) return;
        if (m_state.track == 0) {
            toast("ASSIGN", "DRUMS ONLY");
            std::printf("[sample] assign: select a drum track (T2-T4)\n");
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories("samples", ec);
        // auto-incremented takeNNN.wav
        char name[64] = {};
        int takeNum = 1;
        for (; takeNum < 1000; ++takeNum) {
            std::snprintf(name, sizeof(name), "samples/take%03d.wav",
                          takeNum);
            if (!std::filesystem::exists(name, ec)) break;
        }
        const int i0 = int(m_trim0 * (m_takeLen - 1));
        const int i1 = std::min(m_takeLen, int(m_trim1 * (m_takeLen - 1)) + 1);
        const int n = std::max(0, i1 - i0);
        std::vector<float> buf;
        buf.resize(size_t(n));
        for (int i = 0; i < n; ++i)
            buf[size_t(i)] = m_take[size_t(i0 + i)] * m_gain;
        if (!gb::writeWavMono(name, buf.data(), n, 48000)) {
            std::printf("[sample] WAV write FAILED: %s\n", name);
            return;
        }
        // load into this track's pad (mono → stereo duplicate)
        std::vector<float> st;
        st.resize(size_t(n) * 2);
        for (int i = 0; i < n; ++i)
            st[size_t(i) * 2] = st[size_t(i) * 2 + 1] = buf[size_t(i)];
        if (auto* rack = dynamic_cast<yawn::instruments::DrumRack*>(
                m_engine->instrument(m_state.track)))
            rack->loadPad(gb::Pattern::kPadNote[m_state.track], st.data(),
                          n, 2);
        rebuildDrumVoices(m_state.track);
        m_assigned = true;
        std::printf("[sample] assigned %s (%d frames) -> T%d pad %d\n",
                    name, n, m_state.track + 1,
                    gb::Pattern::kPadNote[m_state.track]);
        char vb[16];
        std::snprintf(vb, sizeof(vb), "take%03d", takeNum);
        toast("SAVED", vb);
        refreshBrowser();
    }

    void pollSample() { // per-frame: recording progress → UiState
        m_state.sampleState = m_sampleState;
        m_state.sampleGain = m_gain;
        m_state.sampleTrim0 = m_trim0;
        m_state.sampleTrim1 = m_trim1;
        if (m_sampleState == 1 && m_capture) {
            m_state.sampleLevel = std::min(1.0f, m_capture->recentPeak());
            m_state.sampleElapsed = m_capture->framesWritten() / 48000.0f;
            m_state.sampleWave = m_capture->data();
            m_state.sampleWaveLen = m_capture->framesWritten();
            if (m_capture->done()) sampleStop(); // buffer full
        } else {
            m_state.sampleLevel = 0.0f;
            m_state.sampleWave = m_takeLen > 0 ? m_take.data() : nullptr;
            m_state.sampleWaveLen = m_takeLen;
        }
    }

    // ── LOAD page (file browser) ────────────────────────────────────
    void refreshBrowser() {
        if (!gb::dirExists(m_browserDir))
            std::filesystem::create_directories(m_browserDir, m_fsEc);
        std::vector<gb::DirEntry> de;
        gb::listDir(m_browserDir, de);
        m_browserNames.clear();
        m_browserNames.push_back("..");
        for (const auto& e : de) m_browserNames.push_back(e.name);
        if (m_browserNames.size() > 256) m_browserNames.resize(256);
        m_browserIsDir[0] = true;
        for (size_t i = 1; i < m_browserNames.size(); ++i)
            m_browserIsDir[i] = de[i - 1].isDir;
        m_browserPtrs.assign(m_browserNames.size(), nullptr);
        for (size_t i = 0; i < m_browserNames.size(); ++i)
            m_browserPtrs[i] = m_browserNames[i].c_str();
        auto& b = m_state.browser;
        b.path = m_browserDir.c_str();
        b.entries = m_browserPtrs.data();
        b.isDir = m_browserIsDir;
        b.count = int(m_browserPtrs.size());
        b.selected = std::min(b.selected, b.count - 1);
        b.scroll = 0;
    }

    void browserActivate() {
        auto& b = m_state.browser;
        if (b.selected < 0 || b.selected >= b.count) return;
        const std::string& name = m_browserNames[size_t(b.selected)];
        if (name == "..") { browserUp(); return; }
        const std::string full = m_browserDir + "/" + name;
        if (m_browserIsDir[size_t(b.selected)]) {
            m_browserDir = full;
            b.selected = 0;
            refreshBrowser();
            std::printf("[load] dir -> %s\n", m_browserDir.c_str());
            return;
        }
        if (m_state.track == 0) {
            toast("LOAD", "DRUMS ONLY");
            return;
        }
        std::vector<float> data;
        int sr = 0;
        const int n = gb::readWavStereo(full.c_str(), data, sr);
        if (n <= 0) {
            std::printf("[load] read FAILED: %s\n", full.c_str());
            toastLow("LOAD", "FAILED");
            return;
        }
        if (auto* rack = dynamic_cast<yawn::instruments::DrumRack*>(
                m_engine->instrument(m_state.track)))
            rack->loadPad(gb::Pattern::kPadNote[m_state.track],
                          data.data(), n, 2);
        rebuildDrumVoices(m_state.track);
        std::printf("[load] %s (%d frames @%d) -> T%d pad %d\n",
                    full.c_str(), n, sr, m_state.track + 1,
                    gb::Pattern::kPadNote[m_state.track]);
        toast("LOADED", name.c_str());
        m_browserLoaded = true;
    }

    void browserUp() {
        if (m_browserDir == "samples" || m_browserDir == "samples/") return;
        const auto pos = m_browserDir.find_last_of('/');
        m_browserDir = (pos == std::string::npos)
                           ? "samples" : m_browserDir.substr(0, pos);
        m_state.browser.selected = 0;
        refreshBrowser();
        std::printf("[load] dir -> %s\n", m_browserDir.c_str());
    }

    // ── SETTINGS page ───────────────────────────────────────────────
    static const char* themeNameGb() {
        return gb::themeName(gb::currentTheme());
    }
    const char* velSourceName() const {
        return m_velSource == 1 ? "FIXED100" : m_velSource == 2 ? "HOST"
                                                                : "SLIDER";
    }

    void settingsAdjust(int dir) {
        switch (m_state.settingsSel) {
        case 0: // THEME
            gb::setTheme(gb::ThemeId(
                (gb::currentTheme() + (dir > 0 ? 1 : gb::kNumThemes - 1)) %
                gb::kNumThemes));
            std::printf("[set] theme -> %s\n", themeNameGb());
            break;
        case 1: // VELOCITY SOURCE
            m_velSource = (m_velSource + (dir > 0 ? 1 : 2)) % 3;
            std::printf("[set] velocity source -> %s\n", velSourceName());
            break;
        case 2: // LED brightness (stub for the panel, no-op here)
            m_ledBrightness = std::max(1, std::min(15,
                m_ledBrightness + (dir > 0 ? 1 : -1)));
            std::printf("[set] LED brightness -> %d (stub)\n",
                        m_ledBrightness);
            break;
        case 3: // SCALE lock (play mode)
            m_scaleLock = (m_scaleLock + (dir > 0 ? 1 : 2)) % 3;
            std::printf("[set] scale -> %s\n", scaleName());
            break;
        }
        syncSettingsUi();
        saveSettings(); // save-on-change
    }

    const char* scaleName() const {
        return m_scaleLock == 0 ? "MAJOR" : m_scaleLock == 1 ? "MINOR"
                                                             : "CHROM";
    }

    void syncSettingsUi() {
        m_state.settingsVal[0] = themeNameGb();
        m_state.settingsVal[1] = velSourceName();
        std::snprintf(m_ledValBuf, sizeof(m_ledValBuf), "%d",
                      m_ledBrightness);
        m_state.settingsVal[2] = m_ledValBuf;
        m_state.settingsVal[3] = scaleName();
    }

    void saveSettings() {
        nlohmann::json j;
        j["theme"] = themeNameGb();
        j["velSource"] = velSourceName();
        j["ledBrightness"] = m_ledBrightness;
        j["scale"] = scaleName();
        // per-track channel types (0=AUDIO 1=MIDI)
        nlohmann::json tt = nlohmann::json::array();
        for (int t = 0; t < 4; ++t) tt.push_back(m_trackType[t]);
        j["trackTypes"] = tt;
        // macro assignments, per track (-1 = factory ADSR default)
        nlohmann::json mac;
        for (int t = 0; t < 4; ++t) {
            nlohmann::json arr = nlohmann::json::array();
            for (int i = 0; i < 4; ++i)
                arr.push_back(m_macroParamByTrack[t][i]);
            mac[std::to_string(t)] = arr;
        }
        j["macros"] = mac;
        if (FILE* f = std::fopen("settings.json", "wb")) {
            const std::string s = j.dump(2);
            std::fwrite(s.data(), 1, s.size(), f);
            std::fclose(f);
            std::printf("[set] saved settings.json\n");
        }
    }

    void loadSettings() {
        FILE* f = std::fopen("settings.json", "rb");
        if (!f) return;
        char buf[512];
        const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        buf[n] = 0;
        try {
            auto j = nlohmann::json::parse(buf);
            const std::string th = j.value("theme", "MONO");
            gb::setTheme(th == "RED"    ? gb::kThemeRed
                         : th == "GREEN"  ? gb::kThemeGreen
                         : th == "AMBER"  ? gb::kThemeAmber
                         : th == "ARCADE" ? gb::kThemeArcade
                                          : gb::kThemeMono);
            const std::string vs = j.value("velSource", "SLIDER");
            m_velSource = vs == "FIXED100" ? 1 : vs == "HOST" ? 2 : 0;
            m_ledBrightness = j.value("ledBrightness", 8);
            const std::string sc = j.value("scale", "CHROM");
            m_scaleLock = sc == "MAJOR" ? 0 : sc == "MINOR" ? 1 : 2;
            if (j.contains("macros")) {
                for (int t = 0; t < 4; ++t) {
                    const auto& arr = j["macros"].value(
                        std::to_string(t), nlohmann::json::array());
                    if (arr.is_array())
                        for (int i = 0; i < 4 && i < int(arr.size()); ++i)
                            m_macroParamByTrack[t][i] =
                                arr[i].get<int>();
                }
                rebindParams(); // refresh macro bindings/labels
            }
            if (j.contains("trackTypes")) {
                int t = 0;
                for (const auto& v : j["trackTypes"]) {
                    if (t < 4 && v.is_number())
                        m_trackType[t++] = v.get<int>() == 0 ? 0 : 1;
                }
                // NOTE: applied as state only; engine-side type set on
                // next toggle (settings load happens before engine use).
            }
            std::printf("[set] loaded settings.json (theme=%s vel=%s)\n",
                        th.c_str(), vs.c_str());
        } catch (...) {
            std::printf("[set] settings.json unreadable — defaults\n");
        }
    }

    // ── Confirm dialog ──────────────────────────────────────────────
    void openConfirm(int action) {
        m_confirmAction = action;
        m_state.dialogActive = true;
        if (action == 1) {
            m_state.dlgTitle = "CLEAR TRACK?";
            m_state.dlgLine = "PATTERN GONE.";
        } else {
            m_state.dlgTitle = "QUIT?";
            m_state.dlgLine = "UNSAVED STATE LOST.";
        }
        std::printf("[ui] confirm: %s\n", m_state.dlgTitle);
    }

    void closeConfirm() {
        m_confirmAction = 0;
        m_state.dialogActive = false;
        std::printf("[ui] confirm cancelled\n");
    }

    void doConfirm() {
        const int action = m_confirmAction;
        closeConfirm();
        if (action == 1) {
            m_pattern.clearTrack(m_state.track);
            std::printf("[seq] T%d pattern cleared\n", m_state.track + 1);
        } else if (action == 2) {
            m_running = false;
        }
    }

    // ── Param/FX sync (engine → UI, every frame) ────────────────────
    void syncParamsToUi() {
        // INST page header: edit pad (last-played voice) on drum tracks
        m_state.padName =
            isDrumTrack(m_state.track)
                ? drumVoiceName(m_state.track, m_editPad[m_state.track],
                                m_padNameBuf[m_state.track],
                                sizeof(m_padNameBuf[m_state.track]))
                : nullptr;
        // param rows: CUT/RES (pots) + A/D/S/R (engine ADSR values,
        // informational — macros may be assigned elsewhere); unbound
        // (drums CUT/RES/S/R) → "—"
        for (int i = 0; i < 2; ++i) {
            m_state.params[i].name = kPotLabel[i];
            m_state.params[i].value =
                m_potParam[i] >= 0
                    ? int(getNormParam(m_potParam[i]) * 127.0f + 0.5f)
                    : -1;
        }
        for (int i = 0; i < 4; ++i) {
            m_state.params[2 + i].name = kAdsrLabel[i];
            m_state.params[2 + i].value =
                m_adsrParam[i] >= 0
                    ? int(getNormParam(m_adsrParam[i]) * 127.0f + 0.5f)
                    : -1;
        }
        // macro strip labels (encoders 5-8): ADSR short names or the
        // assigned param's name
        for (int i = 0; i < 4; ++i) {
            m_state.macroLabels[i] = m_macroLabel[i];
        }
        // dynamic encoder labels
        if (m_state.page == 0) {
            auto* inst = selInstrument();
            for (int i = 0; i < 4; ++i) {
                if (inst && m_encParam[i] >= 0)
                    std::snprintf(m_encLabelBuf[i], sizeof(m_encLabelBuf[i]),
                                  "%.8s", inst->parameterInfo(m_encParam[i]).name);
                else
                    std::snprintf(m_encLabelBuf[i], sizeof(m_encLabelBuf[i]),
                                  "---");
                m_state.encLabels[i] = m_encLabelBuf[i];
            }
        } else if (m_state.page == 3) {
            auto* fx = fxOnSelectedTrack();
            for (int i = 0; i < 4; ++i) {
                if (fx && i < fx->parameterCount())
                    std::snprintf(m_encLabelBuf[i], sizeof(m_encLabelBuf[i]),
                                  "%.8s", fx->parameterInfo(i).name);
                else
                    std::snprintf(m_encLabelBuf[i], sizeof(m_encLabelBuf[i]),
                                  "---");
                m_state.encLabels[i] = m_encLabelBuf[i];
            }
            m_state.fxName = fx ? fx->name() : nullptr;
            m_state.fxBypassed = fx && fx->bypassed();
            for (int i = 0; i < 4; ++i) {
                if (fx && i < fx->parameterCount()) {
                    const auto& pi = fx->parameterInfo(i);
                    const float range = pi.maxValue - pi.minValue;
                    m_state.fxParams[i].name = pi.name;
                    m_state.fxParams[i].value =
                        range > 0.0f
                            ? int((fx->getParameter(i) - pi.minValue) /
                                  range * 127.0f + 0.5f)
                            : 0;
                } else {
                    m_state.fxParams[i].name = nullptr;
                    m_state.fxParams[i].value = -1;
                }
            }
        } else if (m_state.page == 7) { // MFX
            auto& chain = m_engine->midiEffectChain(m_state.track);
            auto* fx = chain.effect(0);
            for (int i = 0; i < 4; ++i) {
                if (fx && i < fx->parameterCount())
                    std::snprintf(m_encLabelBuf[i], sizeof(m_encLabelBuf[i]),
                                  "%.8s", fx->parameterInfo(i).name);
                else
                    std::snprintf(m_encLabelBuf[i], sizeof(m_encLabelBuf[i]),
                                  "---");
                m_state.encLabels[i] = m_encLabelBuf[i];
            }
            m_state.mfxName = fx ? fx->name() : nullptr;
            m_state.mfxBypassed = fx && fx->bypassed();
            for (int i = 0; i < 4; ++i) {
                if (fx && i < fx->parameterCount()) {
                    const auto& pi = fx->parameterInfo(i);
                    const float range = pi.maxValue - pi.minValue;
                    m_state.mfxParams[i].name = pi.name;
                    m_state.mfxParams[i].value =
                        range > 0.0f
                            ? int((fx->getParameter(i) - pi.minValue) /
                                  range * 127.0f + 0.5f)
                            : 0;
                } else {
                    m_state.mfxParams[i].name = nullptr;
                    m_state.mfxParams[i].value = -1;
                }
            }
        }
        // TRACK page fields (always synced — cheap)
        m_state.fxSlot = m_fxSlot;
        m_state.trackType = m_trackType[m_state.track];
        m_state.trackVol = m_trackVol;
        m_state.trackPan = m_trackPan;
        m_state.trackInputCh = m_trackInputCh;
        m_state.trackMonitor = m_trackMonitor;
        m_state.trackHasClip = m_clipActive[m_state.track];
    }

    int velocity7() {
        int v;
        if (m_velSource == 1)      v = 100;         // fixed
        else if (m_velSource == 2) v = m_hostVel;   // host register
        else v = std::max(1, std::min(127, int(m_slider * 127.0f)));
        m_hostVel = v; // host register tracks the last velocity used
        return v;
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
        else if (m_testMode == 5) probeStep(t, once);
        else if (m_testMode == 6) paramtestStep(t, once);
        else if (m_testMode == 7) sampletestStep(t, once);
        else if (m_testMode == 8) nsr2testStep(t, once);
        else if (m_testMode == 9) revbtestStep(t, once);
        else if (m_testMode == 10) macrotestStep(t, once);
        else if (m_testMode == 11) tracktestStep(t, once);
        else if (m_testMode == 12) bouncetestStep(t, once);
        else if (m_testMode == 13) drumtestStep(t, once);
        else                       padtestStep(t, once);
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
        // NSR-1 rev B: steps live on the step row (40-55), pitch via
        // p-lock (hold step + piano tap), gate via hold + encoder.
        using gb::kKeyStep0;
        if (t >= 0.3 && once(0)) {
            std::printf("[uitest] select T1 via shift+step1; "
                        "toggle steps 0/3/7 on the step row\n");
            setPage(1);
            onKey(gb::kKeyShiftL, true);
            onKey(kKeyStep0 + 0, true); onKey(kKeyStep0 + 0, false);
            onKey(gb::kKeyShiftL, false);
            for (int s : {0, 3, 7}) {
                onKey(kKeyStep0 + s, true); onKey(kKeyStep0 + s, false);
            }
        }
        if (t >= 0.6 && once(1)) {
            std::printf("[uitest] hold step 3 + piano E4 -> p-lock\n");
            onKey(kKeyStep0 + 3, true);        // hold step 3
            onKey(2, true); onKey(2, false);   // piano white 3 = E4
            dumpFrame("uitest_toast.rgb565");  // toast visible mid-edit
            onKey(kKeyStep0 + 3, false);
        }
        if (t >= 0.9 && once(2)) {
            std::printf("[uitest] hold step 7 + piano G4, ENC1 gate 85%%\n");
            onKey(kKeyStep0 + 7, true);
            onKey(4, true); onKey(4, false);   // piano white 5 = G4
            onEncoderDelta(0, +3);             // gate 70% -> 85%
            onKey(kKeyStep0 + 7, false);
        }
        if (t >= 1.1 && once(3)) { std::printf("[uitest] PLAY\n"); togglePlay(); }
        if (t >= 1.0 && once(9)) {
            // Repeat-storm regression on the step row: toggle step 5 on,
            // hold it, edit pitch via p-lock, interleave duplicate
            // key-down events, release. The edit must stick and the step
            // must NOT toggle off.
            onKey(kKeyStep0 + 5, true); onKey(kKeyStep0 + 5, false);
            onKey(kKeyStep0 + 5, true);                       // hold
            onKey(1, true); onKey(1, false);                  // p-lock D4
            onKey(kKeyStep0 + 5, true); onKey(kKeyStep0 + 5, true);
            onKey(kKeyStep0 + 5, true);                       // storm
            onKey(kKeyStep0 + 5, false);                      // release
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
            onKey(gb::kKeyStep0 + 6, true); onKey(gb::kKeyStep0 + 6, false);
            onKey(gb::kKeyShiftL, false);   // shift+step 7 -> mute T3
            m_muteOk = m_muted[2];
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyStep0 + 6, true); onKey(gb::kKeyStep0 + 6, false);
            onKey(gb::kKeyShiftL, false);   // toggle back
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
        check(m_muteOk, "shift+step7 mutes/unmutes T3");
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
            // step 0 on page 1 (step row key 1)
            onKey(gb::kKeyStep0 + 0, true); onKey(gb::kKeyStep0 + 0, false);
            // shift+> to page 2 (manual window)
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyNext, true); onKey(gb::kKeyNext, false);
            onKey(gb::kKeyShiftL, false);
            m_windowOk = (m_window == 16) && !m_follow;
            // step 20 = step-row key 5 on page 2; accent via hold +
            // SHIFT tap; pitch E4 via hold + piano tap (p-lock)
            onKey(gb::kKeyStep0 + 4, true); onKey(gb::kKeyStep0 + 4, false);
            onKey(gb::kKeyStep0 + 4, true);  // hold step 20
            onKey(gb::kKeyShiftL, true);     // SHIFT tap = accent
            onKey(gb::kKeyShiftL, false);
            onKey(2, true); onKey(2, false); // piano white 3 = E4 p-lock
            onKey(gb::kKeyStep0 + 4, false); // release (edited, no toggle)
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
    // wheel over encoder 3 (ATK on the SYNTH page). Desktop-sim only:
    // the Pi backend has no SDL event queue to inject into.
    template <typename Once>
    void probeStep(double t, Once& once) {
#ifdef GB_PI_BACKEND
        if (t >= 0.5 && once(0)) {
            std::printf("[probe] not supported on the Pi backend "
                        "(no SDL event injection) — skipped\n");
            m_running = false;
        }
#else
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
        // panel coords: mm*4. NSR-1: white key 1 (46,134)mm.
        // NSR-2: grid key 0 center (41,138)mm.
        const float keyX = m_nsr2 ? 41.0f : 46.0f;
        const float keyY = m_nsr2 ? 138.0f : 134.0f;
        if (t >= 0.5 && once(0)) {
            std::printf("[probe] click key @ (%.0f,%.0f)mm\n", keyX, keyY);
            pushButton(keyX * 4, keyY * 4, true);
        }
        if (t >= 0.8 && once(1)) pushButton(keyX * 4, keyY * 4, false);
        // NSR-1: RES pot drag. NSR-2: crossfader drag (analog ch 6).
        if (t >= 1.1 && once(2)) {
            if (m_nsr2) {
                // click near the fader bottom, drag up 60px: 0.04 -> 0.33
                std::printf("[probe] drag crossfader up 60px\n");
                pushButton(162.0f * 4, 180.0f * 4, true);
                for (int i = 1; i <= 6; ++i)
                    pushMotion(162.0f * 4, (180.0f * 4) - i * 10.0f);
                pushButton(162.0f * 4, 180.0f * 4 - 60.0f, false);
            } else {
                std::printf("[probe] drag RES pot up 60px\n");
                pushButton(158.0f * 4, 32.0f * 4, true);
                for (int i = 1; i <= 6; ++i)
                    pushMotion(158.0f * 4, (32.0f * 4) - i * 10.0f);
                pushButton(158.0f * 4, 32.0f * 4 - 60.0f, false);
            }
        }
        // encoder 3 center: NSR-1 (70,88)mm, NSR-2 (97,103)mm
        if (t >= 1.5 && once(3)) {
            const float ex = m_nsr2 ? 97.0f : 70.0f;
            const float ey = m_nsr2 ? 103.0f : 88.0f;
            std::printf("[probe] wheel over ENC3\n");
            pushWheel(ex * 4, ey * 4, +1);
            pushWheel(ex * 4, ey * 4, +1);
        }
        // NSR-1 rev B: click step-row key 1 ((45.5,156.5)mm) = step
        // toggle on T1 (key index 40)
        if (t >= 1.8 && once(4) && !m_nsr2) {
            std::printf("[probe] click step-row key 1\n");
            pushButton(45.5f * 4, 156.5f * 4, true);
            pushButton(45.5f * 4, 156.5f * 4, false);
        }
        if (t >= 2.0 && once(5)) {
            if (!m_nsr2)
                m_probeStepOk = m_pattern.steps[0][0].on;
            std::printf("[probe] done — asserting\n");
            m_running = false;
        }
#endif // GB_PI_BACKEND
    }

    int probeVerdict() {
#ifdef GB_PI_BACKEND
        return 0; // skipped — see probeStep
#else
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[probe] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_probeNotes == 1, "mouse click on key -> note event");
        if (m_nsr2)
            check(std::fabs(m_slider - 0.8f) > 0.05f,
                  "crossfader drag -> analog delta (ch 6)");
        else
            check(m_state.params[1].value != 41,
                  "RES pot drag -> analog delta");
        // ENC3 on the OSC page = Osc2 Wave (norm default 0.25)
        check(std::fabs(getNormParam(m_encParam[2]) - 0.25f) > 0.05f,
              "wheel over ENC3 -> encoder delta");
        if (!m_nsr2)
            check(m_probeStepOk, "click step-row key -> step toggles on");
        std::printf("[probe] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
#endif // GB_PI_BACKEND
    }

    // --paramtest: pot pickup, encoder edits/reset/fine, FX chooser,
    // drum-track pot mapping — all through the HAL event path.
    template <typename Once>
    void paramtestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[paramtest] select T1 (shift+step1) — pickup "
                        "re-arms\n");
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyStep0 + 0, true); onKey(gb::kKeyStep0 + 0, false);
            onKey(gb::kKeyShiftL, false);
            // sweep CUT pot 0.0 -> 0.6 in small steps: param must NOT
            // move (stored = 87/127 ≈ 0.685 from boot)
            for (int i = 0; i <= 12; ++i) onAnalog(gb::kAnalogPot0, i * 0.05f);
            const float norm = getNormParam(m_potParam[0]);
            m_ptOk[0] = !m_potEngaged[0] && norm > 0.66f && norm < 0.71f;
            std::printf("[paramtest] after sub-target sweep: engaged=%d "
                        "norm=%.3f\n", int(m_potEngaged[0]), double(norm));
        }
        if (t >= 0.6 && once(1)) {
            // cross the stored value -> engages, param follows
            onAnalog(gb::kAnalogPot0, 0.70f);
            onAnalog(gb::kAnalogPot0, 0.75f);
            const float norm = getNormParam(m_potParam[0]);
            m_ptOk[1] = m_potEngaged[0] && norm > 0.73f && norm < 0.77f;
            std::printf("[paramtest] after crossing: engaged=%d norm=%.3f\n",
                        int(m_potEngaged[0]), double(norm));
        }
        if (t >= 0.8 && once(2)) {
            // encoder edit (coarse) then push-to-reset. ENC1 on the OSC
            // page = Osc1 Wave (min 0 max 4 default 1 → norm 0.25).
            onEncoderDelta(0, +1); // +1/16 = 0.0625
            const float edited = getNormParam(m_encParam[0]);
            m_ptOk[2] = edited > 0.29f && edited < 0.34f && m_toastShown;
            onEncoderPush(0, true); onEncoderPush(0, false);
            const float reset = getNormParam(m_encParam[0]);
            m_ptOk[3] = reset > 0.23f && reset < 0.27f;
            std::printf("[paramtest] enc edit %.3f -> reset %.3f\n",
                        double(edited), double(reset));
        }
        if (t >= 1.0 && once(3)) {
            // shift-fine adjust: step must be tiny (1/128)
            const float before = getNormParam(m_encParam[0]);
            onKey(gb::kKeyShiftL, true);
            onEncoderDelta(0, +1);
            onKey(gb::kKeyShiftL, false);
            const float d = getNormParam(m_encParam[0]) - before;
            m_ptOk[4] = d > 0.001f && d < 0.02f;
            std::printf("[paramtest] shift-fine delta = %.4f\n", double(d));
        }
        if (t >= 1.2 && once(4)) softKey(1); // PG+ -> osc param page
        if (t >= 1.3 && once(9)) dumpFrame("paramtest_p2.rgb565");
        if (t >= 1.35 && once(10)) {
            // Exclusion sweep (rev B2): turn ALL 4 pageable encoders on
            // ALL pages — no dedicated-control parameter (pots CUT/RES,
            // macro ADSR defaults) may change.
            float beforeCut = getNormParam(m_potParam[0]);
            float beforeRes = getNormParam(m_potParam[1]);
            float beforeAdsr[4];
            for (int i = 0; i < 4; ++i)
                beforeAdsr[i] = m_adsrParam[i] >= 0
                                    ? getNormParam(m_adsrParam[i]) : -1.0f;
            for (int p = 0; p < 3; ++p) {
                for (int e = 0; e < 4; ++e) onEncoderDelta(e, +2);
                softKey(1);
            }
            bool same = std::fabs(getNormParam(m_potParam[0]) - beforeCut) < 1e-4f &&
                        std::fabs(getNormParam(m_potParam[1]) - beforeRes) < 1e-4f;
            for (int i = 0; i < 4; ++i)
                if (m_adsrParam[i] >= 0 &&
                    std::fabs(getNormParam(m_adsrParam[i]) - beforeAdsr[i]) >
                        1e-4f)
                    same = false;
            m_ptOk[8] = same;
            m_ptOk[9] = m_encPages[0].count == 4 &&
                        m_encPages[1].count == 4;
            std::printf("[paramtest] encoder sweep: dedicated params %s; "
                        "OSC=%d MOD=%d MISC=%d\n", same ? "untouched" : "MOVED",
                        m_encPages[0].count, m_encPages[1].count,
                        m_encPages[2].count);
        }
        if (t >= 1.4 && once(12)) {
            // ADSR macro defaults: encoder 5 edits amp attack (relative,
            // no pickup); shift = fine (1/256)
            const float b4 = getNormParam(m_adsrParam[0]);
            onEncoderDelta(4, +4); // 4/64 = 0.0625
            const float a4 = getNormParam(m_adsrParam[0]);
            onKey(gb::kKeyShiftL, true);
            onEncoderDelta(4, +1); // 1/256 fine
            onKey(gb::kKeyShiftL, false);
            const float f4 = getNormParam(m_adsrParam[0]);
            m_ptOk[11] = (a4 - b4) > 0.04f && (a4 - b4) < 0.09f &&
                         (f4 - a4) > 0.001f && (f4 - a4) < 0.01f;
            std::printf("[paramtest] ADSR enc5: %.3f -> %.3f -> %.3f\n",
                        double(b4), double(a4), double(f4));
        }
        if (t >= 1.4 && once(5)) {
            std::printf("[paramtest] FX: select T2, page -> FX, chooser\n");
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyStep0 + 1, true); onKey(gb::kKeyStep0 + 1, false); // shift+step 2 = T2
            onKey(gb::kKeyShiftL, false);
            setPage(3); // FX (cycle order changed in M2d)
            softKey(0); // open chooser
            m_ptOk[5] = m_state.chooserOpen;
            softKey(0); // confirm: Reverb (descriptor 0)
            const auto* fx = fxOnSelectedTrack();
            m_ptOk[5] = m_ptOk[5] && fx && containsCI(fx->name(), "reverb");
            std::printf("[paramtest] fx on T2: %s\n",
                        fx ? fx->name() : "(none)");
        }
        if (t >= 1.6 && once(6)) {
            auto* fx = fxOnSelectedTrack();
            const float before = fx ? fx->getParameter(0) : -1.0f;
            onEncoderDelta(0, +2); // FX param 0 += 2/16
            const float after = fx ? fx->getParameter(0) : -1.0f;
            m_ptOk[6] = fx && after > before;
            std::printf("[paramtest] fx param0 %.3f -> %.3f\n",
                        double(before), double(after));
            dumpFrame("paramtest_fx.rgb565");
        }
        if (t >= 1.8 && once(7)) {
            // drum-track macros: ENC5 (macro 1, factory = Pad Attack)
            onEncoderDelta(4, +8); // +8/64 = 0.125 norm
            auto* rack = dynamic_cast<yawn::instruments::DrumRack*>(
                m_engine->instrument(1));
            m_ptOk[7] = rack && rack->getParameter(5) > 0.01f;
            onAnalog(gb::kAnalogPot0, 0.1f); // CUT: unbound, must not crash
            std::printf("[paramtest] drum pad attack = %.3f\n",
                        rack ? double(rack->getParameter(5)) : -1.0);
            // re-arm a toast and dump it on the FX page
            onEncoderDelta(0, +1);
            dumpFrame("paramtest_toast.rgb565");
        }
        if (t >= 1.9 && once(11)) {
            // DrumRack: pages are PAD (4 pad params) / PAD2 (Start/End)
            // / KIT (Volume) — PG+ walks all three in order (all
            // non-empty, so none are skipped).
            setPage(0);
            m_ptOk[10] = m_encPages[0].count == 4 &&
                         m_encPages[1].count == 2 &&
                         m_encPages[2].count == 1;
            const int p0 = m_paramPage;
            softKey(1); // PG+
            const int p1 = m_paramPage;
            softKey(1); // PG+
            m_ptOk[10] = m_ptOk[10] && p1 == (p0 + 1) % 3 &&
                         m_paramPage == (p0 + 2) % 3;
            std::printf("[paramtest] drum pages: PAD=%d PAD2=%d KIT=%d "
                        "cycle %d->%d->%d\n", m_encPages[0].count,
                        m_encPages[1].count, m_encPages[2].count,
                        p0, p1, m_paramPage);
        }
        if (t >= 2.0 && once(8)) {
            std::printf("[paramtest] done — asserting\n");
            m_running = false;
        }
    }

    int paramtestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[paramtest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_ptOk[0], "pot pickup: no change below stored value");
        check(m_ptOk[1], "pot pickup: engages on crossing, param follows");
        check(m_ptOk[2], "encoder edit applies + toast shown");
        check(m_ptOk[3], "encoder push resets to default");
        check(m_ptOk[4], "shift+encoder = fine step (~1/128)");
        check(m_ptOk[5], "FX chooser loads Reverb on T2");
        check(m_ptOk[6], "encoder edits FX param 0");
        check(m_ptOk[7], "drum track: ENC5 (macro 1) maps to Pad Attack");
        check(m_ptOk[8], "page encoders never touch dedicated params");
        check(m_ptOk[9], "SubSynth OSC+MOD pages have 4 params each");
        check(m_ptOk[10], "drum: PAD/PAD2/KIT pages, PG+ cycles");
        check(m_ptOk[11], "ADSR macro edit + shift-fine (default bind)");
        std::printf("[paramtest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --sampletest: record (generated take) → trim → assign → browser
    // → settings round-trip → dialog — through the HAL event path.
    template <typename Once>
    void sampletestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[sampletest] T2, page -> SAMPLE (4x >)\n");
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyStep0 + 1, true); onKey(gb::kKeyStep0 + 1, false); // shift+step 2 = T2
            onKey(gb::kKeyShiftL, false);
            for (int i = 0; i < 4; ++i) {
                onKey(gb::kKeyNext, true); onKey(gb::kKeyNext, false);
            }
            softKey(0); // S1 = REC
            m_stOk[0] = (m_sampleState == 1);
        }
        if (t >= 0.6 && once(1)) dumpFrame("sampletest_rec.rgb565");
        if (t >= 1.2 && once(2)) {
            softKey(1); // S2 = STOP
            float peak = 0.0f;
            for (int i = 0; i < m_takeLen; ++i)
                peak = std::max(peak, std::fabs(m_take[size_t(i)]));
            m_stOk[1] = (m_sampleState == 2) && m_takeLen > 20000 &&
                        peak > 0.01f;
            std::printf("[sampletest] take: %d frames, peak %.3f\n",
                        m_takeLen, double(peak));
        }
        if (t >= 1.4 && once(3)) {
            onEncoderDelta(0, +10); // trim start +0.10
            onEncoderDelta(1, -10); // trim end -0.10
            softKey(2);             // S3 = NORM
            m_stOk[2] = m_gain > 1.0f;
            std::printf("[sampletest] trim %.2f..%.2f gain %.2f\n",
                        double(m_trim0), double(m_trim1), double(m_gain));
        }
        if (t >= 1.6 && once(4)) {
            softKey(3); // S4 = ASSIGN
            m_stOk[3] = m_assigned && gb::dirExists("samples");
            std::printf("[sampletest] assigned=%d\n", int(m_assigned));
        }
        if (t >= 1.8 && once(5)) {
            // audition: play the assigned pad, watch the T2 meter rise
            playKey(0, true);
        }
        if (t >= 2.2 && once(6)) {
            playKey(0, false);
            m_stOk[4] = m_state.mixer.level[1] > 0.001f;
            std::printf("[sampletest] T2 meter after audition: %.4f\n",
                        double(m_state.mixer.level[1]));
        }
        if (t >= 2.4 && once(7)) {
            // LOAD page: browser lists the take, S1 loads it
            onKey(gb::kKeyNext, true); onKey(gb::kKeyNext, false);
            refreshBrowser();
            bool found = false;
            for (const auto& n : m_browserNames)
                if (n.size() > 4 && n.rfind(".wav") == n.size() - 4)
                    found = true;
            m_stOk[5] = found;
            // select the first .wav entry and load it
            auto& b = m_state.browser;
            for (int i = 0; i < b.count; ++i)
                if (!m_browserIsDir[size_t(i)]) { b.selected = i; break; }
            browserActivate();
            m_stOk[5] = m_stOk[5] && m_browserLoaded;
            dumpFrame("sampletest_browser.rgb565");
        }
        if (t >= 2.6 && once(8)) {
            // settings round-trip: set theme RED, reload from file
            onKey(gb::kKeyNext, true); onKey(gb::kKeyNext, false);
            m_state.settingsSel = 0;
            settingsAdjust(+1); // -> RED + save-on-change
            gb::setTheme(gb::kThemeMono); // simulate restart
            loadSettings();
            m_stOk[6] = (gb::currentTheme() == gb::kThemeRed);
            dumpFrame("sampletest_settings.rgb565");
        }
        if (t >= 2.8 && once(9)) {
            // confirm dialog on clear-track
            setPage(1);
            onKey(gb::kKeyShiftL, true);
            softKey(0); // shift+S1 = CLR -> confirm
            onKey(gb::kKeyShiftL, false);
            m_stOk[7] = m_state.dialogActive && m_confirmAction == 1;
            dumpFrame("sampletest_dialog.rgb565");
            onKey(gb::kKeySoft2, true); onKey(gb::kKeySoft2, false); // CANCEL
            m_stOk[7] = m_stOk[7] && m_confirmAction == 0 &&
                        !m_state.dialogActive;
        }
        if (t >= 3.0 && once(10)) {
            std::printf("[sampletest] done — asserting\n");
            m_running = false;
        }
    }

    int sampletestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[sampletest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_stOk[0], "S1 starts recording (capture running)");
        check(m_stOk[1], "stop finalizes take (>20k frames, level>0)");
        check(m_stOk[2], "trim + normalize applies gain");
        check(m_stOk[3], "assign writes WAV + loads pad");
        check(m_stOk[4], "assigned pad is audible on trigger");
        check(m_stOk[5], "browser lists + loads the take");
        check(m_stOk[6], "settings persist across save/load");
        check(m_stOk[7], "CLR confirm dialog opens + cancels");
        std::printf("[sampletest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --nsr2test: NSR-2 grid semantics — step entry, isomorphic play
    // with scale lock, MODE cycle, text entry, 32-LED path.
    template <typename Once>
    void nsr2testStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[nsr2test] MODE -> STEP; grid step entry\n");
            m_n2Ok[0] = m_nsr2;
            onKey(gb::kN2Mode, true); onKey(gb::kN2Mode, false);
            m_n2Ok[0] = m_n2Ok[0] && m_state.mode == 1;
            // row 0 (T1) steps 0 and 2; row 1 (T2) step 1
            onKey(0, true); onKey(0, false);
            onKey(2, true); onKey(2, false);
            onKey(9, true); onKey(9, false);
            // shift+> to page 2 (SEQ page needed for window paging),
            // then row 0 col 0 = step 8
            setPage(1);
            onKey(gb::kN2ShiftL, true);
            onKey(gb::kN2Next, true); onKey(gb::kN2Next, false);
            onKey(gb::kN2ShiftL, false);
            onKey(0, true); onKey(0, false);
            m_n2Ok[1] = m_pattern.steps[0][0].on &&
                        m_pattern.steps[0][2].on &&
                        m_pattern.steps[1][1].on &&
                        m_pattern.steps[0][8].on && m_window == 8;
        }
        if (t >= 0.8 && once(1)) {
            std::printf("[nsr2test] MODE -> PLAY; isomorphic notes\n");
            // NSR-2 cycle SEQ -> TEXT -> PLAY: two presses
            onKey(gb::kN2Mode, true); onKey(gb::kN2Mode, false);
            onKey(gb::kN2Mode, true); onKey(gb::kN2Mode, false);
            // grid key 16 = (row-from-bottom 1, col 0) -> E3 = 52
            onKey(16, true); onKey(16, false);
            m_n2Ok[2] = m_lastGridNote == 52;
            std::printf("[nsr2test] grid key 16 -> note %d (want 52/E3)\n",
                        m_lastGridNote);
        }
        if (t >= 1.1 && once(2)) {
            // scale lock MAJOR via SETTINGS encoder path (still PLAY)
            setPage(6);
            m_state.settingsSel = 3;
            onEncoderDelta(1, +1); // CHROM -> MAJOR
            setPage(1);
            // grid key 25 (bottom row, col 1): raw C#3 -> snap to D3
            onKey(25, true); onKey(25, false);
            m_n2Ok[3] = m_lastGridNote == 50;
            std::printf("[nsr2test] grid key 25 major-lock -> %d (want 50/D3)\n",
                        m_lastGridNote);
        }
        if (t >= 1.5 && once(3)) {
            std::printf("[nsr2test] MODE -> TEXT; type HI\n");
            // PLAY -> SEQ -> TEXT: two presses
            onKey(gb::kN2Mode, true); onKey(gb::kN2Mode, false);
            onKey(gb::kN2Mode, true); onKey(gb::kN2Mode, false);
            onKey(13, true); onKey(13, false); // H (row 1, col 5)
            onKey(7, true);  onKey(7, false);  // I (row 0, col 7)
            m_n2Ok[4] = std::strcmp(m_textBuf, "HI") == 0 &&
                        m_state.mode == 2;
            dumpFrame("nsr2test_text.rgb565");
            onKey(31, true); onKey(31, false); // ENTER -> step mode
            m_n2Ok[4] = m_n2Ok[4] && m_state.mode == 1;
        }
        if (t >= 1.9 && once(5)) {
            dumpFrame("nsr2test_seq.rgb565");
            m_n2Ok[5] = m_hal.ledMaxIndex() >= 31;
            std::printf("[nsr2test] led max index = %d\n",
                        m_hal.ledMaxIndex());
            std::printf("[nsr2test] done — asserting\n");
            m_running = false;
        }
    }

    int nsr2testVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[nsr2test] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_n2Ok[0], "MODE cycles to step mode on NSR-2");
        check(m_n2Ok[1], "grid step entry across 2 tracks + paging");
        check(m_n2Ok[2], "grid key 16 plays E3 (isomorphic)");
        check(m_n2Ok[3], "scale-lock major snaps C#3 -> D3");
        check(m_n2Ok[4], "text mode types HI, ENTER exits to step");
        check(m_n2Ok[5], "32-LED path exercised (index >= 31)");
        std::printf("[nsr2test] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --revbtest: NSR-1 rev B semantics — step row, p-lock, accent,
    // shift+step select/mute, scale lock, 32-LED path.
    template <typename Once>
    void revbtestStep(double t, Once& once) {
        using gb::kKeyStep0;
        using gb::kKeyShiftL;
        if (t >= 0.3 && once(0)) {
            std::printf("[revbtest] step row toggles steps 0/3 on T1\n");
            onKey(kKeyStep0 + 0, true); onKey(kKeyStep0 + 0, false);
            onKey(kKeyStep0 + 3, true); onKey(kKeyStep0 + 3, false);
            m_rbOk[0] = m_pattern.steps[0][0].on && m_pattern.steps[0][3].on;
        }
        if (t >= 0.5 && once(1)) {
            std::printf("[revbtest] p-lock: hold step 3, tap piano E4\n");
            onKey(kKeyStep0 + 3, true);   // hold step 3
            onKey(2, true);               // piano white 3 = E4 (also sounds)
            onKey(2, false);
            dumpFrame("revbtest_plock.rgb565"); // toast visible
            onKey(kKeyStep0 + 3, false);  // release (no toggle — edited)
            m_rbOk[1] = m_pattern.steps[0][3].on &&
                        m_pattern.steps[0][3].noteOffset == 4;
            std::printf("[revbtest] step 3 noteOffset=%d (want +4)\n",
                        int(m_pattern.steps[0][3].noteOffset));
        }
        if (t >= 0.7 && once(2)) {
            std::printf("[revbtest] accent: step 9 on, then shift+step 9\n");
            onKey(kKeyStep0 + 9, true); onKey(kKeyStep0 + 9, false);
            onKey(kKeyShiftL, true);
            onKey(kKeyStep0 + 9, true); onKey(kKeyStep0 + 9, false);
            onKey(kKeyShiftL, false);
            m_rbOk[2] = m_pattern.steps[0][9].accent;
        }
        if (t >= 0.9 && once(3)) {
            std::printf("[revbtest] shift+step select T2 + mute T1\n");
            onKey(kKeyShiftL, true);
            onKey(kKeyStep0 + 1, true); onKey(kKeyStep0 + 1, false); // T2
            onKey(kKeyStep0 + 4, true); onKey(kKeyStep0 + 4, false); // mute T1
            onKey(kKeyShiftL, false);
            m_rbOk[3] = m_state.track == 1 && m_muted[0];
            onKey(kKeyShiftL, true);
            onKey(kKeyStep0 + 4, true); onKey(kKeyStep0 + 4, false); // unmute
            onKey(kKeyShiftL, false);
            m_rbOk[3] = m_rbOk[3] && !m_muted[0];
            selectTrack(0);
        }
        if (t >= 1.1 && once(4)) {
            std::printf("[revbtest] MODE -> scale-lock MAJOR, C# snaps\n");
            onKey(gb::kKeyMode, true); onKey(gb::kKeyMode, false);
            onKey(16, true); onKey(16, false); // black 16 = C#4 (61)
            m_rbOk[4] = m_scaleLock == 0 && m_lastGridNote == 62;
            std::printf("[revbtest] C#4(61) -> %d (want 62/D4)\n",
                        m_lastGridNote);
            onKey(gb::kKeyMode, true); onKey(gb::kKeyMode, false); // back
        }
        if (t >= 1.3 && once(5)) {
            m_rbOk[5] = m_hal.ledMaxIndex() >= 31;
            std::printf("[revbtest] led max index = %d\n",
                        m_hal.ledMaxIndex());
            std::printf("[revbtest] done — asserting\n");
            m_running = false;
        }
    }

    int revbtestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[revbtest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_rbOk[0], "step row toggles steps on selected track");
        check(m_rbOk[1], "hold step + piano tap = p-lock pitch (E4)");
        check(m_rbOk[2], "shift+step 9 toggles accent");
        check(m_rbOk[3], "shift+step 2 selects T2, shift+step 5 mutes T1");
        check(m_rbOk[4], "MODE scale-lock: C#4 snaps to D4 in major");
        check(m_rbOk[5], "32-LED path exercised (index >= 31)");
        std::printf("[revbtest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --macrotest: macro encoder assign/bind/reset/persist flow.
    template <typename Once>
    void macrotestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            // fresh settings: no macros key -> factory ADSR defaults
            std::remove("settings.json");
            m_mcOk[5] = true;
            for (int i = 0; i < 4; ++i)
                m_mcOk[5] = m_mcOk[5] && m_macroParam[i] == m_adsrParam[i];
            std::printf("[macrotest] fresh defaults: macros = ADSR: %s\n",
                        m_mcOk[5] ? "yes" : "NO");
        }
        if (t >= 0.5 && once(1)) {
            // assign macro 1 (ENC5) to the OSC page's encoder-1 param
            std::printf("[macrotest] shift+push ENC5 -> assign\n");
            onKey(gb::kKeyShiftL, true);
            onEncoderPush(4, true); onEncoderPush(4, false);
            onKey(gb::kKeyShiftL, false);
            m_mcOk[0] = (m_assignMacro == 0) && m_state.toastActive;
            onEncoderDelta(0, +1); // bind: OSC page encoder 1 param
            m_mcOk[1] = m_assignMacro < 0 &&
                        m_macroParam[0] == m_encParam[0];
            std::printf("[macrotest] macro 1 bound to param %d\n",
                        m_macroParam[0]);
        }
        if (t >= 0.7 && once(2)) {
            // ENC5 now edits the bound param (relative + toast)
            const float before = getNormParam(m_macroParam[0]);
            onEncoderDelta(4, +4); // +4/64
            const float after = getNormParam(m_macroParam[0]);
            m_mcOk[2] = (after - before) > 0.04f && (after - before) < 0.09f;
            std::printf("[macrotest] ENC5 edit: %.3f -> %.3f\n",
                        double(before), double(after));
        }
        if (t >= 0.9 && once(3)) {
            // reset path: shift+push ENC5 -> assign, S1 -> factory ADSR
            onKey(gb::kKeyShiftL, true);
            onEncoderPush(4, true); onEncoderPush(4, false);
            onKey(gb::kKeyShiftL, false);
            softKey(0); // S1 while assigning = reset to ADSR
            m_mcOk[3] = m_assignMacro < 0 &&
                        m_macroParam[0] == m_adsrParam[0];
            std::printf("[macrotest] macro 1 reset to ADSR: %s\n",
                        m_mcOk[3] ? "yes" : "NO");
        }
        if (t >= 1.1 && once(4)) {
            // cancel path: enter assign, push again = cancel
            onKey(gb::kKeyShiftL, true);
            onEncoderPush(4, true); onEncoderPush(4, false);
            onKey(gb::kKeyShiftL, false);
            const bool entered = m_assignMacro == 0;
            onEncoderPush(4, true); onEncoderPush(4, false);
            m_mcOk[4] = entered && m_assignMacro < 0 &&
                        m_macroParam[0] == m_adsrParam[0];
            std::printf("[macrotest] push-again cancel: %s\n",
                        m_mcOk[4] ? "yes" : "NO");
        }
        if (t >= 1.3 && once(5)) {
            // persistence round-trip: bind macro 2 to OSC enc-2 param,
            // save happens on assign; wipe in-memory, reload from file
            onKey(gb::kKeyShiftL, true);
            onEncoderPush(5, true); onEncoderPush(5, false);
            onKey(gb::kKeyShiftL, false);
            onEncoderDelta(1, +1); // bind macro 2 -> OSC page enc 2
            const int bound = m_macroParam[1];
            for (int t2 = 0; t2 < 4; ++t2)
                for (int i = 0; i < 4; ++i)
                    m_macroParamByTrack[t2][i] = -1; // wipe
            loadSettings();
            m_mcOk[5] = m_macroParamByTrack[m_state.track][1] == bound &&
                        m_macroParam[1] == bound;
            std::printf("[macrotest] persist round-trip: bound=%d "
                        "restored=%d\n", bound, m_macroParam[1]);
        }
        if (t >= 1.5 && once(6)) {
            dumpFrame("macrotest.rgb565");
            std::printf("[macrotest] done — asserting\n");
            m_running = false;
        }
    }

    int macrotestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[macrotest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_mcOk[0], "shift+push ENC5 enters ASSIGN (toast)");
        check(m_mcOk[1], "next param touch binds macro 1");
        check(m_mcOk[2], "ENC5 edits bound param (relative + toast)");
        check(m_mcOk[3], "S1 while assigning resets to factory ADSR");
        check(m_mcOk[4], "push again cancels assign");
        check(m_mcOk[5], "settings.json persist round-trip + defaults");
        std::printf("[macrotest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --tracktest: M2d — per-track engine mapping, instrument swap,
    // MFX slot, type toggle. (Clip record/bounce = deferred, see
    // README/report.)
    template <typename Once>
    void tracktestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[tracktest] T2 kick + T3 hat steps; PLAY\n");
            for (int s : {0, 4, 8, 12}) {
                m_pattern.steps[1][s].on = true;
                m_pattern.steps[2][s].on = true;
                m_pattern.steps[1][s].vel = m_pattern.steps[2][s].vel = 110;
            }
            togglePlay();
        }
        if (t >= 1.0 && once(1)) {
            std::printf("[tracktest] mute T3 via engine\n");
            m_t3FiresAtMute = m_fires[2];
            onKey(gb::kKeyShiftL, true);
            onKey(gb::kKeyStep0 + 6, true); onKey(gb::kKeyStep0 + 6, false);
            onKey(gb::kKeyShiftL, false); // shift+step 7 = mute T3
        }
        if (t >= 1.6 && once(2)) {
            // T2 keeps firing, T3 silent at BOTH levels (scheduler skip
            // + engine mute: T3 meter stays flat while T2's moves)
            m_ttOk[0] = m_fires[1] > 0 &&
                        m_fires[2] == m_t3FiresAtMute &&
                        m_state.mixer.level[1] > 0.001f &&
                        m_state.mixer.level[2] < m_state.mixer.level[1] * 0.5f;
            std::printf("[tracktest] isolation: T2 fires=%d meter=%.4f; "
                        "T3 fires=%d meter=%.5f\n", m_fires[1],
                        double(m_state.mixer.level[1]), m_fires[2],
                        double(m_state.mixer.level[2]));
            stopTransport();
        }
        if (t >= 1.8 && once(3)) {
            std::printf("[tracktest] INST swap T1 subsynth -> fmsynth\n");
            selectTrack(0);
            setPage(0);
            chooserKey(1);                    // open
            m_state.chooserSel = 1;           // FM Synth
            chooserKey(1);                    // confirm
            auto* inst = m_engine->instrument(0);
            m_ttOk[1] = inst && std::strcmp(inst->id(), "fmsynth") == 0 &&
                        m_adsrParam[0] >= 0 &&       // "Op1 Attack" bound
                        m_macroParamByTrack[0][0] == -1; // factory reset
            std::printf("[tracktest] T1 id=%s, adsr[0]=%d, macro reset=%d\n",
                        inst ? inst->id() : "?", m_adsrParam[0],
                        m_macroParamByTrack[0][0]);
        }
        if (t >= 2.2 && once(4)) {
            std::printf("[tracktest] MFX: arpeggiator on T1\n");
            setPage(7);
            chooserKey(2);                    // open
            m_state.chooserSel = 0;           // Arpeggiator
            chooserKey(2);                    // confirm
            auto* fx = m_engine->midiEffectChain(0).effect(0);
            const float before = fx ? fx->getParameter(0) : -1.0f;
            onEncoderDelta(0, +8); // coarse enough to move a StepSelector
            const float after = fx ? fx->getParameter(0) : -1.0f;
            mfxBypassKey();
            m_ttOk[2] = fx != nullptr && after != before && fx->bypassed();
            std::printf("[tracktest] MFX param0 %.3f -> %.3f, bypassed=%d\n",
                        double(before), double(after),
                        fx ? int(fx->bypassed()) : -1);
        }
        if (t >= 2.6 && once(5)) {
            std::printf("[tracktest] T4 type toggle MIDI->AUDIO->MIDI\n");
            selectTrack(3);
            setPage(8);
            softKey(0); // MIDI -> AUDIO
            const bool audioOk = m_trackType[3] == 0 &&
                                 m_engine->instrument(3) == nullptr;
            softKey(0); // AUDIO -> MIDI
            auto* inst = m_engine->instrument(3);
            m_ttOk[3] = audioOk && m_trackType[3] == 1 && inst &&
                        std::strcmp(inst->id(), "drumrack") == 0;
            std::printf("[tracktest] T4 type=%d inst=%s\n",
                        int(m_trackType[3]), inst ? inst->id() : "(none)");
            std::printf("[tracktest] done — asserting\n");
            m_running = false;
        }
    }

    int tracktestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[tracktest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_ttOk[0], "engine-level mute isolation (T3 flat, T2 plays)");
        check(m_ttOk[1], "INST swap subsynth->fmsynth + rebind + macro reset");
        check(m_ttOk[2], "MFX arpeggiator add + param edit + bypass");
        check(m_ttOk[3], "T4 type toggle MIDI<->AUDIO (inst removed/restored)");
        std::printf("[tracktest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --padtest: last-played-pad parameter editing. T1 -> drumsynth;
    // playing a voice makes it the edit pad (INST bindings + toast
    // follow); per-voice params retain edits across pad switches;
    // sequencer fire never steals the edit pad; DrumRack selectedPad
    // follows through the instrument's own setter.
    template <typename Once>
    void padtestStep(double t, Once& once) {
        auto inst = [&]() { return m_engine->instrument(0); };
        if (t >= 0.3 && once(0)) {
            // Swap T1 -> drumsynth via the real chooser path.
            m_state.track = 0;
            m_chooserKind = 1;
            m_state.chooserOpen = true;
            const auto& descs = yawn::instrumentDescriptors();
            int sel = -1;
            for (int i = 0; i < int(descs.size()); ++i)
                if (std::strcmp(descs[i].id, "drumsynth") == 0) sel = i;
            m_state.chooserSel = sel;
            chooserConfirm();
            m_padOk[0] = m_trackInstId[0] == "drumsynth" &&
                         m_drumVoiceNotes[0].size() == 8 &&
                         m_editPad[0] == 0;
            std::printf("[padtest] T1 -> drumsynth, edit pad %d, "
                        "PAD page params:", m_editPad[0]);
            for (int i = 0; i < 4 && m_encParam[i] >= 0; ++i)
                std::printf(" %s",
                            inst()->parameterInfo(m_encParam[i]).name);
            std::printf("\n");
        }
        if (t >= 0.6 && once(1)) {
            // Play snare (black key 1 = strip pos 1) -> EDIT SNARE.
            onKey(gb::kKeyBlack0 + 0, true);
            onKey(gb::kKeyBlack0 + 0, false);
            const bool names =
                std::strcmp(inst()->parameterInfo(m_adsrParam[0]).name,
                            "Atk") == 0 &&
                std::strcmp(inst()->parameterInfo(m_encParam[0]).name,
                            "Tune") == 0;
            std::printf("[padtest] snare: editPad=%d toast=\"%s\" "
                        "A-idx=%d PAD0-idx=%d (%s/%s)\n", m_editPad[0],
                        m_state.toastValue, m_adsrParam[0], m_encParam[0],
                        inst()->parameterInfo(m_adsrParam[0]).name,
                        inst()->parameterInfo(m_encParam[0]).name);
            m_padOk[1] = m_editPad[0] == 1 &&
                         std::strcmp(m_state.toastValue, "SNARE") == 0 &&
                         m_adsrParam[0] == drumSynthBase(1) + 1 &&
                         m_encParam[0] == drumSynthBase(1) && names;
        }
        if (t >= 0.9 && once(2)) {
            // Edit snare Tune (PAD page encoder 1), then switch to kick:
            // bindings must follow, snare edit must be retained.
            const float tune0 = inst()->getParameter(drumSynthBase(1));
            onEncoderDelta(0, +4); // +4/16 norm on snare Tune
            const float tune1 = inst()->getParameter(drumSynthBase(1));
            onKey(gb::kKeyWhite0 + 0, true);  // kick
            onKey(gb::kKeyWhite0 + 0, false);
            const float snareTuneAfter =
                inst()->getParameter(drumSynthBase(1));
            std::printf("[padtest] snare tune %.3f -> %.3f; kick: "
                        "editPad=%d toast=\"%s\" A-idx=%d PAD0-idx=%d; "
                        "snare tune retained=%d\n", double(tune0),
                        double(tune1), m_editPad[0], m_state.toastValue,
                        m_adsrParam[0], m_encParam[0],
                        int(snareTuneAfter == tune1));
            m_padOk[2] = tune1 > tune0 && m_editPad[0] == 0 &&
                         std::strcmp(m_state.toastValue, "KICK") == 0 &&
                         m_adsrParam[0] == 1 && m_encParam[0] == 0 &&
                         snareTuneAfter == tune1;
        }
        if (t >= 1.2 && once(3)) {
            // Macro A (factory binding) edits the CURRENT pad's attack:
            // kick Atk moves, snare Atk untouched.
            const float kickA0 = inst()->getParameter(1);
            const float snareA0 = inst()->getParameter(8);
            onEncoderDelta(4, +8); // macro 1, +8/64 norm
            const float kickA1 = inst()->getParameter(1);
            const float snareA1 = inst()->getParameter(8);
            std::printf("[padtest] macro A: kick atk %.3f -> %.3f, "
                        "snare atk %.3f -> %.3f (macro idx=%d)\n",
                        double(kickA0), double(kickA1), double(snareA0),
                        double(snareA1), m_macroParam[0]);
            m_padOk[3] = m_macroParam[0] == 1 && kickA1 > kickA0 &&
                         snareA1 == snareA0;
        }
        if (t >= 1.5 && once(4)) {
            // Hold-step voice p-lock also sets the edit pad.
            onStepRowKey(0, true);
            onKey(gb::kKeyWhite0 + 4, true);  // strip pos 7 = tambourine
            onKey(gb::kKeyWhite0 + 4, false);
            onStepRowKey(0, false);
            std::printf("[padtest] p-lock: noteOffset=%d editPad=%d "
                        "A-idx=%d (want 7/7/%d)\n",
                        int(m_pattern.steps[0][0].noteOffset),
                        m_editPad[0], m_adsrParam[0],
                        drumSynthBase(7) + 1);
            m_padOk[4] = int(m_pattern.steps[0][0].noteOffset) == 7 &&
                         m_editPad[0] == 7 &&
                         m_adsrParam[0] == drumSynthBase(7) + 1;
        }
        if (t >= 1.8 && once(5)) {
            // Sequencer fire does NOT steal the edit pad.
            m_pattern.steps[0][1].on = true;
            m_pattern.steps[0][1].noteOffset = 0; // kick
            fireStep(0, 1, m_pattern.steps[0][1], 0.0);
            std::printf("[padtest] after step fire: editPad=%d (want 7)\n",
                        m_editPad[0]);
            m_padOk[5] = m_editPad[0] == 7;
        }
        if (t >= 2.1 && once(6)) {
            // DrumRack (T2): load a second pad, play it, selectedPad
            // follows via the instrument's own setter.
            selectTrack(1);
            auto* rack = dynamic_cast<yawn::instruments::DrumRack*>(
                m_engine->instrument(1));
            auto smp = makeDrumSample(1, m_engine->sampleRate());
            if (rack)
                rack->loadPad(38, smp.data(), int(smp.size()) / 2, 2);
            rebuildDrumVoices(1);
            std::printf("[padtest] T2 voices (%zu):",
                        m_drumVoiceNotes[1].size());
            for (int n : m_drumVoiceNotes[1]) std::printf(" %d", n);
            std::printf("\n");
            onKey(gb::kKeyBlack0 + 0, true);  // pos 1 -> pad 38
            onKey(gb::kKeyBlack0 + 0, false);
            const bool follow38 = rack && rack->selectedPad() == 38 &&
                                  m_editPad[1] == 1 &&
                                  std::strcmp(m_state.toastValue,
                                              "PAD38") == 0;
            std::printf("[padtest] T2 play pad38: selectedPad=%d "
                        "editPad=%d toast=\"%s\"\n",
                        rack ? rack->selectedPad() : -1, m_editPad[1],
                        m_state.toastValue);
            // sequencer fire (voice 0) must not steal; white1 -> 36.
            m_pattern.steps[1][0].on = true;
            m_pattern.steps[1][0].noteOffset = 0;
            fireStep(1, 0, m_pattern.steps[1][0], 0.0);
            const bool noSteal = m_editPad[1] == 1 &&
                                 rack->selectedPad() == 38;
            onKey(gb::kKeyWhite0 + 0, true);
            onKey(gb::kKeyWhite0 + 0, false);
            const bool follow36 = rack->selectedPad() == 36 &&
                                  m_editPad[1] == 0;
            std::printf("[padtest] T2: fire-steal=%d, white1 -> "
                        "selectedPad=%d editPad=%d\n", int(!noSteal),
                        rack ? rack->selectedPad() : -1, m_editPad[1]);
            m_padOk[6] = m_drumVoiceNotes[1].size() == 2 && follow38;
            m_padOk[7] = noSteal && follow36;
            m_running = false;
        }
    }

    int padtestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[padtest] ASSERT %-48s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_padOk[0], "INST swap T1->drumsynth, edit pad = kick");
        check(m_padOk[1], "play snare -> EDIT SNARE + snare bindings");
        check(m_padOk[2], "pad switch to kick, snare tune retained");
        check(m_padOk[3], "macro A edits current pad attack only");
        check(m_padOk[4], "hold-step voice p-lock sets edit pad");
        check(m_padOk[5], "sequencer fire does not steal edit pad");
        check(m_padOk[6], "DrumRack: pad load + play -> selectedPad");
        check(m_padOk[7], "DrumRack: no fire-steal, back to lane pad");
        std::printf("[padtest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --drumtest: drum key map. Baseline chromatic check on T1=subsynth,
    // then swap T1 -> drumsynth through the real INST chooser path and
    // sweep all 27 piano keys asserting every note-on is a real DrumSynth
    // voice (yawn's own slotForNote is the acceptance condition the
    // instrument itself uses). Then: wraparound, sequencer defaults,
    // hold-step voice p-lock, encoder voice cycling, DrumRack track
    // sanity, non-drum track unaffected.
    template <typename Once>
    void drumtestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            // Baseline: T1 = subsynth, key row is chromatic from C4.
            m_dtOk[6] = !isDrumTrack(0);
            onKey(gb::kKeyWhite0 + 0, true);  // white 1 = C4
            const int n0 = m_lastNoteOn;
            onKey(gb::kKeyWhite0 + 0, false);
            onKey(gb::kKeyBlack0 + 0, true);  // black 1 = C#4
            const int n1 = m_lastNoteOn;
            onKey(gb::kKeyBlack0 + 0, false);
            std::printf("[drumtest] baseline subsynth: white1=%d (want 60)"
                        " black1=%d (want 61)\n", n0, n1);
            m_dtOk[0] = (n0 == 60 && n1 == 61);
        }
        if (t >= 0.6 && once(1)) {
            // Swap T1 -> drumsynth via the real chooser path.
            m_state.track = 0;
            m_chooserKind = 1; // INST
            m_state.chooserOpen = true;
            const auto& descs = yawn::instrumentDescriptors();
            int sel = -1;
            for (int i = 0; i < int(descs.size()); ++i)
                if (std::strcmp(descs[i].id, "drumsynth") == 0) sel = i;
            std::printf("[drumtest] INST swap T1 -> drumsynth (sel=%d)\n",
                        sel);
            m_state.chooserSel = sel;
            chooserConfirm();
            m_dtOk[1] = m_trackInstId[0] == "drumsynth" &&
                        m_drumVoiceNotes[0].size() ==
                            size_t(yawn::instruments::DrumSynth::kNumDrums);
            std::printf("[drumtest] voice list (%zu):",
                        m_drumVoiceNotes[0].size());
            for (int n : m_drumVoiceNotes[0]) std::printf(" %d", n);
            std::printf("\n");
        }
        if (t >= 0.9 && once(2)) {
            // Sweep all 27 piano keys through the HAL path; every
            // note-on must be a real DrumSynth voice.
            bool allValid = true;
            bool hit[yawn::instruments::DrumSynth::kNumDrums] = {};
            for (int k = 0; k <= 26; ++k) {
                onKey(k, true);
                const int note = m_lastNoteOn;
                onKey(k, false);
                const int slot =
                    yawn::instruments::DrumSynth::slotForNote(note);
                char vb[16];
                std::printf("[drumtest] key %2d (pos %2d) -> note %3d "
                            "voice %-6s slot %d\n", k, pianoPhysPos(k),
                            note,
                            drumVoiceName(0, pianoPhysPos(k), vb,
                                          sizeof(vb)),
                            slot);
                if (slot < 0) allValid = false;
                else hit[slot] = true;
                if (k == 26) // wraparound: pos 25 -> 25 % 8 = slot 1
                    m_dtOk[3] = slot == 1 &&
                                note == yawn::instruments::DrumSynth::
                                            kDrumNotes[1];
            }
            int distinct = 0;
            for (bool h : hit) if (h) ++distinct;
            m_dtVoicesHit = distinct;
            m_dtOk[2] = allValid;
            std::printf("[drumtest] sweep: all-valid=%d distinct-voices"
                        "=%d/8\n", int(allValid), distinct);
        }
        if (t >= 1.2 && once(3)) {
            // Sequencer: fresh step on a drum track defaults to voice 0
            // (kick); hold-step + piano tap p-locks a voice; encoder
            // cycles voices with wrap.
            toggleStep(0, 0);
            m_dtOk[4] = stepNote(0, 0) ==
                yawn::instruments::DrumSynth::kDrumNotes[0];
            std::printf("[drumtest] fresh step note=%d (want 36 kick)\n",
                        stepNote(0, 0));
            // hold step 1, tap white key 5 (pos 7 -> voice 7 = tamb)
            onStepRowKey(0, true);
            onKey(gb::kKeyWhite0 + 4, true);
            onKey(gb::kKeyWhite0 + 4, false);
            onStepRowKey(0, false); // heldEdited -> no toggle
            m_dtOk[5] = int(m_pattern.steps[0][0].noteOffset) == 7 &&
                        stepNote(0, 0) == 54;
            std::printf("[drumtest] p-lock: noteOffset=%d (want 7) "
                        "stepNote=%d (want 54 tamb)\n",
                        int(m_pattern.steps[0][0].noteOffset),
                        stepNote(0, 0));
            // encoder +1 wraps voice 7 -> 0 (kick), toast shows name
            onStepRowKey(0, true);
            onEncoderDelta(0, +1);
            onStepRowKey(0, false);
            m_dtOk[5] = m_dtOk[5] &&
                        int(m_pattern.steps[0][0].noteOffset) == 0 &&
                        stepNote(0, 0) == 36 &&
                        std::strcmp(m_state.toastValue, "KICK") == 0;
            std::printf("[drumtest] enc cycle: noteOffset=%d stepNote=%d"
                        " toast=\"%s\" (want 0/36/KICK)\n",
                        int(m_pattern.steps[0][0].noteOffset),
                        stepNote(0, 0), m_state.toastValue);
        }
        if (t >= 1.5 && once(4)) {
            // DrumRack lane (T2, one loaded pad @36): every key wraps to
            // that pad; melodic-inst check above covers non-drum.
            selectTrack(1);
            m_dtOk[7] = m_drumVoiceNotes[1].size() == 1 &&
                        m_drumVoiceNotes[1][0] == 36;
            onKey(gb::kKeyWhite0 + 0, true);
            const int na = m_lastNoteOn;
            onKey(gb::kKeyWhite0 + 0, false);
            onKey(gb::kKeyBlack0 + 10, true); // last key
            const int nb = m_lastNoteOn;
            onKey(gb::kKeyBlack0 + 10, false);
            std::printf("[drumtest] T2 drumrack: voices=%zu key1=%d "
                        "key27=%d (want 1/36/36)\n",
                        m_drumVoiceNotes[1].size(), na, nb);
            m_dtOk[7] = m_dtOk[7] && na == 36 && nb == 36;
            m_running = false;
        }
    }

    int drumtestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[drumtest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_dtOk[0], "baseline: subsynth key row chromatic (C4/C#4)");
        check(m_dtOk[1], "INST swap T1->drumsynth builds 8-voice list");
        check(m_dtOk[2], "all 27 keys trigger a real DrumSynth voice");
        check(m_dtVoicesHit == 8, "sweep covers all 8 DrumSynth voices");
        check(m_dtOk[3], "key 27 wraps to a valid voice (snare 38)");
        check(m_dtOk[4], "fresh drum step defaults to voice 0 (kick 36)");
        check(m_dtOk[5], "hold-step p-lock + encoder voice cycle (+toast)");
        check(m_dtOk[6], "T1 not a drum track before the swap");
        check(m_dtOk[7], "T2 drumrack: single pad, all keys wrap to 36");
        std::printf("[drumtest] %d/%d assertions PASS\n", pass, pass + fail);
        return fail == 0 ? 0 : 1;
    }

    // --bouncetest: AUDIO-track clip record (test capture source),
    // bounce T1 pattern -> WAV -> clip on first AUDIO track + mute src,
    // FX 2-slot independence.
    template <typename Once>
    void bouncetestStep(double t, Once& once) {
        if (t >= 0.3 && once(0)) {
            std::printf("[bouncetest] T4 -> AUDIO (TRACK page)\n");
            for (int s : {0, 4, 8, 12}) { // T1 4-floor C4 for the bounce
                m_pattern.steps[0][s].on = true;
                m_pattern.steps[0][s].vel = 110;
            }
            selectTrack(3);
            setPage(8);
            softKey(0); // MIDI -> AUDIO
            m_btOk[0] = m_trackType[3] == 0 &&
                        m_engine->instrument(3) == nullptr;
        }
        if (t >= 0.5 && once(1)) {
            std::printf("[bouncetest] REC arm + PLAY (clip record)\n");
            toggleRec();
            togglePlay(); // -> recordArmAudioStart
            m_btOk[1] = m_clipRecording;
        }
        if (t >= 1.8 && once(2)) {
            std::printf("[bouncetest] STOP -> clip finalize\n");
            stopTransport();
            // non-silent check
            float peak = 0.0f;
            if (m_clipBuf[3])
                for (int i = 0; i < m_clipBuf[3]->numFrames(); ++i)
                    peak = std::max(peak,
                                    std::fabs(m_clipBuf[3]->sample(0, i)));
            togglePlay(); // clip loops with transport
            m_btOk[2] = m_clipActive[3] && m_clipRecorded && peak > 0.01f;
            m_clipPeak = peak;
            std::printf("[bouncetest] clip peak %.3f\n", double(peak));
        }
        if (t >= 2.3 && once(3)) {
            const auto& cs = m_engine->clipEngine().trackState(3);
            std::printf("[bouncetest] diag: active=%d stopping=%d "
                        "playPos=%lld clip=%p\n", int(cs.active),
                        int(cs.stopping), (long long)cs.playPosition,
                        (const void*)cs.clip);
            m_btOk[3] = m_engine->clipEngine().trackState(3).active &&
                        m_state.mixer.level[3] > 0.0005f;
            std::printf("[bouncetest] clip active=%d meter=%.4f\n",
                        int(m_engine->clipEngine().trackState(3).active),
                        double(m_state.mixer.level[3]));
        }
        if (t >= 2.6 && once(4)) {
            std::printf("[bouncetest] FX 2 slots on T1: reverb + delay\n");
            stopTransport();
            selectTrack(0);
            setPage(3);
            chooserKey(0);                  // open
            m_state.chooserSel = 0;         // Reverb
            chooserKey(0);                  // confirm -> slot 0
            onKey(gb::kKeyShiftL, true);
            softKey(1);                     // shift+S2 = slot 2
            onKey(gb::kKeyShiftL, false);
            chooserKey(0);
            m_state.chooserSel = 1;         // Delay
            chooserKey(0);
            auto& chain = m_engine->mixer().trackEffects(0);
            auto* fx0 = chain.effectAt(0);
            auto* fx1 = chain.effectAt(1);
            const float rBefore = fx0 ? fx0->getParameter(0) : -1.0f;
            const float dBefore = fx1 ? fx1->getParameter(0) : -1.0f;
            onEncoderDelta(0, +2);          // edits slot 1 (Delay)
            const float rAfter = fx0 ? fx0->getParameter(0) : -1.0f;
            const float dAfter = fx1 ? fx1->getParameter(0) : -1.0f;
            fxBypassKey();                  // bypass slot 1 only
            m_btOk[4] = fx0 && fx1 &&
                        std::strcmp(fx0->id(), "reverb") == 0 &&
                        std::strcmp(fx1->id(), "delay") == 0 &&
                        dAfter != dBefore && rAfter == rBefore &&
                        fx1->bypassed() && !fx0->bypassed();
            std::printf("[bouncetest] slot0=%s slot1=%s dParam %.3f->%.3f "
                        "byp %d/%d\n", fx0 ? fx0->name() : "?",
                        fx1 ? fx1->name() : "?", double(dBefore),
                        double(dAfter), fx0 ? int(fx0->bypassed()) : -1,
                        fx1 ? int(fx1->bypassed()) : -1);
        }
        if (t >= 3.4 && once(5)) {
            std::printf("[bouncetest] bounce T1 -> T4 (TRACK page S3)\n");
            selectTrack(0);
            setPage(8);
            softKey(2); // BOUNCE
            m_btOk[5] = m_bounceDone && gb::dirExists("samples") &&
                        m_clipActive[3] && m_muted[0];
            std::printf("[bouncetest] bounceDone=%d clip=%d muted0=%d\n",
                        int(m_bounceDone), int(m_clipActive[3]),
                        int(m_muted[0]));
        }
        if (t >= 4.4 && once(6)) {
            // bounce clip playing on T4: meter moves
            m_btOk[6] = m_state.mixer.level[3] > 0.0005f;
            std::printf("[bouncetest] T4 meter after bounce: %.4f\n",
                        double(m_state.mixer.level[3]));
            std::printf("[bouncetest] done — asserting\n");
            m_running = false;
        }
    }

    int bouncetestVerdict() const {
        int pass = 0, fail = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("[bouncetest] ASSERT %-46s %s\n", what,
                        ok ? "PASS" : "FAIL");
            ok ? ++pass : ++fail;
        };
        check(m_btOk[0], "T4 type MIDI->AUDIO (instrument removed)");
        check(m_btOk[1], "REC arm + PLAY starts clip capture");
        check(m_btOk[2], "stop finalizes non-silent clip");
        check(m_btOk[3], "clip loops with transport (active + meter)");
        check(m_btOk[4], "FX slots 0/1 independent (edit + bypass)");
        check(m_btOk[5], "bounce T1->T4: WAV + clip + T1 muted");
        check(m_btOk[6], "bounced clip plays (T4 meter moves)");
        std::printf("[bouncetest] %d/%d assertions PASS\n", pass, pass + fail);
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

    // --profile N: heavy-load profiling harness. Dense 16-step pattern
    // on all 4 tracks (vel 120), 140 BPM, reverb inserted on T1+T2.
    // The normal scheduler (updateSequencer) drives the pattern while
    // the display path (render + presentFrame) stays hot — this mirrors
    // worst-case panel load on the Pi. Once per second: PortAudio CPU
    // load % + the PA callback status flags (xrun counters: bit 0x4 =
    // output underflow, 0x8 = output overflow; see paStreamCallbackFlags).
    int runProfile(int seconds) {
        std::printf("[profile] heavy load: 4 tracks x 16 steps (vel 120), "
                    "140 BPM, reverb on T1+T2, %d s\n", seconds);
        for (int t = 0; t < 4; ++t)
            for (int s = 0; s < 16; ++s) {
                auto& st = m_pattern.steps[t][s];
                st.on = true; st.vel = 120;
            }
        m_pattern.length = 16;
        for (int t = 0; t < 2; ++t) {
            auto fx = yawn::createAudioEffect("reverb");
            std::printf("[profile] reverb %s on T%d\n",
                        fx ? "inserted" : "FAILED", t + 1);
            if (fx)
                m_engine->mixer().trackEffects(gb::Pattern::engineTrack(t))
                    .insert(0, std::move(fx));
        }
        m_state.bpm = 140.0f;
        m_engine->sendCommand(yawn::audio::TransportSetBPMMsg{140.0});
        m_engine->sendCommand(yawn::audio::TransportPlayMsg{});
        const auto t0 = std::chrono::steady_clock::now();
        int sec = 0;
        uint32_t xrunFlags = 0; // accumulated under/overflow bits
        while (m_running && sec < seconds) {
            m_hal.poll();
            m_engine->pollRetirements();
            yawn::audio::AudioEvent ev;
            while (m_engine->pollEvent(ev)) {}
            updateSequencer();
            m_ui.render(m_state, m_fb);
            m_hal.presentFrame(m_fb);
            const int el = int(std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count());
            if (el > sec) {
                sec = el;
                const uint32_t fl = m_engine->consumeCallbackStatusFlags();
                xrunFlags |= fl & 0x0F; // under/overflow bits only
                std::printf("[profile] t=%2ds  cpu=%5.1f%%  "
                            "callback-flags=0x%02x %s\n", sec,
                            m_engine->cpuLoad() * 100.0, fl,
                            (fl & 0x0C) ? "(XRUN)" : "");
                std::fflush(stdout);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        m_engine->sendCommand(yawn::audio::TransportStopMsg{});
        std::printf("[profile] done: %d s sampled, accumulated xrun bits "
                    "0x%x (%s)\n", sec, xrunFlags,
                    xrunFlags ? "under/overflow occurred" : "clean");
        return 0;
    }

    HalBackend m_hal;
    gb::Ui m_ui;
    gb::UiState m_state;
    uint16_t m_fb[gb::kDisplayW * gb::kDisplayH] = {};
    std::unique_ptr<yawn::audio::AudioEngine> m_engine;

    gb::Pattern m_pattern;
    bool m_muted[4] = {};
    float m_vol[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool m_recArmed = false;

    // SAMPLE page
    std::unique_ptr<gb::CaptureSource> m_capture;
    std::vector<float> m_take;
    int m_takeLen = 0;
    int m_sampleState = 0; // 0 idle, 1 recording, 2 review
    float m_trim0 = 0.0f, m_trim1 = 1.0f, m_gain = 1.0f;

    // LOAD page
    std::string m_browserDir = "samples";
    std::vector<std::string> m_browserNames;
    std::vector<const char*> m_browserPtrs;
    bool m_browserIsDir[256] = {}; // vector<bool> has no .data()
    std::error_code m_fsEc;

    // SETTINGS
    int m_velSource = 0;      // 0=slider 1=fixed100 2=host(last used)
    int m_hostVel = 100;
    int m_ledBrightness = 8;  // stub for the panel
    char m_ledValBuf[8] = {};

    // confirm dialog (0=none 1=clear track 2=quit)
    int m_confirmAction = 0;

    // sampletest assertions
    bool m_assigned = false, m_browserLoaded = false;

    // parameter bindings (M2b)
    int m_potParam[2] = {-1, -1};
    bool m_potEngaged[2] = {};
    float m_potPrev[2] = {};

    // track state (M2d)
    uint8_t m_trackType[4] = {1, 1, 1, 1}; // 0=AUDIO 1=MIDI
    std::string m_trackInstId[4] = {"subsynth", "drumrack", "drumrack",
                                    "drumrack"};
    float m_trackVol = 1.0f, m_trackPan = 0.0f;
    int m_trackInputCh = 0;
    bool m_trackMonitor = false;
    int m_fxSlot = 0;                     // FX page: insert slot 0/1
    int m_chooserKind = 0;                // 0=FX 1=INST 2=MFX
    std::vector<const char*> m_instItems, m_mfxItems;
    // clips (record + bounce targets)
    std::shared_ptr<yawn::audio::AudioBuffer> m_clipBuf[4];
    yawn::audio::Clip m_clip[4];
    bool m_clipActive[4] = {};
    bool m_clipRecording = false;
    bool m_clipPending[4] = {};   // finalized take, launched on next PLAY
    bool m_clipRecorded = false;
    bool m_bounceDone = false;
    int64_t m_bounceLast = -1;
    struct { bool active; int note; double offBeat; } m_bounceLive =
        {false, 0, 0.0};
    int m_adsrParam[4] = {-1, -1, -1, -1}; // factory ADSR bindings
    // Macro encoders (5-8): per-track assignment (-1 = factory ADSR)
    int m_macroParamByTrack[4][4] = {};
    int m_macroParam[4] = {-1, -1, -1, -1}; // resolved for sel track
    char m_macroLabel[4][10] = {};
    int m_assignMacro = -1;
    std::chrono::steady_clock::time_point m_assignTime{};
    EncPage m_encPages[3];
    int m_encParam[4] = {-1, -1, -1, -1};
    int m_paramPage = 0;
    char m_encLabelBuf[4][10] = {};
    std::vector<const char*> m_fxItems; // chooser: descriptors + NONE

    // scheduler state
    struct LiveNote { bool active = false; int note = 0; double offBeat = 0.0; };
    LiveNote m_live[4];
    int64_t m_lastStep[4] = {-1, -1, -1, -1};

    int m_baseNote = 60; // C4
    float m_slider = 0.8f;
    bool m_shiftL = false, m_shiftR = false;
    bool m_running = true;
    int m_testMode = 0;
    int m_profileSecs = 0; // --profile N: heavy-load harness duration
    bool m_testDone[16] = {};

    // hold-step edit gesture (SEQ mode)
    int m_heldStep = -1;
    int m_heldRow = -1;      // NSR-2 grid row of the held key
    bool m_heldEdited = false;

    // NSR-2 panel profile state
    bool m_nsr2 = false;
    int m_winSize = 16;      // step window: 16 NSR-1, 8 NSR-2
    bool m_gridNoteHeld[32] = {};
    char m_textBuf[32] = {};
    int m_textLen = 0;
    int m_scaleLock = 2;     // 0=major 1=minor 2=chromatic

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
    bool m_probeStepOk = false;
    bool m_ptOk[12] = {}; // paramtest assertions
    bool m_stOk[8] = {};  // sampletest assertions
    bool m_n2Ok[6] = {};  // nsr2test assertions
    bool m_rbOk[6] = {};  // revbtest assertions
    bool m_mcOk[6] = {};  // macrotest assertions
    bool m_ttOk[4] = {};  // tracktest assertions
    bool m_btOk[7] = {};  // bouncetest assertions
    float m_clipPeak = 0.0f;
    int m_t3FiresAtMute = 0;
    int m_lastGridNote = -1;

    // Drum voice mapping (see rebuildDrumVoices): per-track voice list
    // for drumsynth/drumrack/drumslop; piano keys walk it with wrap.
    std::vector<int> m_drumVoiceNotes[4];
    // Last-played-pad editing: voice INDEX of the pad the INST page /
    // ADSR macros edit on a drum track. Follows manual key play and
    // hold-step voice edits — never sequencer triggers.
    int m_editPad[4] = {};
    char m_padNameBuf[4][16] = {};
    int m_lastNoteOn = -1;    // last note-on sent (drumtest)
    bool m_dtOk[8] = {};      // drumtest assertions
    bool m_padOk[8] = {};     // padtest assertions
    int m_dtVoicesHit = 0;    // distinct voices across the 27-key sweep
};

namespace {

// --paneldump: render one panel frame (with a real device screen page
// and some demo control activity) to a PNG, no window/engine needed.
// profile: kPanelNSR1 → NSR-1 faceplate, kPanelNSR2 → NSR-2.
bool writePanelDumpPng(const char* path, gb::PanelProfile profile) {
    // device screen: the default SYNTH page
    gb::Ui ui;
    gb::UiState state; // defaults: SYNTH page, params, T1 120
    uint16_t screen[gb::kDisplayW * gb::kDisplayH];
    ui.render(state, screen);

    gb::PanelState ps;
    ps.keyDown[gb::kKeyPlay] = true;          // PLAY held
    ps.keyDown[gb::kKeyWhite0 + 4] = true;    // white 5 held
    const float pots[2] = {87.f / 127.f, 41.f / 127.f};
    for (int i = 0; i < 2; ++i) ps.pots[i] = pots[i];
    ps.slider = 0.8f;
    ps.joyX = 0.3f; ps.joyY = 0.2f;
    for (int i = 0; i < gb::kMaxLeds; i += 4) ps.leds[i] = true;

    const int pw = profile == gb::kPanelNSR2 ? gb::PanelViewNSR2::kW
                                             : gb::PanelView::kW;
    const int ph = profile == gb::kPanelNSR2 ? gb::PanelViewNSR2::kH
                                             : gb::PanelView::kH;
    if (profile == gb::kPanelNSR2) {          // highlight grid key 0
        ps.activeType = gb::PanelView::kHitKey;
        ps.activeIndex = 0;
        ps.keyDown[gb::kN2Play] = true;
        ps.keyDown[gb::kN2Rec] = true;
    } else {
        ps.activeType = gb::PanelView::kHitPot; // highlight CUT knob
        ps.activeIndex = 0;
    }

    std::vector<uint16_t> fb(size_t(pw) * ph);
    gb::Canvas565 c{fb.data(), pw, ph};
    gb::Font5x7 font;
    if (profile == gb::kPanelNSR2)
        gb::PanelViewNSR2::render(c, font, ps, screen);
    else
        gb::PanelView::render(c, font, ps, screen);

    std::vector<unsigned char> rgb(fb.size() * 3);
    for (size_t i = 0; i < fb.size(); ++i) {
        const uint16_t p = fb[i];
        rgb[i * 3 + 0] = uint8_t(((p >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = uint8_t(((p >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = uint8_t((p & 0x1F) * 255 / 31);
    }
    const int ok = stbi_write_png(path, pw, ph, 3, rgb.data(), pw * 3);
    if (ok) std::printf("[paneldump] wrote %s (%dx%d)\n", path, pw, ph);
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
    std::setvbuf(stdout, nullptr, _IONBF, 0); // crash-safe logging
#ifdef _WIN32
    // Dev crash handler: print exception + symbolicated stack.
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        std::printf("\n!!! CRASH 0x%08X at %p (data %s %p)\n",
                    ep->ExceptionRecord->ExceptionCode,
                    ep->ExceptionRecord->ExceptionAddress,
                    ep->ExceptionRecord->ExceptionInformation[0] ? "write"
                                                                 : "read",
                    (void*)ep->ExceptionRecord->ExceptionInformation[1]);
        HANDLE proc = GetCurrentProcess();
        SymInitialize(proc, nullptr, TRUE);
        void* stack[32];
        const USHORT n = CaptureStackBackTrace(0, 32, stack, nullptr);
        auto* si = static_cast<SYMBOL_INFO*>(
            malloc(sizeof(SYMBOL_INFO) + 256));
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        for (int i = 0; i < n; ++i)
            if (SymFromAddr(proc, DWORD64(stack[i]), nullptr, si))
                std::printf("  %s+0x%llx\n", si->Name,
                            DWORD64(stack[i]) - si->Address);
        free(si);
        std::fflush(stdout);
        return EXCEPTION_EXECUTE_HANDLER;
    });
#endif
    gb::PanelProfile panel = gb::kPanelNSR1;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--panel=", 8) == 0) {
            panel = std::strcmp(argv[i] + 8, "nsr2") == 0 ? gb::kPanelNSR2
                                                          : gb::kPanelNSR1;
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("groovebox_sim %s\n", GB_VERSION_STRING);
            return 0;
        }
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
            return writePanelDumpPng(out, panel) ? 0 : 1;
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
    int profileSecs = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0)      testMode = 1;
        if (std::strcmp(argv[i], "--seqtest") == 0)    testMode = 2;
        if (std::strcmp(argv[i], "--uitest") == 0)     testMode = 3;
        if (std::strcmp(argv[i], "--longtest") == 0)   testMode = 4;
        if (std::strcmp(argv[i], "--panelprobe") == 0) testMode = 5;
        if (std::strcmp(argv[i], "--paramtest") == 0)  testMode = 6;
        if (std::strcmp(argv[i], "--sampletest") == 0) testMode = 7;
        if (std::strcmp(argv[i], "--nsr2test") == 0)   testMode = 8;
        if (std::strcmp(argv[i], "--revbtest") == 0)   testMode = 9;
        if (std::strcmp(argv[i], "--macrotest") == 0)  testMode = 10;
        if (std::strcmp(argv[i], "--tracktest") == 0)  testMode = 11;
        if (std::strcmp(argv[i], "--bouncetest") == 0) testMode = 12;
        if (std::strcmp(argv[i], "--drumtest") == 0)   testMode = 13;
        if (std::strcmp(argv[i], "--padtest") == 0)    testMode = 14;
        if (std::strcmp(argv[i], "--profile") == 0 && i + 1 < argc)
            profileSecs = std::atoi(argv[i + 1]);
    }
    App app;
    app.setPanelProfile(panel);
    app.setProfileSeconds(profileSecs);
    return app.run(testMode);
}
