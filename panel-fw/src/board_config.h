#pragma once

// Hardware-facing constants for the groovebox panel module (rev A).
// Docs: docs/panel-protocol.md, docs/design.md.
// The keyboard PCB routes matrix cell (row, col) to physical key index per
// KEYMAP below — the layout MUST match this table (or update it here).

#include <stdint.h>

// ── I2C bus (HT16K33 + 3x ADS1115) ──────────────────────────────────
#define PANEL_I2C            i2c0
#define PANEL_I2C_SDA_PIN    4
#define PANEL_I2C_SCL_PIN    5
#define PANEL_I2C_BAUD       400000

#define HT16K33_ADDR         0x70

#define ADS_ADDR_CH0         0x48   // pot0..pot3  (CC20..23)
#define ADS_ADDR_CH1         0x49   // pot4,pot5,slider,joyY (CC24,25,26,1)
#define ADS_ADDR_CH2         0x4A   // joyX (pitch bend), 3 spares

// ── Encoders (GP4/GP5 are I2C; GP23..25 are Pico-internal) ──────────
#define ENC_NUM              4
static const uint8_t ENC_A_PIN[ENC_NUM]  = { 0, 2, 6, 8 };
static const uint8_t ENC_B_PIN[ENC_NUM]  = { 1, 3, 7, 9 };
static const uint8_t ENC_SW_PIN[ENC_NUM] = { 10, 11, 12, 13 };

// ── HT16K33 key data RAM read ───────────────────────────────────────
// Datasheet: 13x3 key matrix, key RAM at pointer 0x40. Three rows of 13
// bits arrive as 6 bytes (row-major, odd bytes hold bits 12..8).
// If hardware disagrees, this is the one knob to turn.
#define HT16K33_KEY_BYTES    6

// ── Matrix cell → panel key index map ───────────────────────────────
// Cell index = row*13 + col (row 0..2, col 0..12).
// Panel key indices: 0..15 white, 16..26 black, 27..30 transport,
// 31 MODE, 32/33 </>, 34..37 soft S1..S4, 38/39 shifts (protocol doc).
// Default: identity — the keyboard PCB is routed to honor it.
#define KEYMAP_CELLS         39
static const uint8_t KEYMAP[KEYMAP_CELLS] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12,   // row 0
   13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,   // row 1
   26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,   // row 2
};
