#pragma once

// Hardware abstraction layer for the groovebox.
//
// The application talks only to this interface. Two backends exist:
//   * SimBackend  — SDL3 window + PC keyboard/mouse (this emulator)
//   * PiBackend   — ILI9488 SPI display + HT16K33 key matrix + ADS1115
//                   analogs on the Raspberry Pi (later milestone)
//
// The panel (docs/design.md) has exactly 39 keys, 4 push-encoders,
// 6 pots + 1 slider + 1 two-axis joystick (analog), and 16 LEDs.

#include <cstdint>

namespace gb {

// Display: 240x160, RGB565 (matches the window the UI is designed for;
// the physical 480x320 ILI9488 will be driven at 2x as well).
constexpr int kDisplayW = 240;
constexpr int kDisplayH = 160;

constexpr int kNumEncoders = 4;
constexpr int kNumLeds     = 16;
constexpr int kMaxLeds     = 32; // NSR-2 grid

// Panel profiles: NSR-1 (400x150 landscape, 39 piano keys) vs NSR-2
// (180x210 portrait, 45 keys, 4x8 grid). Selected by --panel= at app
// level; the backend renders the matching faceplate.
enum PanelProfile { kPanelNSR1 = 0, kPanelNSR2 = 1 };

// ── NSR-2 panel key indices (docs/nsr2-design.md, panel-protocol) ───
//  0..31  grid keys, row-major (top-left = 0)
// 32..34 transport: PLAY, STOP, REC
// 35     MODE
// 36..37 < and >
// 38..41 soft keys S1..S4
// 42..43 SHIFT-L, SHIFT-R
// 44     SPACE (wide bar)
enum KeyNSR2 {
    kN2Grid0  = 0,
    kN2Play   = 32,
    kN2Stop   = 33,
    kN2Rec    = 34,
    kN2Mode   = 35,
    kN2Prev   = 36,
    kN2Next   = 37,
    kN2Soft1  = 38,
    kN2Soft2  = 39,
    kN2Soft3  = 40,
    kN2Soft4  = 41,
    kN2ShiftL = 42,
    kN2ShiftR = 43,
    kN2Space  = 44,
    kN2NumKeys = 45,
    kN2NumLeds = 32,
};

// ── Panel key indices (docs/panel-protocol.md) ──────────────────────
//  0..15  white keys (C D E F G A B | C D E F G A B C D — two octaves)
// 16..26  black keys (11, piano pattern)
// 27..29  transport: PLAY, STOP, REC
// 30      reserved (unused on rev A)
// 31      MODE
// 32..33  < and >
// 34..37  soft keys S1..S4 (under the screen)
// 38..39  SHIFT-L, SHIFT-R (held modifiers, logically identical)
// Index space is 0..39 (40 slots, one reserved) for 39 physical keys.
constexpr int kNumKeys = 40;
enum Key {
    kKeyWhite0 = 0,
    kKeyBlack0 = 16,
    kKeyPlay   = 27,
    kKeyStop   = 28,
    kKeyRec    = 29,
    // 30 reserved
    kKeyMode   = 31,
    kKeyPrev   = 32,
    kKeyNext   = 33,
    kKeySoft1  = 34,
    kKeySoft2  = 35,
    kKeySoft3  = 36,
    kKeySoft4  = 37,
    kKeyShiftL = 38,
    kKeyShiftR = 39,
};

// ── Analog channel indices ──────────────────────────────────────────
//  0..5  pots: cutoff, resonance, amp-ENV A/D/S/R   (float 0..1)
//  6     data slider (default: velocity)            (float 0..1)
//  7..8  joystick X (pitch bend) / Y (mod)          (float -1..1)
enum Analog {
    kAnalogPot0   = 0,
    kAnalogSlider = 6,
    kAnalogJoyX   = 7,
    kAnalogJoyY   = 8,
    kNumAnalog    = 9,
};

// Application-side event sink. Called from the backend's poll().
class HalHandler {
public:
    virtual ~HalHandler() = default;
    virtual void onKey(int index, bool pressed)      = 0;
    virtual void onEncoderDelta(int index, int delta) = 0;
    virtual void onAnalog(int index, float value)    = 0;
    virtual void onQuit()                            = 0;
    // Encoder push-button (CC 32-35 in panel-protocol.md). Default
    // no-op — pages may ignore pushes until they have a use for them.
    virtual void onEncoderPush(int index, bool pressed) {
        (void)index; (void)pressed;
    }
    // UI theme cycle request (F12 in the sim). Default no-op.
    virtual void onThemeNext() {}
};

class Hal {
public:
    virtual ~Hal() = default;
    virtual bool init(HalHandler& handler) = 0;
    virtual void shutdown() = 0;
    virtual void poll() = 0;  // dispatch pending input events
    // Submit a finished 240x160 RGB565 frame (kDisplayW*kDisplayH pixels).
    virtual void presentFrame(const uint16_t* rgb565) = 0;
    virtual void setLed(int index, bool on) = 0;
};

} // namespace gb
