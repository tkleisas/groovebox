#include "leds.h"
#include "board_config.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

// LED mapping: LED i sits under white key i. Display RAM address a bit m
// drives ROW a, COM m (HT16K33 RAM is 16 bytes x 8 bits = 8 COM x 16 ROW).
// We use ROW0..7 x COM0..1: LED i -> RAM[i & 7] bit (i >> 3).
// The keyboard PCB wires LED i anode to ROW(i & 7), cathode (via resistor)
// to COM(i >> 3). If the layout differs, change this function only.

static uint8_t ram[16];
static bool dirty;
static int quiesce;   // periodic re-flush counter (corruption self-heal)

static void cmd(uint8_t c) {
    i2c_write_blocking(PANEL_I2C, HT16K33_ADDR, &c, 1, false);
}

static void flush(void) {
    uint8_t buf[17];
    buf[0] = 0x00;                       // pointer + autoincrement
    for (int a = 0; a < 16; a++) buf[1 + a] = ram[a];
    i2c_write_blocking(PANEL_I2C, HT16K33_ADDR, buf, sizeof(buf), false);
}

void leds_init(void) {
    // The chip boots in standby (oscillator off, display off): without these
    // writes RAM updates land but no LED ever lights.
    cmd(0x21);                           // system setup: oscillator on
    cmd(0xE0 | 8);                       // dimming set: brightness 8/16
    cmd(0x81);                           // display setup: on, no blink
    for (int i = 0; i < 16; i++) ram[i] = 0;
    dirty = true;                        // push the cleared RAM on first tick
    quiesce = 0;
}

void leds_set(uint8_t index, bool on) {
    if (index >= 16) return;
    uint8_t mask = (uint8_t)(1u << (index >> 3));
    uint8_t *cell = &ram[index & 7];
    if (on ? (*cell & mask) != 0 : (*cell & mask) == 0) return;
    if (on) *cell |= mask; else *cell &= (uint8_t)~mask;
    dirty = true;
}

void leds_tick(void) {
    if (dirty) { flush(); dirty = false; quiesce = 0; return; }
    if (++quiesce >= 256) { flush(); quiesce = 0; }   // ~0.25 s re-assert
}
