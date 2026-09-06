#include "midi.h"
#include "tusb.h"
#include "leds.h"
#include "config.h"

#include <string.h>

// TinyUSB stream_write returns the byte count actually accepted; a short
// count means the TX FIFO is full (host not draining). Channel messages are
// 3 bytes = one USB-MIDI packet, so a failed write queues nothing and the
// stream stays consistent; we drop the message whole (never retry a partial
// frame) and count the drop. A short SysEx likewise abandons that frame —
// the config reply is idempotent and the brain re-queries.
static unsigned tx_drops;

static void send(const uint8_t *m, uint32_t n) {
    if (tud_midi_n_stream_write(0, 0, m, n) != n) tx_drops++;
}

void midi_send_note_on(uint8_t ch, uint8_t note, uint8_t velocity) {
    uint8_t m[3] = { (uint8_t)(0x90 | ch), note, velocity };
    send(m, 3);
}

void midi_send_note_off(uint8_t ch, uint8_t note) {
    uint8_t m[3] = { (uint8_t)(0x80 | ch), note, 0 };
    send(m, 3);
}

void midi_send_cc(uint8_t ch, uint8_t cc, uint8_t value) {
    uint8_t m[3] = { (uint8_t)(0xB0 | ch), cc, value };
    send(m, 3);
}

void midi_send_bend(uint8_t ch, uint16_t value14) {
    uint8_t m[3] = { (uint8_t)(0xE0 | ch),
                     (uint8_t)(value14 & 0x7F),
                     (uint8_t)(value14 >> 7) };
    send(m, 3);
}

void midi_send_sysex(const uint8_t *inner, uint32_t len) {
    uint8_t m[40];
    if (len > 38) return;
    m[0] = 0xF0;
    memcpy(&m[1], inner, len);
    m[len + 1] = 0xF7;
    send(m, len + 2);
}

// ── RX parser ───────────────────────────────────────────────────────
// Status byte + running status for the channel messages we care about;
// GRV SysEx frames accumulate up to 32 bytes. Realtime bytes are skipped.

#define SX_MAX 32
static uint8_t sx_buf[SX_MAX];
static uint32_t sx_len;
static bool sx_active;

static uint8_t last_status;
static uint8_t msg[2];
static uint8_t msg_need, msg_got;

static void dispatch(void) {
    uint8_t st = last_status & 0xF0;
    uint8_t ch = last_status & 0x0F;
    if (ch != 1) return;                      // only channel 2 inbound
    if (st == 0x90 && msg[1] > 0) {           // Note On -> LED index 0..31
        if (msg[0] < LED_COUNT) leds_set(msg[0], true);
    } else if (st == 0x80 || (st == 0x90 && msg[1] == 0)) {
        if (msg[0] < LED_COUNT) leds_set(msg[0], false);
    } else if (st == 0xB0 && msg[0] == 27) {  // host velocity register
        config_set_host_velocity(msg[1]);
    }
}

static void feed(uint8_t b) {
    if (b >= 0xF8) return;                    // realtime: ignore
    if (b == 0xF7) {                          // end of SysEx
        if (sx_active) config_handle_sysex(sx_buf, sx_len);
        sx_active = false;
        sx_len = 0;
        return;
    }
    if (b == 0xF0) {
        sx_active = true;
        sx_len = 0;
        msg_got = 0;
        return;
    }
    if (sx_active) {
        if (sx_len < SX_MAX) sx_buf[sx_len++] = b;
        else sx_active = false;               // oversized: drop silently
        return;
    }
    if (b & 0x80) {
        last_status = b;
        msg_got = 0;
        switch (b & 0xF0) {
        case 0x80: case 0x90: case 0xB0: case 0xE0: msg_need = 2; break;
        default: msg_need = 0; break;         // others not used by the panel
        }
        return;
    }
    // data byte (running status)
    if (msg_need == 0) return;
    msg[msg_got++] = b;
    if (msg_got == msg_need) { dispatch(); msg_got = 0; }
}

void midi_task(void) {
    uint8_t buf[64];
    uint32_t n = tud_midi_n_stream_read(0, 0, buf, sizeof(buf));
    for (uint32_t i = 0; i < n; i++) feed(buf[i]);
}
