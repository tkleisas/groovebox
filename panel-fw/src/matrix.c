#include "matrix.h"
#include "board_config.h"
#include "midi.h"
#include "config.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define DEBOUNCE_TICKS  3
#define NOTE_BASE       36

static bool stable_key[KEYMAP_CELLS];
static bool cand[KEYMAP_CELLS];
static uint8_t seen[KEYMAP_CELLS];

void matrix_init(void) {
    static const uint8_t setup[] = { 0x21, 0x81, 0xE1 };  // osc, display, dim
    for (unsigned i = 0; i < sizeof(setup); i++)
        i2c_write_blocking(PANEL_I2C, HT16K33_ADDR, &setup[i], 1, false);
    for (int c = 0; c < KEYMAP_CELLS; c++) stable_key[c] = false;
}

static void fire(uint8_t cell, bool pressed) {
    uint8_t key = KEYMAP[cell];
    if (key == 0xFF || key >= 40) return;
    if (pressed) midi_send_note_on(0, NOTE_BASE + key, config_velocity());
    else         midi_send_note_off(0, NOTE_BASE + key);
}

void matrix_tick(void) {
    uint8_t ptr = 0x40;
    uint8_t data[HT16K33_KEY_BYTES];
    if (i2c_write_blocking(PANEL_I2C, HT16K33_ADDR, &ptr, 1, true) < 0) return;
    if (i2c_read_blocking(PANEL_I2C, HT16K33_ADDR, data, sizeof(data), false) < 0)
        return;

    // key RAM: 6 bytes, row-major; even byte = row cols 0..7 (bit = col),
    // odd byte = row cols 8..12 (bits 0..4; 13x3 matrix, rest unused).
    for (unsigned b = 0; b < sizeof(data); b++) {
        const unsigned row = b >> 1;
        const uint8_t bits = (b & 1) ? (uint8_t)(data[b] & 0x1F) : data[b];
        const unsigned col_base = (b & 1) ? 8u : 0u;
        const unsigned col_max = (b & 1) ? 5u : 8u;
        for (unsigned bit = 0; bit < col_max; bit++) {
            const unsigned cell = row * 13 + col_base + bit;
            const bool cur = (bits >> bit) & 1;
            if (cur == stable_key[cell]) { seen[cell] = 0; continue; }
            if (seen[cell] == 0) cand[cell] = cur;
            if (cand[cell] == cur) {
                if (++seen[cell] >= DEBOUNCE_TICKS) {
                    stable_key[cell] = cur;
                    seen[cell] = 0;
                    fire((uint8_t)cell, cur);
                }
            } else {
                seen[cell] = 1;
                cand[cell] = cur;
            }
        }
    }
}
