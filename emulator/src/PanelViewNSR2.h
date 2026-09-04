#pragma once

// NSR-2 virtual front panel: geometry-driven portrait faceplate
// (180x210mm @ 4px/mm → 720x840 px) per docs/nsr2-design.md and
// nsr2/panel_mockup_nsr2.png. Same conventions as PanelView (NSR-1):
// Canvas565 software rendering, hit-tested mouse-interactive controls,
// device screen stretched to fill the window bezel, red PCB + cream
// silkscreen aesthetic. Key indices per docs/panel-protocol.md NSR-2
// section (hal.h KeyNSR2).

#include "PanelView.h" // PanelState, Canvas565 via widgets.h

namespace gb {

class PanelViewNSR2 {
public:
    static constexpr int kPxPerMm = 4;
    static constexpr int kW = 180 * kPxPerMm;  // 720
    static constexpr int kH = 210 * kPxPerMm;  // 840

    static PanelView::Hit hitTest(int x, int y);

    static void render(Canvas565& c, Font5x7& font, const PanelState& st,
                       const uint16_t* screen240x160);
};

} // namespace gb
