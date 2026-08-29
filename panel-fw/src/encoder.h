#pragma once

#include <stdint.h>

void encoder_init(void);
void encoder_tick(void);   // emit relative CC16..19, debounced pushes CC32..35
