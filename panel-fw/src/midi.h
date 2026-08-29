#pragma once

// MIDI send/receive helpers over TinyUSB (jack 0, raw byte stream API of
// tinyusb 0.14+/0.15 as bundled with pico-sdk 1.5.x).

#include <stdbool.h>
#include <stdint.h>

// TX convenience (channel is 0-based: 0 = ch1 panel events, 1 = ch2 brain side)
void midi_send_note_on(uint8_t ch, uint8_t note, uint8_t velocity);
void midi_send_note_off(uint8_t ch, uint8_t note);
void midi_send_cc(uint8_t ch, uint8_t cc, uint8_t value);
void midi_send_bend(uint8_t ch, uint16_t value14);
void midi_send_sysex(const uint8_t *inner, uint32_t len); // without F0/F7

// Pump TinyUSB RX; dispatches ch2 notes (LEDs), ch2 CC27 (host velocity
// register) and GRV SysEx frames to their owners.
void midi_task(void);
