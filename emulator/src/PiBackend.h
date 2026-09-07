#pragma once

// Headless Raspberry Pi HAL backend (no SDL, no X/Wayland):
//   * Display: fbdev — /dev/fb1 (ILI9488 via fbtft overlay on SPI),
//     fallback /dev/fb0. The 240x160 RGB565 frame is 2x-doubled into
//     the 480x320 framebuffer (mmap'd; write() path also fine at 20fps
//     but mmap avoids a syscall per frame — documented choice).
//   * Input: the USB-MIDI panel via RtMidi (vendored by yawn) — opens
//     the port whose name contains "groovebox-panel" and decodes
//     channel 1 per docs/panel-protocol.md.
//   * LEDs: note on/off on channel 2 back to the panel (32 LEDs: 0-15
//     step row, 16-31 white keys / NSR-2 grid).

#include "hal.h"

#include <cstddef> // size_t (hal.h only guarantees <cstdint>)

class RtMidiIn;   // forward decls — RtMidi C++ headers stay out of hal.h
class RtMidiOut;  // (they're pulled into PiBackend.cpp only)

namespace gb {

class PiBackend : public Hal {
public:
    ~PiBackend() override { shutdown(); }

    bool init(HalHandler& handler) override;
    void shutdown() override;
    void poll() override;
    void presentFrame(const uint16_t* rgb565) override;
    void setLed(int index, bool on) override;

    // Interface-compat with SimBackend (profile affects app-side key
    // semantics; the Pi backend always accepts 56 keys / 32 LEDs).
    void setPanelProfile(PanelProfile p) { m_profile = p; }
    int ledMaxIndex() const { return m_ledMax; } // test-mode compat
    // The SIGINT/SIGTERM handler (static) reaches the app via this.
    HalHandler* handler() const { return m_handler; }

private:
    HalHandler* m_handler = nullptr;
    PanelProfile m_profile = kPanelNSR1;

    // fbdev
    int m_fbFd = -1;
    uint8_t* m_fbMap = nullptr;
    size_t m_fbSize = 0;
    int m_fbW = 0, m_fbH = 0, m_fbBpp = 0;

    // RtMidi
    RtMidiIn* m_midiIn = nullptr;
    RtMidiOut* m_midiOut = nullptr;

    int m_ledMax = -1;
    bool m_leds[kMaxLeds] = {};

    bool openDisplay();
    void closeDisplay();
    bool openMidi();
    void decodeMessage(const unsigned char* msg, size_t len);
};

} // namespace gb
