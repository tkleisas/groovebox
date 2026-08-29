#pragma once

// Hardware-facing constants for the groovebox panel module (rev A).
// Docs: docs/panel-protocol.md, docs/design.md, hardware/netlist-spec.md.
//
// Key architecture (rev A): ALL 39 panel keys are single-ended inputs on
// three MCP23017 I2C expanders (switch to GND, expander pull-ups). No
// matrix, no diodes. The HT16K33 drives the 16 in-switch LEDs only.

#include <stdint.h>

// ── I2C bus (3x MCP23017 keys, HT16K33 LEDs, 3x ADS1115 analog) ─────
#define PANEL_I2C            i2c0
#define PANEL_I2C_SDA_PIN    4
#define PANEL_I2C_SCL_PIN    5
#define PANEL_I2C_BAUD       400000

#define HT16K33_ADDR         0x70    // LED driver only (no key scan)

#define ADS_ADDR_CH0         0x48    // pots 1-4
#define ADS_ADDR_CH1         0x49    // pots 5-6, slider, joystick Y
#define ADS_ADDR_CH2         0x4A    // joystick X (pitch bend)

#define MCP_ADDR_WHITE       0x20    // GPA0-15: white keys 0..15
#define MCP_ADDR_BLACK       0x21    // pins 0-10: black keys 16..26,
                                     // pins 11/12: shifts 38/39, 3 spare
#define MCP_ADDR_FN          0x22    // pins 0-9: function keys, pin 10:
                                     // joystick SW (index 30, reserved),
                                     // 5 spare

// Key index per expander pin; 0xFF = unused (route PCB to match).
typedef struct {
    uint8_t addr;
    uint8_t map[16];
} key_chip_t;

static const key_chip_t KEY_CHIPS[3] = {
    { MCP_ADDR_WHITE,
      { 0,  1,  2,  3,  4,  5,  6,  7,          // white keys 1-16
        8,  9, 10, 11, 12, 13, 14, 15 } },
    { MCP_ADDR_BLACK,
      { 16, 17, 18, 19, 20, 21, 22, 23,         // black keys 1-11
        24, 25, 26,
        38, 39,                                  // SHIFT-L, SHIFT-R
        0xFF, 0xFF, 0xFF } },                    // spare
    { MCP_ADDR_FN,
      { 27, 28, 29, 31, 32, 33,                  // PLAY, STOP, REC, MODE, <, >
        34, 35, 36, 37,                          // S1-S4
        30,                                      // joystick SW (reserved idx)
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },        // spare
};

// ── Encoders (GP4/5 are I2C; GP23..25 are Pico-internal) ────────────
#define ENC_NUM              4
static const uint8_t ENC_A_PIN[ENC_NUM]  = { 0, 2, 6, 8 };
static const uint8_t ENC_B_PIN[ENC_NUM]  = { 1, 3, 7, 9 };
static const uint8_t ENC_SW_PIN[ENC_NUM] = { 10, 11, 12, 13 };

// GP14..GP28: free for rev B (GP16/17 earmarked for the UART link header).

// ── HT16K33 key data RAM read ───────────────────────────────────────
// Unused in rev A (no key scan); kept for reference.
#define HT16K33_KEY_BYTES    6

// ── Panel key indices (docs/panel-protocol.md) ──────────────────────
// 0..15 white, 16..26 black, 27..30 transport (30 reserved -> joystick SW),
// 31 MODE, 32/33 </>, 34..37 soft S1..S4, 38/39 shifts.
