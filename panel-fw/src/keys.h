#pragma once

#include <stdbool.h>

// Uniform key input: all 55 panel keys plus 8 encoder push-buttons read
// from four MCP23017 expanders (single-ended, expander pull-ups, active
// low). Debounced; keys emit Note On/Off ch1 at 36 + panel key index,
// pushes emit CC 32..39 (127 press / 0 release). Absent expanders are
// probed at init and skipped in the scan (any subset attached works).

void keys_init(void);
void keys_tick(void);      // scans all online expanders every 1 ms tick
bool keys_any_offline(void);   // true if any expander failed its probe
