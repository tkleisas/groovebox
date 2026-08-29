#include "encoder.h"
#include "board_config.h"
#include "midi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// Quadrature: full 4-state table on A/B edges (IRQ), detents emitted from
// the 1 ms tick so MIDI stays out of IRQ context. Pushes are polled here
// with a 30 ms debounce (active low, internal pull-ups).

static const int8_t qdec[16] = {  0, -1,  1,  0,
                                  1,  0,  0, -1,
                                 -1,  0,  0,  1,
                                  0,  1, -1,  0 };

static void irq_cb(uint gpio, uint32_t events);

static int8_t delta[ENC_NUM];
static uint8_t prev[ENC_NUM];

static bool sw_stable[ENC_NUM];
static uint8_t sw_seen[ENC_NUM];

void encoder_init(void) {
    for (unsigned e = 0; e < ENC_NUM; e++) {
        gpio_init(ENC_A_PIN[e]); gpio_pull_up(ENC_A_PIN[e]);
        gpio_init(ENC_B_PIN[e]); gpio_pull_up(ENC_B_PIN[e]);
        gpio_init(ENC_SW_PIN[e]); gpio_pull_up(ENC_SW_PIN[e]);
        prev[e] = (uint8_t)((gpio_get(ENC_A_PIN[e]) << 1) | gpio_get(ENC_B_PIN[e]));
        gpio_set_irq_enabled_with_callback(
            ENC_A_PIN[e], GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &irq_cb);
        gpio_set_irq_enabled(
            ENC_B_PIN[e], GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    }
}

static void irq_cb(uint gpio, uint32_t events) {
    (void)events;
    for (unsigned e = 0; e < ENC_NUM; e++) {
        uint8_t a = ENC_A_PIN[e], b = ENC_B_PIN[e];
        if (gpio != a && gpio != b) continue;
        const uint8_t cur = (uint8_t)((gpio_get(a) << 1) | gpio_get(b));
        delta[e] += qdec[(prev[e] << 2) | cur];
        prev[e] = cur;
        return;
    }
}

void encoder_tick(void) {
    for (unsigned e = 0; e < ENC_NUM; e++) {
        while (delta[e] > 0)  { midi_send_cc(0, (uint8_t)(16 + e), 65); delta[e]--; }
        while (delta[e] < 0)  { midi_send_cc(0, (uint8_t)(16 + e), 63); delta[e]++; }

        const bool pressed = !gpio_get(ENC_SW_PIN[e]);
        if (pressed != sw_stable[e]) {
            if (++sw_seen[e] >= 30) {           // 30 ms at 1 ms ticks
                sw_stable[e] = pressed;
                sw_seen[e] = 0;
                midi_send_cc(0, (uint8_t)(32 + e), pressed ? 127 : 0);
            }
        } else sw_seen[e] = 0;
    }
}
