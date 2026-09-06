#pragma once

#include <stdbool.h>
#include <stdint.h>

// Rev B2: 2x ADS1115, 5 channels — pots CUT/RES, slider, joystick X/Y.
// (Pots A/D/S/R and chip 0x4A removed; ADSR moved to encoders 5-8.)
enum {
    ADS_POT0 = 0, ADS_POT1,          // CUT, RES — the only pots left
    ADS_SLIDER = 2,
    ADS_JOY_Y  = 3,
    ADS_JOY_X  = 4,
    ADS_CH_COUNT = 5,
};

#define ADS_POT_COUNT  2

// Expected full-scale raw reading for 3V3-referenced pots with the
// +/-4.096 V PGA: 32767 * 3.3 / 4.096 ~= 26400. Single-ended reads span
// 0..ADS_FULL_SCALE_RAW in practice; per-unit calibration can tune this later.
#define ADS_FULL_SCALE_RAW 26400

void ads_init(void);
void ads_tick(void);                 // poll one of the two chips, round-robin
uint16_t ads_value(uint8_t ch);      // latest 16-bit reading, 0..ADS_FULL_SCALE_RAW
bool ads_any_offline(void);          // true if any chip failed its probe
