// groovebox-panel firmware entry point.
// Superloop + 1 ms tick: USB MIDI via TinyUSB, 3x MCP23017 key scan, 3x
// ADS1115 analog polling, quadrature encoders, HT16K33 LED output, GRV
// config service. Watchdog (4 s) kicked every loop iteration.

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#include "ads1115.h"
#include "board_config.h"
#include "config.h"
#include "encoder.h"
#include "keys.h"
#include "leds.h"
#include "midi.h"

int main(void) {
    config_init();

    i2c_init(PANEL_I2C, PANEL_I2C_BAUD);
    gpio_set_function(PANEL_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PANEL_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PANEL_I2C_SDA_PIN);
    gpio_pull_up(PANEL_I2C_SCL_PIN);

    leds_init();
    keys_init();
    ads_init();
    encoder_init();
    tusb_init();
    watchdog_enable(4000, true);    // 4 s, paused while a debugger is attached

    absolute_time_t next_tick = make_timeout_time_ms(1);
    while (true) {
        watchdog_update();
        tud_task();
        midi_task();
        if (time_reached(next_tick)) {
            next_tick = delayed_by_us(next_tick, 1000);
            keys_tick();
            ads_tick();
            encoder_tick();
            leds_tick();
            config_service();
        }
    }
    return 0;
}
