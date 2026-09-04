#pragma once

// Virtual front panel: geometry-driven reproduction of
// tools/mockup_panel.py (400x150mm panel) at 4 px/mm → 1600x600 px.
// All controls are interactive (hit-tested by SimBackend and mapped to
// HAL events; key indices per docs/panel-protocol.md). The device
// screen (240x160 RGB565) is stretched to FILL the bezel's inner area
// (~1.47x non-uniform, nearest-neighbor — the slightly uneven pixels
// read like a real LCD).

#include "widgets.h" // Canvas565, Font5x7, palette
#include "hal.h"

namespace gb {

// Live panel state, owned by the backend (SimBackend) and fed from the
// same data that goes to the HAL handler.
struct PanelState {
    bool keyDown[kN2NumKeys] = {};       // sized for NSR-2 (45)
    bool encoderPush[kNumEncoders] = {};
    float pots[6] = {};         // 0..1, CUT/RES/A/D/S/R
    float slider = 0.8f;        // 0..1 (NSR-2: crossfader)
    float joyX = 0, joyY = 0;   // -1..1
    bool leds[kMaxLeds] = {};
    // mouse-captured control (bright outline highlight)
    int activeType = 0;         // PanelView::HitType
    int activeIndex = -1;
};

class PanelView {
public:
    static constexpr int kPxPerMm = 4;
    static constexpr int kW = 400 * kPxPerMm;  // 1600
    static constexpr int kH = 150 * kPxPerMm;  // 600

    enum HitType {
        kHitNone = 0,
        kHitKey,      // index = panel key index (0..39)
        kHitPot,      // index = pot 0..5 (analog channel 0..5)
        kHitSlider,   // analog channel 6
        kHitJoy,      // analog channels 7/8
        kHitEncoder,  // index = encoder 0..3
    };
    struct Hit { int type = kHitNone; int index = -1; };

    // Panel-space px coords → control.
    static Hit hitTest(int x, int y);

    // Render the whole panel. screen240x160 = the device framebuffer
    // (may be nullptr → bezel drawn empty).
    static void render(Canvas565& c, Font5x7& font, const PanelState& st,
                       const uint16_t* screen240x160);
};

} // namespace gb
