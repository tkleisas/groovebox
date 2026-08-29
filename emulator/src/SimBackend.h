#pragma once

// SDL3 implementation of the groovebox HAL. Two views (F11 toggles):
//   * PANEL view (default): the full virtual front panel
//     (PanelView, 1600x600 = 400x150mm @ 4px/mm) with mouse-interactive
//     controls; the device screen renders inset in its bezel.
//   * SCREEN view: just the 240x160 framebuffer at 2x (letterboxed) —
//     for UI screenshot work.
// PC keyboard always maps to the 39 panel keys (panel-protocol.md).

#include "hal.h"
#include "PanelView.h"
#include "font5x7.h"
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace gb {

class SimBackend : public Hal {
public:
    ~SimBackend() override = default;

    bool init(HalHandler& handler) override;
    void shutdown() override;
    void poll() override;
    void presentFrame(const uint16_t* rgb565) override;
    void setLed(int index, bool on) override;

private:
    HalHandler* m_handler = nullptr;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_texture = nullptr;       // screen view (240x160)
    SDL_Texture* m_panelTexture = nullptr;  // panel view (1600x600)

    bool m_panelMode = true;
    Font5x7 m_panelFont;
    PanelState m_panel;
    std::vector<uint16_t> m_panelFb;        // PanelView::kW*kH

    bool m_leds[kNumLeds] = {};

    // Simulated analog state. Pot defaults mirror the UI mockup values.
    int m_selectedPot = 0;
    float m_pots[6] = {87.f / 127.f, 41.f / 127.f, 3.f / 127.f,
                       55.f / 127.f, 70.f / 127.f, 24.f / 127.f};
    float m_slider = 0.8f;
    bool m_joyActive = false;   // screen view: right-drag joystick
    float m_joyX = 0.0f, m_joyY = 0.0f;

    // panel view: captured mouse drag
    int m_dragType = PanelView::kHitNone;
    int m_dragIndex = -1;
    float m_dragStartVal = 0.0f;
    float m_dragStartY = 0.0f;
    float m_encAccum = 0.0f;

    int keyIndexForScancode(unsigned scancode) const;
    void setPot(int which, float value);
    void adjustPot(int which, float delta);
    void setSlider(float value);
    void emitJoy(float x, float y);
    void updateJoystick(); // screen view right-drag
    bool windowToLogical(float wx, float wy, float& lx, float& ly) const;
    void mouseDownPanel(float lx, float ly);
    void mouseMovePanel(float lx, float ly);
    void mouseUpPanel(float lx, float ly);
    void wheelPanel(float lx, float ly, int delta);
    void printLegend() const;
};

} // namespace gb
