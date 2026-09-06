#include "keys.h"
#include "board_config.h"
#include "config.h"
#include "midi.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define DEBOUNCE_TICKS  4       // ~4 ms (4 stable reads; all chips scanned
                                // every 1 ms tick). MX bounce spec — do not
                                // go below 4. Uniform for every key.

// MCP23017 registers (BANK=0, sequential A then B)
#define MCP_IODIRA   0x00
#define MCP_GPPUA    0x0C
#define MCP_GPIOA    0x12

static bool stable[KEY_CHIP_COUNT][16];
static uint8_t seen[KEY_CHIP_COUNT][16];
static bool chip_offline[KEY_CHIP_COUNT];   // probe failed or chip dropped off the bus

static int write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    const uint8_t w[2] = { reg, val };
    return i2c_write_timeout_us(PANEL_I2C, addr, w, 2, false,
                                PANEL_I2C_TIMEOUT_US);
}

void keys_init(void) {
    for (unsigned c = 0; c < KEY_CHIP_COUNT; c++) {
        const uint8_t addr = KEY_CHIPS[c].addr;
        // Probe: the IODIRA write doubles as the first config write. NACK or
        // timeout -> chip absent, skip it forever after (bounded bring-up).
        chip_offline[c] = write_reg(addr, MCP_IODIRA, 0xFF) < 0;
        if (chip_offline[c]) continue;
        write_reg(addr, MCP_IODIRA + 1, 0xFF); // GPB inputs (IODIRB)
        write_reg(addr, MCP_GPPUA, 0xFF);    // pull-ups A
        write_reg(addr, MCP_GPPUA + 1, 0xFF); // pull-ups B
        for (unsigned k = 0; k < 16; k++) {
            stable[c][k] = false;
            seen[c][k] = 0;
        }
    }
}

bool keys_any_offline(void) {
    for (unsigned c = 0; c < KEY_CHIP_COUNT; c++)
        if (chip_offline[c]) return true;
    return false;
}

static void fire(uint8_t key, bool pressed) {
    if (key == 0xFF || key >= KEY_COUNT) return;
    if (pressed) midi_send_note_on(0, 36 + key, config_velocity());
    else         midi_send_note_off(0, 36 + key);
}

static void scan_chip(unsigned c) {
    if (chip_offline[c]) return;               // zero I2C traffic to absent chips
    const uint8_t addr = KEY_CHIPS[c].addr;

    uint8_t ptr = MCP_GPIOA;
    uint8_t d[2];
    if (i2c_write_timeout_us(PANEL_I2C, addr, &ptr, 1, true,
                             PANEL_I2C_TIMEOUT_US) < 0) {
        chip_offline[c] = true;
        return;
    }
    if (i2c_read_timeout_us(PANEL_I2C, addr, d, 2, false,
                            PANEL_I2C_TIMEOUT_US) < 0) {
        chip_offline[c] = true;
        return;
    }
    const uint16_t raw = (uint16_t)(d[0] | (d[1] << 8));

    for (unsigned k = 0; k < 16; k++) {
        const uint8_t key  = KEY_CHIPS[c].map[k];
        const uint8_t push = PUSH_MAP[c][k];
        if (key == 0xFF && push == 0xFF) continue;
        const bool pressed = ((raw >> k) & 1u) == 0u;   // active low
        if (pressed == stable[c][k]) { seen[c][k] = 0; continue; }
        if (++seen[c][k] >= DEBOUNCE_TICKS) {
            stable[c][k] = pressed;
            seen[c][k] = 0;
            // Keys emit notes; encoder pushes (rev B2) emit CC 32+push.
            if (key != 0xFF) fire(key, pressed);
            else midi_send_cc(0, (uint8_t)(32 + push), pressed ? 127 : 0);
        }
    }
}

void keys_tick(void) {
    // All four expanders every 1 ms tick: 4 x ~125 us of I2C at 400 kHz
    // (~0.5 ms total) — half the tick budget, tud_task still serviced
    // between ticks. Offline chips are skipped before any bus traffic.
    for (unsigned c = 0; c < KEY_CHIP_COUNT; c++) scan_chip(c);
}
