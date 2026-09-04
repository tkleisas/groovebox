#pragma once

#include <stdint.h>

enum {
    ADS_POT0 = 0, ADS_POT1, ADS_POT2, ADS_POT3, ADS_POT4, ADS_POT5,
    ADS_SLIDER = 6,
    ADS_JOY_X = 7,
    ADS_JOY_Y = 8,
    ADS_CH_COUNT = 9,
};

// Expected full-scale raw reading for 3V3-referenced pots with the
// +/-4.096 V PGA: 32767 * 3.3 / 4.096 ~= 26400. Single-ended reads span
// 0..ADS_FULL_SCALE_RAW in practice; per-unit calibration can tune this later.
#define ADS_FULL_SCALE_RAW 26400

void ads_init(void);
void ads_tick(void);                 // poll one of the three chips, round-robin
uint16_t ads_value(uint8_t ch);      // latest 16-bit reading, 0..ADS_FULL_SCALE_RAW
