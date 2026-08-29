// USB descriptors: TinyUSB MIDI class device "groovebox-panel".
// VID 0x2E8A is Raspberry Pi Ltd's, openly usable for Pico firmware.
// PID is an arbitrary pick — bump it if it ever collides.

#include "tusb.h"
#include "board_config.h"

#define USB_VID        0x2E8A
#define USB_PID        0x4720
#define USB_BCD        0x0200

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,           // class at interface level (IAD)
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

enum { ITF_NUM_CONTROL = 0, ITF_NUM_MIDI, ITF_NUM_TOTAL };
#define EPNUM_MIDI_OUT   0x01
#define EPNUM_MIDI_IN    0x81

uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0,
                          TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN, 0x80, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_CONTROL, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

enum { STRID_LANGID = 0, STRID_MANUF, STRID_PRODUCT, STRID_COUNT };
static char const *string_desc_arr[STRID_COUNT] = {
    (const char[]){ 0x09, 0x04 },   // English (0x0409)
    "Groovebox",
    "groovebox-panel",
};

static uint16_t desc_string[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t len = 0;
    if (index == 0) {
        desc_string[1] = 0x0409;
        len = 1;
    } else {
        if (index >= STRID_COUNT) return NULL;
        const char *s = string_desc_arr[index];
        while (*s && len < 31) desc_string[1 + len++] = (uint16_t)(*s++);
    }
    desc_string[0] = (uint16_t)(0x0300 | ((uint16_t)(2 * len + 2)));
    return desc_string;
}
