#include "config.h"
#include "ads1115.h"
#include "midi.h"
#include "board_config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include <string.h>

#define CFG_MAGIC       0x47525631u   // 'GRV1'
#define CFG_VERSION     1
#define FLASH_CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - 4096)
#define COMMIT_DELAY_US 1000000       // coalesce flash writes to 1/s

typedef struct {
    uint32_t magic;
    uint8_t ver, vel_src, fixed, persist;
    uint8_t crc;
} cfg_store_t;

static uint8_t g_src = VSRC_SLIDER;
static uint8_t g_fixed = 100;
static uint8_t g_persist = 0;
static uint8_t g_host_vel = 100;
static bool dirty;
static absolute_time_t dirty_since;

static uint8_t crc8(const uint8_t *d, unsigned n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *d++;
        for (int i = 0; i < 8; i++)
            c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

static void touch(void) {
    dirty = true;
    dirty_since = get_absolute_time();
}

void config_init(void) {
    const cfg_store_t *s =
        (const cfg_store_t *)(XIP_BASE + FLASH_CONFIG_OFFSET);
    if (s->magic == CFG_MAGIC && s->ver == CFG_VERSION &&
        s->crc == crc8((const uint8_t *)s, 8)) {
        if (s->vel_src <= VSRC_HOST) g_src = s->vel_src;
        if (s->fixed) g_fixed = s->fixed;
        g_persist = s->persist ? 1 : 0;
    }
}

static void defaults(void) {
    g_src = VSRC_SLIDER;
    g_fixed = 100;
    g_persist = 0;
}

uint8_t config_velocity(void) {
    uint16_t v;
    switch (g_src) {
    case VSRC_POT0: case VSRC_POT1: case VSRC_POT2:
    case VSRC_POT3: case VSRC_POT4: case VSRC_POT5:
        v = ads_value((uint8_t)(ADS_POT0 + g_src - VSRC_POT0)) >> 5; break;
    case VSRC_JOY_Y:  v = ads_value(ADS_JOY_Y) >> 5; break;
    case VSRC_SLIDER: v = ads_value(ADS_SLIDER) >> 5; break;
    case VSRC_FIXED:  v = g_fixed; break;
    case VSRC_HOST:   v = g_host_vel; break;
    default:          v = ads_value(ADS_SLIDER) >> 5; break;
    }
    if (v < 1) v = 1;
    if (v > 127) v = 127;
    return (uint8_t)v;
}

void config_set_host_velocity(uint8_t v) { g_host_vel = v; }

static void query_reply(void) {
    const uint8_t r[] = { 0x7D, 0x47, 0x52, 0x56, 0x03, CFG_VERSION, 0x07 };
    midi_send_sysex(r, sizeof(r));
}

// inner = bytes between F0 and F7
bool config_handle_sysex(const uint8_t *inner, uint32_t len) {
    if (len < 5 || inner[0] != 0x7D || inner[1] != 0x47 ||
        inner[2] != 0x52 || inner[3] != 0x56)
        return false;
    switch (inner[4]) {
    case 0x01:                                  // SET_VELOCITY_SOURCE
        if (len < 6) return true;
        if (inner[5] == VSRC_FIXED) {
            if (len < 7) return true;
            g_fixed = inner[6] ? inner[6] : 100;
            g_src = VSRC_FIXED;
        } else if (inner[5] <= VSRC_HOST) {
            g_src = inner[5];
        } else {
            g_src = VSRC_SLIDER;                // unknown src -> safe fallback
        }
        touch();
        break;
    case 0x02:                                  // QUERY
        query_reply();
        break;
    case 0x04:                                  // SET_PERSISTENCE
        if (len < 6) return true;
        g_persist = inner[5] ? 1 : 0;
        touch();
        break;
    case 0x7F:                                  // RESET_DEFAULTS
        defaults();
        touch();
        break;
    default:
        break;                                  // unknown: ignored, never NAK
    }
    return true;
}

// Runs with all interrupts masked and code executing from RAM: flash XIP is
// stalled for the duration of the erase/program, so neither IRQ handlers nor
// flash-resident instructions may run. Single-core firmware (core1 never
// launched), so masking core0 IRQs is a complete safety argument; if a rev B
// ever uses core1, switch to pico_flash's flash_safe_execute + core handshake.
static uint8_t page[256];
static void __not_in_flash_func(commit_impl)(void *unused) {
    (void)unused;
    flash_range_erase(FLASH_CONFIG_OFFSET, 4096);
    flash_range_program(FLASH_CONFIG_OFFSET, page, 256);
}

void config_service(void) {
    if (!dirty || !time_reached(delayed_by_us(dirty_since, COMMIT_DELAY_US)))
        return;
    dirty = false;
    if (g_persist) {
        memset(page, 0xFF, sizeof(page));
        cfg_store_t s;
        s.magic = CFG_MAGIC;
        s.ver = CFG_VERSION;
        s.vel_src = g_src;
        s.fixed = g_fixed;
        s.persist = g_persist;
        s.crc = crc8((const uint8_t *)&s, 8);
        memcpy(page, &s, sizeof(s));
    } else {
        page[0] = 0xFF;                         // invalidate stored config
    }
    const uint32_t irq_save = save_and_disable_interrupts();
    commit_impl(NULL);
    restore_interrupts(irq_save);
}
