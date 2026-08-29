#pragma once

// Uniform key input: all 39 panel keys read from three MCP23017 expanders
// (single-ended, expander pull-ups, active low). Debounced, emitted as
// Note On/Off ch1 at 36 + panel key index.

void keys_init(void);
void keys_tick(void);      // polls one expander per 1 ms tick (round-robin)
