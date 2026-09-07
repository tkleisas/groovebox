// PiBackend — see PiBackend.h. Linux-only (built with GB_PI_BACKEND).

#include "PiBackend.h"

#include <RtMidi.h>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace gb {

namespace {
PiBackend* g_self = nullptr; // for the SIGINT handler
void onSigint(int) {
    if (g_self && g_self->handler()) g_self->handler()->onQuit();
}
} // namespace

bool PiBackend::init(HalHandler& handler) {
    m_handler = &handler;
    g_self = this;
    signal(SIGINT, onSigint);
    signal(SIGTERM, onSigint);
    const bool dispOk = openDisplay();
    const bool midiOk = openMidi();
    std::printf("[pi] backend up: display %s (%dx%d %dbpp), panel %s\n",
                dispOk ? "OK" : "FAILED", m_fbW, m_fbH, m_fbBpp,
                midiOk ? "OK (groovebox-panel)" : "not found (display-only)");
    // Neither peripheral is fatal: a headless profiling/CI run (or a
    // Pi booted without the SPI panel seated) still boots the engine.
    // The warnings above are loud enough to catch a wiring fault.
    if (!dispOk)
        std::fprintf(stderr, "[pi] WARNING: no display — running blind\n");
    return true;
}

void PiBackend::shutdown() {
    closeDisplay();
    if (m_midiIn) { m_midiIn->closePort(); delete m_midiIn; m_midiIn = nullptr; }
    if (m_midiOut) { m_midiOut->closePort(); delete m_midiOut; m_midiOut = nullptr; }
    g_self = nullptr;
}

// ── display (fbdev) ─────────────────────────────────────────────────

bool PiBackend::openDisplay() {
    const char* paths[] = {"/dev/fb1", "/dev/fb0"};
    for (const char* p : paths) {
        m_fbFd = open(p, O_RDWR);
        if (m_fbFd < 0) continue;
        fb_var_screeninfo var{};
        fb_fix_screeninfo fix{};
        if (ioctl(m_fbFd, FBIOGET_VSCREENINFO, &var) < 0 ||
            ioctl(m_fbFd, FBIOGET_FSCREENINFO, &fix) < 0) {
            close(m_fbFd); m_fbFd = -1; continue;
        }
        m_fbW = int(var.xres); m_fbH = int(var.yres);
        m_fbBpp = int(var.bits_per_pixel);
        m_fbSize = fix.smem_len;
        m_fbMap = static_cast<uint8_t*>(
            mmap(nullptr, m_fbSize, PROT_READ | PROT_WRITE, MAP_SHARED,
                 m_fbFd, 0));
        if (m_fbMap == MAP_FAILED) {
            close(m_fbFd); m_fbFd = -1; m_fbMap = nullptr; continue;
        }
        std::printf("[pi] display: %s %dx%d %dbpp (mmap %zu KB)\n", p,
                    m_fbW, m_fbH, m_fbBpp, m_fbSize / 1024);
        return true;
    }
    std::fprintf(stderr, "[pi] no framebuffer found (/dev/fb1, /dev/fb0)\n");
    return false;
}

void PiBackend::closeDisplay() {
    if (m_fbMap) { munmap(m_fbMap, m_fbSize); m_fbMap = nullptr; }
    if (m_fbFd >= 0) { close(m_fbFd); m_fbFd = -1; }
}

void PiBackend::presentFrame(const uint16_t* rgb565) {
    if (!m_fbMap) return;
    // 2x integer doubling, centered; 16bpp writes through, 32bpp
    // converts to XRGB8888.
    const int dw = (m_fbW >= kDisplayW * 2) ? kDisplayW * 2 : m_fbW;
    const int dh = (m_fbH >= kDisplayH * 2) ? kDisplayH * 2 : m_fbH;
    const int ox = (m_fbW - dw) / 2, oy = (m_fbH - dh) / 2;
    const int srcH = dh / 2, srcW = dw / 2;
    for (int y = 0; y < srcH; ++y) {
        const uint16_t* srow = rgb565 + y * kDisplayW;
        for (int dy = 0; dy < 2; ++dy) {
            const int row = oy + y * 2 + dy;
            if (m_fbBpp == 16) {
                uint16_t* d = reinterpret_cast<uint16_t*>(
                    m_fbMap + row * m_fbW * 2 + ox * 2);
                for (int x = 0; x < srcW; ++x) {
                    d[x * 2] = srow[x]; d[x * 2 + 1] = srow[x];
                }
            } else if (m_fbBpp == 32) {
                uint32_t* d = reinterpret_cast<uint32_t*>(
                    m_fbMap + row * m_fbW * 4 + ox * 4);
                for (int x = 0; x < srcW; ++x) {
                    const uint16_t p = srow[x];
                    const uint32_t px =
                        (uint32_t((p >> 11) & 0x1F) << 19 |
                         uint32_t((p >> 5) & 0x3F) << 10 |
                         uint32_t(p & 0x1F) << 3) * 0xFF / 0x1F; // approx
                    d[x * 2] = px; d[x * 2 + 1] = px;
                }
            }
        }
    }
}

