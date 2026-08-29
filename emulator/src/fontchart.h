#pragma once

namespace gb {

// Dev/review tool: render every defined non-ASCII glyph (Greek +
// Cyrillic blocks) onto a native-resolution RGB canvas — 4x glyph +
// "U+XXXX" label per cell, sectioned GREEK UPPERCASE / GREEK
// LOWERCASE + TONOS / CYRILLIC UPPERCASE / CYRILLIC LOWERCASE — and
// write it as a PNG. Canvas size is computed from the content.
// Returns false if the PNG could not be written.
bool writeFontChartPng(const char* path);

} // namespace gb
