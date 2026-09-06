#pragma once

// Hardware-facing constants for the groovebox panel module (rev B2).
// Docs: docs/panel-protocol.md, docs/design.md, hardware/netlist-spec.md.
//
// Key architecture: ALL 55 panel keys are single-ended inputs on four
// MCP23017 I2C expanders (switch to GND, expander pull-ups). No matrix, no
// diodes. The HT16K33 drives the 32 LEDs only (16 step row + 16 white key).
// Rev B2: 8 encoders (quadrature on direct GPIO, pushes on expander spare
// pins), analog shrinks to 2x ADS1115 (2 pots + slider + joystick X/Y).

#include <stdint.h>

// ── I2C bus (4x MCP23017 keys, HT16K33 LEDs, 2x ADS1115 analog) ─────
#define PANEL_I2C            i2c0
#define PANEL_I2C_SDA_PIN    4
#define PANEL_I2C_SCL_PIN    5
#define PANEL_I2C_BAUD       400000

// Bounded I2C: every transaction uses a timeout so an absent or wedged chip
// (NACK / stuck bus) can never stall the superloop. ~30 us/byte at 400 kHz,
// so 2 ms is generous for the longest transfer (17-byte LED flush).
#define PANEL_I2C_TIMEOUT_US 2000

#define HT16K33_ADDR         0x70    // LED driver only (no key scan)

#define ADS_ADDR_CH0         0x48    // AIN0/1 = pots CUT/RES, AIN2 = slider,
                                     // AIN3 = joystick Y
#define ADS_ADDR_CH1         0x49    // AIN0 = joystick X (pitch bend)
#define ADS_CHIP_COUNT       2       // rev B2: 0x4A dropped

#define MCP_ADDR_WHITE       0x20    // GPA0-15: white keys 0..15
#define MCP_ADDR_BLACK       0x21    // pins 0-10: black keys 16..26,
                                     // pins 11/12: shifts 38/39,
                                     // pin 13: joystick SW (index 30),
                                     // pins 14/15: encoder pushes 1/2
#define MCP_ADDR_FN          0x22    // pins 0-9: function keys,
                                     // pins 10-15: encoder pushes 3-8
#define MCP_ADDR_STEP        0x23    // GPA0-15: step-row keys 40..55 (rev B)

#define KEY_CHIP_COUNT       4       // MCP23017 expanders on the bus
#define KEY_COUNT            56      // valid key indices 0..55 (55 keys +
                                     // joystick push on index 30)

// Key index per expander pin; 0xFF = unused (route PCB to match).
typedef struct {
    uint8_t addr;
    uint8_t map[16];
} key_chip_t;

static const key_chip_t KEY_CHIPS[KEY_CHIP_COUNT] = {
    { MCP_ADDR_WHITE,
      { 0,  1,  2,  3,  4,  5,  6,  7,          // white keys 1-16
        8,  9, 10, 11, 12, 13, 14, 15 } },
    { MCP_ADDR_BLACK,
      { 16, 17, 18, 19, 20, 21, 22, 23,         // black keys 1-11
        24, 25, 26,
        38, 39,                                  // SHIFT-L, SHIFT-R
        30,                                      // joystick SW (moved here in B2)
        0xFF, 0xFF } },                          // encoder pushes 1/2 (PUSH_MAP)
    { MCP_ADDR_FN,
      { 27, 28, 29, 31, 32, 33,                  // PLAY, STOP, REC, MODE, <, >
        34, 35, 36, 37,                          // S1-S4
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } },  // encoder pushes 3-8 (PUSH_MAP)
    { MCP_ADDR_STEP,
      { 40, 41, 42, 43, 44, 45, 46, 47,          // step row 1-16 (rev B)
        48, 49, 50, 51, 52, 53, 54, 55 } },
};

// Encoder push-buttons (rev B2): no longer direct GPIO — they ride the
// key-scan reads on spare expander pins. PUSH_MAP[c][pin] = encoder push
// index 0..7 (emitted as CC 32+index, 127 press / 0 release), 0xFF = not a
// push. Same debounce path as keys.
static const uint8_t PUSH_MAP[KEY_CHIP_COUNT][16] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,      // 0x20 white: none
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,      // 0x21 pins 14/15:
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,    0,    1 },    // pushes 1/2
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,      // 0x22 pins 10-15:
      0xFF, 0xFF,    2,    3,    4,    5,    6,    7 },    // pushes 3-8
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,      // 0x23 step: none
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
};

// ── Encoders (rev B2: 8 total) ──────────────────────────────────────
// Quadrature A/B on 16 direct GPIO; push-buttons live on MCP23017 spare
// pins (see PUSH_MAP above). Pin map:
//   enc 1-4 (pageable):  A/B = GP0/1, GP2/3, GP6/7, GP8/9   (unchanged)
//   enc 5-8 (ADSR, B2):  A/B = GP14/15, GP18/19, GP20/21, GP22/26
// Avoided: GP4/5 (I2C), GP16/17 (UART link header), GP25 (heartbeat LED).
#define ENC_NUM              8
static const uint8_t ENC_A_PIN[ENC_NUM]  = { 0, 2, 6, 8, 14, 18, 20, 22 };
static const uint8_t ENC_B_PIN[ENC_NUM]  = { 1, 3, 7, 9, 15, 19, 21, 26 };

// ── HT16K33 key data RAM read ───────────────────────────────────────
// Unused (no key scan); kept for reference.
#define HT16K33_KEY_BYTES    6

// ── Panel key indices (docs/panel-protocol.md) ──────────────────────
// 0..15 white, 16..26 black, 27..30 transport (30 reserved -> joystick SW),
// 31 MODE, 32/33 </>, 34..37 soft S1..S4, 38/39 shifts, 40..55 step row.
