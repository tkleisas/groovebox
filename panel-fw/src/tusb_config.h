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

// One MIDI device, one jack.
#define CFG_TUD_MIDI               1
#define CFG_TUD_MIDI_EP_BUFSIZE    64
#define CFG_TUD_MIDI_RX_BUFSIZE    64
#define CFG_TUD_MIDI_TX_BUFSIZE    64
