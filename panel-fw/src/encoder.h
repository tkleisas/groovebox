#pragma once

#include <stdint.h>

void encoder_init(void);
void encoder_tick(void);   // emit relative CC16..23 (pushes CC32..39: keys.c)
