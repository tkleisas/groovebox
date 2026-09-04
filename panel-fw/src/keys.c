#include "keys.h"
#include "board_config.h"
#include "config.h"
#include "midi.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define DEBOUNCE_TICKS  8       // ~24 ms (8 hits at one scan per chip per 3 ms),
                                // uniform for every key

// MCP23017 registers (BANK=0, sequential A then B)
#define MCP_IODIRA   0x00
#define MCP_GPPUA    0x0C
#define MCP_GPIOA    0x12

static bool stable[3][16];
static uint8_t seen[3][16];
static unsigned chip_rr;

static void write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    const uint8_t w[2] = { reg, val };
    i2c_write_blocking(PANEL_I2C, addr, w, 2, false);
}

void keys_init(void) {
    for (unsigned c = 0; c < 3; c++) {
        const uint8_t addr = KEY_CHIPS[c].addr;
        write_reg(addr, MCP_IODIRA, 0xFF);   // GPA inputs
        write_reg(addr, MCP_IODIRA + 1, 0xFF); // GPB inputs (IODIRB)
        write_reg(addr, MCP_GPPUA, 0xFF);    // pull-ups A
        write_reg(addr, MCP_GPPUA + 1, 0xFF); // pull-ups B
        for (unsigned k = 0; k < 16; k++) {
            stable[c][k] = false;
            seen[c][k] = 0;
        }
    }
}

static void fire(uint8_t key, bool pressed) {
    if (key == 0xFF || key >= 40) return;
    if (pressed) midi_send_note_on(0, 36 + key, config_velocity());
    else         midi_send_note_off(0, 36 + key);
}

void keys_tick(void) {
    const unsigned c = chip_rr;
    chip_rr = (chip_rr + 1) % 3;
    const uint8_t addr = KEY_CHIPS[c].addr;

    uint8_t ptr = MCP_GPIOA;
    uint8_t d[2];
    if (i2c_write_blocking(PANEL_I2C, addr, &ptr, 1, true) < 0) return;
    if (i2c_read_blocking(PANEL_I2C, addr, d, 2, false) < 0) return;
    const uint16_t raw = (uint16_t)(d[0] | (d[1] << 8));

    for (unsigned k = 0; k < 16; k++) {
        const uint8_t key = KEY_CHIPS[c].map[k];
        if (key == 0xFF) continue;
        const bool pressed = ((raw >> k) & 1u) == 0u;   // active low
        if (pressed == stable[c][k]) { seen[c][k] = 0; continue; }
        if (++seen[c][k] >= DEBOUNCE_TICKS) {
            stable[c][k] = pressed;
            seen[c][k] = 0;
            fire(key, pressed);
        }
    }
}
