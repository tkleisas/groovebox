// groovebox-panel firmware entry point (rev B2).
// Superloop + 1 ms tick: USB MIDI via TinyUSB, 4x MCP23017 key scan (55 keys
// + 8 encoder pushes), 2x ADS1115 analog polling, 8 quadrature encoders,
// HT16K33 LED output (32 LEDs), GRV config service. Watchdog (4 s) kicked
// every loop iteration.
// USB comes up FIRST and independently of the peripherals: every I2C chip is
// probed with a bounded timeout and skipped when absent, so the device
// enumerates with any subset of the panel attached (bare-board bring-up).
// GP25 heartbeat: slow blink = all peripherals online, fast blink = degraded.

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
    tusb_init();

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

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // All init I2C is bounded (PANEL_I2C_TIMEOUT_US), so this point is
    // reached in milliseconds even with every peripheral absent.
    watchdog_enable(4000, true);    // 4 s, paused while a debugger is attached

    unsigned hb_ms = 0;
    bool hb = false;
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
            const bool degraded =
                keys_any_offline() || !leds_online() || ads_any_offline();
            if (++hb_ms >= (degraded ? 100u : 500u)) {
                hb_ms = 0;
                hb = !hb;
                gpio_put(PICO_DEFAULT_LED_PIN, hb);
            }
        }
    }
    return 0;
}
