#pragma once

namespace gb {

// Dev/review tool: render every widget onto one labeled, 8px-grid-
// fitted page (240 px wide RGB565, matching the device renderer) with
// plausible demo data, and write it as a PNG. Canvas height is fixed
// by the layout. Returns false if the PNG could not be written.
bool writeWidgetShowcasePng(const char* path);

// Render a representative SEQ page in all three themes (MONO/RED/
// GREEN), stacked with labels, to a PNG.
bool writeThemesPng(const char* path);

} // namespace gb
