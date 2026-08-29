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
