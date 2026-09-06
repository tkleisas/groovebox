#include "SimBackend.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

namespace gb {

// Screen view: 240x160 framebuffer through a 480x320 logical viewport
// (integer 2x, nearest) letterboxed in the window. Panel view: the
// 1600x600 panel canvas letterboxed likewise.
static constexpr int kLogicalW = kDisplayW * 2;
static constexpr int kLogicalH = kDisplayH * 2;

bool SimBackend::init(HalHandler& handler) {
    m_handler = &handler;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[sim] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    const int pw = panelW(), ph = panelH();
    m_window = SDL_CreateWindow(
        m_profile == kPanelNSR2
            ? "groovebox_sim — NSR-2 panel — yawn engine emulator"
            : "groovebox_sim — NSR-1 panel — yawn engine emulator",
        pw, ph, SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::fprintf(stderr, "[sim] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    m_renderer = SDL_CreateRenderer(m_window, nullptr);
    if (!m_renderer) {
        std::fprintf(stderr, "[sim] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  kDisplayW, kDisplayH);
    m_panelTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB565,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       pw, ph);
    if (!m_texture || !m_panelTexture) {
        std::fprintf(stderr, "[sim] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(m_panelTexture, SDL_SCALEMODE_NEAREST);
    m_panelFb.resize(size_t(pw) * ph, 0);
    // panel mirror starts from the simulated analog defaults
    for (int i = 0; i < 2; ++i) m_panel.pots[i] = m_pots[i];
    m_panel.slider = m_slider;
    printLegend();
    return true;
}

void SimBackend::shutdown() {
    if (m_panelTexture) { SDL_DestroyTexture(m_panelTexture); m_panelTexture = nullptr; }
    if (m_texture)      { SDL_DestroyTexture(m_texture);   m_texture = nullptr; }
    if (m_renderer)     { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window)       { SDL_DestroyWindow(m_window);     m_window = nullptr; }
    SDL_Quit();
}

void SimBackend::printLegend() const {
    std::printf(
        "[sim] ── panel view (default; F11 toggles screen-only) ─────────\n"
        "[sim] profiles: --panel=nsr1 (piano panel) / --panel=nsr2 (grid)\n"
        "[sim] pages: SYNTH SEQ MIXER FX SAMPLE LOAD SET (< > cycle)\n"
        "[sim] mouse: click/hold keys & buttons = panel key events\n"
        "[sim]        pots = vertical drag or wheel over knob\n"
        "[sim]        slider = horizontal drag; joystick = 2D drag\n"
        "[sim]        encoders = wheel over knob (or vertical drag),\n"
        "[sim]                   click = push\n"
        "[sim] ── keyboard fallback (39 panel keys) ─────────────────────\n"
        "[sim] white keys : Z X C V B N M ,  = C D E F G A B C (octave 0)\n"
        "[sim]              Q W E R T Y U I  = D E F G A B C D (octaves 1-2)\n"
        "[sim] black keys : S D  G H J  (oct 0)   2 3  5 6 7 (oct 1)  9 (oct 2 C#)\n"
        "[sim] transport  : Space=play/stop  Backspace=stop  Enter=rec\n"
        "[sim] MODE=Tab   <=Left-Arrow  >=Right-Arrow  SHIFT=Left/Right Shift\n"
        "[sim] soft keys  : F1..F4 = S1..S4 (page-specific; S3/S4 = TRK-/TRK+\n"
        "[sim]                   everywhere; SYNTH: S1=OCT-(shift OCT+),\n"
        "[sim]                   S2=PG+ param pages OSC/MOD/MISC; MIXER:\n"
        "[sim]                   S1=MUTE selected; FX: S1=LOAD S2=BYP)\n"
        "[sim] ownership   : 2 pots own CUT/RES; encoders 5-8 = macros\n"
        "[sim]                   (default amp ADSR; shift+push = assign,\n"
        "[sim]                   touch param to bind, S1 = factory reset)\n"
        "[sim] shift layer : hold Shift + white 1-4 = select track,\n"
        "[sim]                   + white 5-8 = mute track; shift+</> = BPM\n"
        "[sim]                   (SEQ page: page the 16-step window)\n"
        "[sim] step edit   : (SEQ mode, T1) hold white key + ENC 1 =\n"
        "[sim]                   per-step pitch, ENC 2 = per-step gate —\n"
        "[sim]                   works on ANY page while held\n"
        "[sim] SEQ page    : S1=LEN (ENC1 = 1..256, any key exits),\n"
        "[sim]                   shift+S1=CLR, S2=4FLR\n"
        "[sim] encoders    : wheel column left->right = ENC 1-4 (footer)\n"
        "[sim] pots (kbd)  : F5/F6 = CUT/RES, -/= adjust\n"
        "[sim] slider(kbd) : [ and ] (velocity)\n"
        "[sim] joystick    : screen view: hold right mouse button + drag\n"
        "[sim] view/theme : F11 = panel/screen view, F12 = theme MONO/RED/GREEN\n"
        "[sim] quit        : ESC or close window\n"
        "[sim] ────────────────────────────────────────────────────────────\n");
    std::fflush(stdout);
}

int SimBackend::keyIndexForScancode(unsigned s) const {
    using Sc = SDL_Scancode;
    switch (s) {
        // white keys — bottom row (octave 0)
        case Sc::SDL_SCANCODE_Z: return 0;
        case Sc::SDL_SCANCODE_X: return 1;
        case Sc::SDL_SCANCODE_C: return 2;
        case Sc::SDL_SCANCODE_V: return 3;
        case Sc::SDL_SCANCODE_B: return 4;
        case Sc::SDL_SCANCODE_N: return 5;
        case Sc::SDL_SCANCODE_M: return 6;
        case Sc::SDL_SCANCODE_COMMA: return 7;
        // white keys — top row (octaves 1-2)
        case Sc::SDL_SCANCODE_Q: return 8;
        case Sc::SDL_SCANCODE_W: return 9;
        case Sc::SDL_SCANCODE_E: return 10;
        case Sc::SDL_SCANCODE_R: return 11;
        case Sc::SDL_SCANCODE_T: return 12;
        case Sc::SDL_SCANCODE_Y: return 13;
        case Sc::SDL_SCANCODE_U: return 14;
        case Sc::SDL_SCANCODE_I: return 15;
        // black keys — lower octave
        case Sc::SDL_SCANCODE_S: return 16;
        case Sc::SDL_SCANCODE_D: return 17;
        case Sc::SDL_SCANCODE_G: return 18;
        case Sc::SDL_SCANCODE_H: return 19;
        case Sc::SDL_SCANCODE_J: return 20;
        // black keys — upper octave
        case Sc::SDL_SCANCODE_2: return 21;
        case Sc::SDL_SCANCODE_3: return 22;
        case Sc::SDL_SCANCODE_5: return 23;
        case Sc::SDL_SCANCODE_6: return 24;
        case Sc::SDL_SCANCODE_7: return 25;
        case Sc::SDL_SCANCODE_9: return 26;
        // transport / mode / nav / soft keys / shift
        // (key indices per docs/panel-protocol.md)
        case Sc::SDL_SCANCODE_SPACE:     return kKeyPlay;
        case Sc::SDL_SCANCODE_BACKSPACE: return kKeyStop;
        case Sc::SDL_SCANCODE_RETURN:    return kKeyRec;
        case Sc::SDL_SCANCODE_TAB:       return kKeyMode;
        case Sc::SDL_SCANCODE_LEFT:      return kKeyPrev;
        case Sc::SDL_SCANCODE_RIGHT:     return kKeyNext;
        case Sc::SDL_SCANCODE_F1: return kKeySoft1;
        case Sc::SDL_SCANCODE_F2: return kKeySoft2;
        case Sc::SDL_SCANCODE_F3: return kKeySoft3;
        case Sc::SDL_SCANCODE_F4: return kKeySoft4;
        case Sc::SDL_SCANCODE_LSHIFT:    return kKeyShiftL;
        case Sc::SDL_SCANCODE_RSHIFT:    return kKeyShiftR;
        default: return -1;
    }
}

void SimBackend::setPot(int which, float value) {
    const float nv = std::fmin(1.0f, std::fmax(0.0f, value));
    if (nv != m_pots[which]) {
        m_pots[which] = nv;
        m_handler->onAnalog(kAnalogPot0 + which, nv);
    }
}

void SimBackend::adjustPot(int which, float delta) {
    setPot(which, m_pots[which] + delta);
}

void SimBackend::setSlider(float value) {
    const float nv = std::fmin(1.0f, std::fmax(0.0f, value));
    if (nv != m_slider) { m_slider = nv; m_handler->onAnalog(kAnalogSlider, nv); }
}

void SimBackend::emitJoy(float x, float y) {
    x = std::fmin(1.0f, std::fmax(-1.0f, x));
    y = std::fmin(1.0f, std::fmax(-1.0f, y));
    if (std::fabs(x - m_joyX) > 0.005f) { m_joyX = x; m_handler->onAnalog(kAnalogJoyX, x); }
    if (std::fabs(y - m_joyY) > 0.005f) { m_joyY = y; m_handler->onAnalog(kAnalogJoyY, y); }
}

// Screen view: joystick = right mouse button + drag (window coords).
void SimBackend::updateJoystick() {
    float mx, my;
    SDL_GetMouseState(&mx, &my);
    int ww = 0, wh = 0;
    SDL_GetWindowSize(m_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    emitJoy((mx / float(ww)) * 2.0f - 1.0f, 1.0f - (my / float(wh)) * 2.0f);
}

bool SimBackend::windowToLogical(float wx, float wy, float& lx,
                                 float& ly) const {
    // SDL3 (pinned rev): SDL_RenderCoordinatesFromWindow, formerly
    // SDL_RenderWindowToLogical.
    return SDL_RenderCoordinatesFromWindow(m_renderer, wx, wy, &lx, &ly);
}

// ── panel view mouse interaction ────────────────────────────────────

void SimBackend::mouseDownPanel(float lx, float ly) {
    const PanelView::Hit hit =
        m_profile == kPanelNSR2
            ? PanelViewNSR2::hitTest(int(lx + 0.5f), int(ly + 0.5f))
            : PanelView::hitTest(int(lx + 0.5f), int(ly + 0.5f));
    m_dragType = hit.type;
    m_dragIndex = hit.index;
    m_panel.activeType = hit.type;
    m_panel.activeIndex = hit.index;
    switch (hit.type) {
    case PanelView::kHitKey:
        m_panel.keyDown[hit.index] = true;
        m_handler->onKey(hit.index, true);
        break;
    case PanelView::kHitPot:
        m_dragStartVal = m_pots[hit.index];
        m_dragStartY = ly;
        break;
    case PanelView::kHitSlider:
        if (m_profile == kPanelNSR2) // vertical crossfader: top = 1
            setSlider(1.0f - (ly - 133 * 4) / float(52 * 4));
        else
            setSlider((lx - 310 * 4) / float(62 * 4));
        break;
    case PanelView::kHitJoy:
        if (m_profile == kPanelNSR2)
            emitJoy((lx - 17 * 4) / float(9 * 4),
                    -(ly - 158 * 4) / float(9 * 4));
        else
            emitJoy((lx - 18 * 4) / float(10 * 4),
                    -(ly - 105 * 4) / float(10 * 4));
        break;
    case PanelView::kHitEncoder:
        m_encAccum = 0.0f;
        m_dragStartY = ly;
        m_panel.encoderPush[hit.index] = true;
        m_handler->onEncoderPush(hit.index, true);
        break;
    default: break;
    }
}

void SimBackend::mouseMovePanel(float lx, float ly) {
    switch (m_dragType) {
    case PanelView::kHitPot:
        setPot(m_dragIndex, m_dragStartVal + (m_dragStartY - ly) / 120.0f);
        break;
    case PanelView::kHitSlider:
        if (m_profile == kPanelNSR2)
            setSlider(1.0f - (ly - 133 * 4) / float(52 * 4));
        else
            setSlider((lx - 310 * 4) / float(62 * 4));
        break;
    case PanelView::kHitJoy:
        if (m_profile == kPanelNSR2)
            emitJoy((lx - 17 * 4) / float(9 * 4),
                    -(ly - 158 * 4) / float(9 * 4));
        else
            emitJoy((lx - 18 * 4) / float(10 * 4),
                    -(ly - 105 * 4) / float(10 * 4));
        break;
    case PanelView::kHitEncoder: {
        m_encAccum += m_dragStartY - ly;
        m_dragStartY = ly;
        while (m_encAccum >= 24.0f) {
            m_encAccum -= 24.0f;
            m_handler->onEncoderDelta(m_dragIndex, +1);
        }
        while (m_encAccum <= -24.0f) {
            m_encAccum += 24.0f;
            m_handler->onEncoderDelta(m_dragIndex, -1);
        }
        break;
    }
    default: break;
    }
}

void SimBackend::mouseUpPanel(float lx, float ly) {
    (void)lx; (void)ly;
    if (m_dragType == PanelView::kHitKey && m_dragIndex >= 0) {
        m_panel.keyDown[m_dragIndex] = false;
        m_handler->onKey(m_dragIndex, false);
    } else if (m_dragType == PanelView::kHitEncoder && m_dragIndex >= 0) {
        m_panel.encoderPush[m_dragIndex] = false;
        m_handler->onEncoderPush(m_dragIndex, false);
    } else if (m_dragType == PanelView::kHitJoy) {
        emitJoy(0.0f, 0.0f); // spring return to center
    }
    m_dragType = PanelView::kHitNone;
    m_dragIndex = -1;
    m_panel.activeType = PanelView::kHitNone;
    m_panel.activeIndex = -1;
}

void SimBackend::wheelPanel(float lx, float ly, int delta) {
    const PanelView::Hit hit =
        m_profile == kPanelNSR2
            ? PanelViewNSR2::hitTest(int(lx + 0.5f), int(ly + 0.5f))
            : PanelView::hitTest(int(lx + 0.5f), int(ly + 0.5f));
    if (hit.type == PanelView::kHitPot)
        adjustPot(hit.index, delta * 0.05f);
    else if (hit.type == PanelView::kHitEncoder && delta != 0)
        m_handler->onEncoderDelta(hit.index, delta);
}

void SimBackend::poll() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            m_handler->onQuit();
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const bool down = (e.type == SDL_EVENT_KEY_DOWN);
            const unsigned sc = e.key.scancode;
            if (down && sc == SDL_SCANCODE_ESCAPE) { m_handler->onQuit(); break; }
            if (down && !e.key.repeat && sc == SDL_SCANCODE_F11) {
                m_panelMode = !m_panelMode;
                std::printf("[sim] view -> %s\n",
                            m_panelMode ? "PANEL" : "SCREEN (2x)");
                break;
            }
            if (down && !e.key.repeat && sc == SDL_SCANCODE_F12) {
                m_handler->onThemeNext();
                break;
            }
            // pot / slider simulation (repeat allowed for adjustment)
            if (down && sc >= SDL_SCANCODE_F5 && sc <= SDL_SCANCODE_F6) {
                m_selectedPot = int(sc - SDL_SCANCODE_F5);
                std::printf("[sim] pot %d selected (%.2f)\n",
                            m_selectedPot + 1, double(m_pots[m_selectedPot]));
                break;
            }
            if (down && (sc == SDL_SCANCODE_MINUS || sc == SDL_SCANCODE_KP_MINUS)) {
                adjustPot(m_selectedPot, -0.02f); break;
            }
            if (down && (sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS)) {
                adjustPot(m_selectedPot, +0.02f); break;
            }
            if (down && sc == SDL_SCANCODE_LEFTBRACKET)  { setSlider(m_slider - 0.02f); break; }
            if (down && sc == SDL_SCANCODE_RIGHTBRACKET) { setSlider(m_slider + 0.02f); break; }
            if (e.key.repeat) break; // no key-repeat retrigger on panel keys
            const int idx = keyIndexForScancode(sc);
            if (idx >= 0) {
                m_panel.keyDown[idx] = down;
                m_handler->onKey(idx, down);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            float lx, ly;
            if (!windowToLogical(e.wheel.mouse_x, e.wheel.mouse_y, lx, ly))
                break;
            const int delta = e.wheel.integer_y != 0 ? e.wheel.integer_y
                                                     : (e.wheel.y > 0 ? 1 : -1);
            if (delta == 0) break;
            if (m_panelMode) {
                wheelPanel(lx, ly, delta);
            } else {
                // screen view: wheel over 4 columns = ENC 1-4
                int zone = int(lx / (float(kLogicalW) / kNumEncoders));
                zone = zone < 0 ? 0 : (zone >= kNumEncoders ? kNumEncoders - 1 : zone);
                m_handler->onEncoderDelta(zone, delta);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT && m_panelMode) {
                float lx, ly;
                if (windowToLogical(e.button.x, e.button.y, lx, ly))
                    mouseDownPanel(lx, ly);
            } else if (e.button.button == SDL_BUTTON_RIGHT && !m_panelMode) {
                m_joyActive = true;
                updateJoystick();
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT && m_panelMode) {
                float lx, ly;
                windowToLogical(e.button.x, e.button.y, lx, ly);
                mouseUpPanel(lx, ly);
            } else if (e.button.button == SDL_BUTTON_RIGHT && m_joyActive) {
                m_joyActive = false;
                emitJoy(0.0f, 0.0f);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (m_panelMode && m_dragType != PanelView::kHitNone) {
                float lx, ly;
                if (windowToLogical(e.motion.x, e.motion.y, lx, ly))
                    mouseMovePanel(lx, ly);
            } else if (m_joyActive) {
                updateJoystick();
            }
            break;
        default: break;
        }
    }
}

void SimBackend::presentFrame(const uint16_t* rgb565) {
    // mirror HAL-side state into the panel model
    for (int i = 0; i < 2; ++i) m_panel.pots[i] = m_pots[i];
    m_panel.slider = m_slider;
    m_panel.joyX = m_joyX;
    m_panel.joyY = m_joyY;
    for (int i = 0; i < kNumLeds; ++i) m_panel.leds[i] = m_leds[i];

    if (m_panelMode) {
        const int pw = panelW(), ph = panelH();
        Canvas565 c{m_panelFb.data(), pw, ph};
        if (m_profile == kPanelNSR2)
            PanelViewNSR2::render(c, m_panelFont, m_panel, rgb565);
        else
            PanelView::render(c, m_panelFont, m_panel, rgb565);
        SDL_UpdateTexture(m_panelTexture, nullptr, m_panelFb.data(),
                          pw * int(sizeof(uint16_t)));
        SDL_SetRenderLogicalPresentation(m_renderer, pw, ph,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_RenderClear(m_renderer);
        SDL_RenderTexture(m_renderer, m_panelTexture, nullptr, nullptr);
        SDL_RenderPresent(m_renderer);
    } else {
        SDL_UpdateTexture(m_texture, nullptr, rgb565,
                          kDisplayW * int(sizeof(uint16_t)));
        SDL_SetRenderLogicalPresentation(m_renderer, kLogicalW, kLogicalH,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_RenderClear(m_renderer);
        SDL_RenderTexture(m_renderer, m_texture, nullptr, nullptr);
        SDL_RenderPresent(m_renderer);
    }
}

void SimBackend::setLed(int index, bool on) {
    if (index < 0 || index >= kMaxLeds) return;
    if (index > m_ledMax) m_ledMax = index;
    m_leds[index] = on;
}

} // namespace gb
