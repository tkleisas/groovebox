#pragma once

#include <stdbool.h>
#include <stdint.h>

void leds_init(void);
void leds_set(uint8_t index, bool on);   // index 0..15 (white-key LEDs)
void leds_tick(void);                    // flush to HT16K33 when dirty
