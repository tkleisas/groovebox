#include "leds.h"
#include "board_config.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

// LED mapping (rev B, 32 LEDs). Logical index = ch2 note number:
// 0..15 step-row LEDs, 16..31 white-key LEDs. Display RAM address a bit m
// drives ROW a, COM m (HT16K33 RAM is 16 bytes x 8 bits = 8 COM x 16 ROW).
// Uniform mapping, continuing the rev A pattern across 4 COMs:
//     LED i -> RAM[i & 7] bit (i >> 3)        (i = 0..31)
// i.e. ROW0..7 x COM0..3. PROPOSAL — needs netlist sync: the rev A netlist
// (hardware/netlist-spec.md) wires white-key LEDs to COM0..1; under this
// uniform mapping the step row takes COM0..1 (indices 0..15) and white-key
// LEDs move to COM2..3 (indices 16..31). Update netlist-spec.md for rev B
// (or, to preserve rev A wiring byte-identical, swap the two halves here).

static uint8_t ram[16];
static bool dirty;
static int quiesce;   // periodic re-flush counter (corruption self-heal)
static bool offline;  // HT16K33 absent: keep the RAM mirror, skip the bus

static int cmd(uint8_t c) {
    return i2c_write_timeout_us(PANEL_I2C, HT16K33_ADDR, &c, 1, false,
                                PANEL_I2C_TIMEOUT_US);
}

static void flush(void) {
    if (offline) return;
    uint8_t buf[17];
    buf[0] = 0x00;                       // pointer + autoincrement
    for (int a = 0; a < 16; a++) buf[1 + a] = ram[a];
    if (i2c_write_timeout_us(PANEL_I2C, HT16K33_ADDR, buf, sizeof(buf),
                             false, PANEL_I2C_TIMEOUT_US) < 0)
        offline = true;                  // dropped off the bus mid-run
}

void leds_init(void) {
    // The chip boots in standby (oscillator off, display off): without these
    // writes RAM updates land but no LED ever lights. They double as the
    // presence probe — a NACK/timeout marks the chip offline.
    if (cmd(0x21) < 0 ||                   // system setup: oscillator on
        cmd(0xE0 | 8) < 0 ||               // dimming set: brightness 8/16
        cmd(0x81) < 0)                     // display setup: on, no blink
        offline = true;
    for (int i = 0; i < 16; i++) ram[i] = 0;
    dirty = true;                        // push the cleared RAM on first tick
    quiesce = 0;
}

bool leds_online(void) {
    return !offline;
}

void leds_set(uint8_t index, bool on) {
    if (index >= LED_COUNT) return;
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
