#pragma once

// Panel configuration state: velocity source routing + persistence,
// driven by GRV SysEx (docs/panel-protocol.md, "Panel configuration").

#include <stdbool.h>
#include <stdint.h>

enum {
    VSRC_SLIDER = 0,        // power-up default (rev A behavior)
    VSRC_POT0 = 1, VSRC_POT1, VSRC_POT2, VSRC_POT3, VSRC_POT4, VSRC_POT5,
                            // rev B2: only 0x01/0x02 (CUT/RES) are real pots;
                            // 0x03-0x06 clamp to VSRC_POT1 (see config.c)
    VSRC_JOY_Y  = 7,
    VSRC_FIXED  = 8,        // fixed value (config.fixed)
    VSRC_HOST   = 9,        // host register (CC 27, channel 2)
};

void config_init(void);          // load persisted config if valid
uint8_t config_velocity(void);   // value stamped on Note On, 1..127
void config_set_host_velocity(uint8_t v);
bool config_handle_sysex(const uint8_t *inner, uint32_t len); // between F0/F7
void config_service(void);       // call every 1 ms: coalesced flash commit
