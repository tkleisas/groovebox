#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LED_COUNT  32   // ch2 note = LED index: 0..15 step row, 16..31 white keys

void leds_init(void);
void leds_set(uint8_t index, bool on);   // index 0..31 (see LED_COUNT)
void leds_tick(void);                    // flush to HT16K33 when dirty
bool leds_online(void);                  // false if the HT16K33 failed its probe
