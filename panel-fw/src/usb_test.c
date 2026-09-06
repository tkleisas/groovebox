// usb_test: minimal known-good TinyUSB MIDI device for enumeration isolation.
// Same VID/PID as groovebox-panel, product string "gb-usb-test". No I2C, no
// peripherals — just tusb_init + tud_task. GP25: on while USB mounted,
// slow blink otherwise. Flash build/usb_test.uf2: if the host enumerates
// "gb-usb-test", the toolchain/SDK/USB path is good and any remaining
// failure is in the panel firmware; if not, the issue is environmental.

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "tusb.h"

#define USB_VID        0x2E8A
#define USB_PID        0x4720

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
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

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0,
                          TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN, 0x80, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_CONTROL, 0, 0x01, 0x81, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_fs_configuration;
}

static uint16_t desc_string[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static char const *const arr[] = { NULL, "Groovebox", "gb-usb-test" };
    uint8_t len = 0;
    if (index == 0) {
        desc_string[1] = 0x0409;
        len = 1;
    } else {
        if (index >= 3) return NULL;
        const char *s = arr[index];
        while (*s && len < 31) desc_string[1 + len++] = (uint16_t)(*s++);
    }
    desc_string[0] = (uint16_t)(0x0300 | ((uint16_t)(2 * len + 2)));
    return desc_string;
}

int main(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    tusb_init();

    unsigned hb_ms = 0;
    bool hb = false;
    absolute_time_t next_tick = make_timeout_time_ms(1);
    while (true) {
        tud_task();
        if (time_reached(next_tick)) {
            next_tick = delayed_by_us(next_tick, 1000);
            if (tud_mounted()) {
                gpio_put(PICO_DEFAULT_LED_PIN, true);       // solid = mounted
            } else if (++hb_ms >= 500) {                    // slow blink = waiting
                hb_ms = 0;
                hb = !hb;
                gpio_put(PICO_DEFAULT_LED_PIN, hb);
            }
        }
    }
    return 0;
}
