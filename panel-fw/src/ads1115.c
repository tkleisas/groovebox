#include "ads1115.h"
#include "board_config.h"
#include "midi.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

// Three ADS1115 in continuous mode, 860 SPS, +/-4.096 V PGA. Each 1 ms tick
// reads one chip (last conversion) and advances its MUX, so every channel
// refreshes about every 12 ms (~83 Hz) — ample for pots and bend.
// Single-ended 16-bit reads: raw spans 0..32767 (0 V..PGA full scale).

#define DR_860        0x07
#define PGA_4096      0x01    // +/-4.096 V (value 0x01; 0x02 would be +/-2.048 V)
#define ADS_CONFIG(mux) (uint16_t)(0x8000 | ((4 + (mux)) << 12) | \
                        (PGA_4096 << 9) | (0u << 8) | (DR_860 << 5))

static const uint8_t chip_addr[3] = { ADS_ADDR_CH0, ADS_ADDR_CH1, ADS_ADDR_CH2 };
static const int8_t ch_map[3][4] = {
    { ADS_POT0, ADS_POT1, ADS_POT2, ADS_POT3 },
    { ADS_POT4, ADS_POT5, ADS_SLIDER, ADS_JOY_Y },
    { ADS_JOY_X, -1, -1, -1 },
};

static uint8_t mux[3];
static uint16_t value[ADS_CH_COUNT];
static uint16_t last_sent[ADS_CH_COUNT];
static unsigned chip_rr;

static void write_config(uint8_t addr, uint8_t m) {
    const uint16_t cfg = ADS_CONFIG(m);
    const uint8_t w[3] = { 0x01, (uint8_t)(cfg >> 8), (uint8_t)cfg };
    i2c_write_blocking(PANEL_I2C, addr, w, 3, false);
}

static void process_channel(uint8_t ch, uint16_t raw) {
    if (ch == ADS_JOY_X) {                       // pitch bend, spring-centered
        static uint16_t bend = 8192;
        uint16_t b;
        const uint16_t center = ADS_FULL_SCALE_RAW / 2;          // 13200
        if (raw >= center - 300 && raw <= center + 300) b = 8192;  // snap to center
        else {
            b = (uint16_t)((uint32_t)raw * 16383 / ADS_FULL_SCALE_RAW);
            if (b > 16383) b = 16383;            // raw above full scale: clamp
        }
        if (b != 8192 && b > bend - 32 && b < bend + 32) return;
        if (b == bend) return;
        bend = b;
        midi_send_bend(0, bend);
        return;
    }
    const uint16_t diff = raw > last_sent[ch] ? raw - last_sent[ch]
                                              : last_sent[ch] - raw;
    if (diff < (ch == ADS_JOY_Y ? 128u : 64u)) return;           // hysteresis
    last_sent[ch] = raw;
    const uint8_t cc = ch <= ADS_POT5 ? (uint8_t)(20 + ch)
                     : ch == ADS_SLIDER ? 26 : 1;
    uint8_t v = (uint8_t)((uint32_t)raw * 127 / ADS_FULL_SCALE_RAW);
    if (v > 127) v = 127;                        // raw above full scale: clamp
    midi_send_cc(0, cc, v);
}

void ads_init(void) {
    for (unsigned c = 0; c < 3; c++) write_config(chip_addr[c], 0);
}

uint16_t ads_value(uint8_t ch) {
    return ch < ADS_CH_COUNT ? value[ch] : 0;
}

void ads_tick(void) {
    const uint8_t addr = chip_addr[chip_rr];
    const uint8_t m = mux[chip_rr];
    const int8_t ch = ch_map[chip_rr][m];

    uint8_t reg = 0x00;
    uint8_t d[2];
    if (i2c_write_blocking(PANEL_I2C, addr, &reg, 1, true) >= 0 &&
        i2c_read_blocking(PANEL_I2C, addr, d, 2, false) >= 0) {
        const uint16_t raw = (uint16_t)((d[0] << 8) | d[1]);
        if (ch >= 0) {
            value[ch] = raw;
            process_channel((uint8_t)ch, raw);
        }
    }
    mux[chip_rr] = (uint8_t)((m + 1) & 3);
    write_config(addr, mux[chip_rr]);
    chip_rr = (chip_rr + 1) % 3;
}
