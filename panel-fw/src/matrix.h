#pragma once

#include <stdint.h>

void matrix_init(void);   // HT16K33 setup: oscillator, display, brightness
void matrix_tick(void);   // 1 ms key scan + debounce -> Note On/Off ch1
