/* Compile-time/runtime consistency checks for panel-fw/src/board_config.h. */
#include <stdint.h>
#include <stdio.h>

#include "board_config.h"

_Static_assert(KEY_CHIP_COUNT == 5, "rev B2 has five expanders");
_Static_assert(sizeof(KEY_CHIPS) / sizeof(KEY_CHIPS[0]) == KEY_CHIP_COUNT,
               "KEY_CHIPS row count");
_Static_assert(sizeof(PUSH_MAP) / sizeof(PUSH_MAP[0]) == KEY_CHIP_COUNT,
               "PUSH_MAP row count");

int main(void) {
    int keys[KEY_COUNT] = {0};
    int pushes[ENC_NUM] = {0};
    for (int c = 0; c < KEY_CHIP_COUNT; c++) {
        for (int k = 0; k < 16; k++) {
            if (KEY_CHIPS[c].map[k] != 0xFF) keys[KEY_CHIPS[c].map[k]]++;
            if (PUSH_MAP[c][k] != 0xFF) pushes[PUSH_MAP[c][k]]++;
        }
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        if (keys[i] != 1) {
            printf("key %d appears %d times\n", i, keys[i]);
            return 1;
        }
    }
    for (int i = 0; i < ENC_NUM; i++) {
        if (pushes[i] != 1) {
            printf("encoder push %d appears %d times\n", i, pushes[i]);
            return 1;
        }
    }
    if (KEY_CHIPS[2].map[10] != 30) return 2;
    if (PUSH_MAP[2][11] != 0 || PUSH_MAP[2][15] != 4) return 3;
    if (PUSH_MAP[4][0] != 5 || PUSH_MAP[4][2] != 7) return 4;
    puts("board_config OK");
    return 0;
}