// ── MIDI panel (RtMidi) ─────────────────────────────────────────────

static int findPortByName(RtMidi& io, const char* needle) {
    const unsigned n = io.getPortCount();
    for (unsigned i = 0; i < n; ++i) {
        const std::string name = io.getPortName(i);
        if (name.find(needle) != std::string::npos) return int(i);
    }
    return -1;
}

bool PiBackend::openMidi() {
    // RtMidiIn's ctor throws RtMidiError when no MIDI API is usable at
    // all (e.g. ALSA sequencer missing in a container) — treat that as
    // "panel not found", not a crash.
    try {
        m_midiIn = new RtMidiIn();
    } catch (const RtMidiError& e) {
        std::fprintf(stderr, "[pi] RtMidi init failed: %s\n",
                     e.getMessage().c_str());
        return false;
    }
    m_midiIn->ignoreTypes(false, true, true); // receive sysex, skip timing
    const int inPort = findPortByName(*m_midiIn, "groovebox-panel");
    if (inPort < 0) {
        delete m_midiIn; m_midiIn = nullptr;
        return false;
    }
    m_midiIn->openPort(unsigned(inPort), "groovebox-brain");
    m_midiOut = new RtMidiOut();
    const int outPort = findPortByName(*m_midiOut, "groovebox-panel");
    if (outPort >= 0) m_midiOut->openPort(unsigned(outPort), "groovebox-brain");
    else { delete m_midiOut; m_midiOut = nullptr; }
    std::printf("[pi] panel MIDI in on port %d, out %s\n", inPort,
                m_midiOut ? "OK" : "missing (LEDs off)");
    return true;
}

void PiBackend::poll() {
    if (!m_midiIn) return;
    std::vector<unsigned char> msg;
    for (;;) {
        const double dt = m_midiIn->getMessage(&msg); // non-blocking pop
        if (msg.empty()) break;
        (void)dt;
        decodeMessage(msg.data(), msg.size());
    }
}

void PiBackend::decodeMessage(const unsigned char* msg, size_t len) {
    if (len < 2) return;
    const uint8_t status = msg[0];
    const uint8_t type = status & 0xF0;
    const uint8_t chan = status & 0x0F;
    if (chan != 0) return; // channel 1 only (panel -> brain)
    switch (type) {
    case 0x90: // note on (vel 0 = off)
    case 0x80: {
        if (len < 3) return;
        const int idx = int(msg[1]) - 36;
        if (idx < 0 || idx >= kMaxPanelKeys) return;
        m_handler->onKey(idx, type == 0x90 && msg[2] > 0);
        break;
    }
    case 0xB0: { // CC
        if (len < 3) return;
        const int cc = msg[1];
        const float v = msg[2] / 127.0f;
        if (cc == 20 || cc == 21)        m_handler->onAnalog(cc - 20, v);
        else if (cc == 26)               m_handler->onAnalog(kAnalogSlider, v);
        else if (cc == 1)                m_handler->onAnalog(kAnalogJoyY, v * 2.0f - 1.0f);
        else if (cc >= 16 && cc <= 23)   m_handler->onEncoderDelta(cc - 16, int(msg[2]) - 64);
        else if (cc >= 32 && cc <= 39)   m_handler->onEncoderPush(cc - 32, msg[2] > 0);
        break;
    }
    case 0xE0: { // pitch bend (14-bit, center 8192) -> joystick X
        if (len < 3) return;
        const int v = msg[1] | (msg[2] << 7);
        m_handler->onAnalog(kAnalogJoyX, (v - 8192) / 8191.0f);
        break;
    }
    default: break;
    }
}

void PiBackend::setLed(int index, bool on) {
    if (index < 0 || index >= kMaxLeds) return;
    if (index > m_ledMax) m_ledMax = index;
    if (m_leds[index] == on) return;
    m_leds[index] = on;
    if (!m_midiOut) return;
    const unsigned char msg[3] = {uint8_t(0x91 | 1), // note on, channel 2
                                  uint8_t(index), uint8_t(on ? 127 : 0)};
    try {
        m_midiOut->sendMessage(msg, 3);
    } catch (const RtMidiError&) {
        // Panel unplugged mid-run — drop the LED update, keep running.
    }
}

} // namespace gb
