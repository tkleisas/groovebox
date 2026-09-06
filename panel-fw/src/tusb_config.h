#pragma once

// TinyUSB configuration for the groovebox-panel MIDI device.

#include "pico.h"

#define CFG_TUSB_MCU               OPT_MCU_RP2040
#define CFG_TUSB_OS                OPT_OS_PICO

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN         __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENABLED            1
#define CFG_TUD_ENDPOINT0_SIZE     64

// TinyUSB 0.15 (pico-sdk 1.5.x): tusb_init() only brings up the device stack
// when a port mode is declared — without this, tusb_init() is a silent no-op
// (returns true, USB never connects). Mirrors the SDK's own device config
// (pico_stdio_usb/include/tusb_config.h).
#define CFG_TUSB_RHPORT0_MODE      (OPT_MODE_DEVICE)

// One MIDI device, one jack.
#define CFG_TUD_MIDI               1
#define CFG_TUD_MIDI_EP_BUFSIZE    64
#define CFG_TUD_MIDI_RX_BUFSIZE    64
#define CFG_TUD_MIDI_TX_BUFSIZE    64
