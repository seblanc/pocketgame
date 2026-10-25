#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Resets the touch panel on the shared I2C bus and identifies its controller by
 * address; returns false if none answers. */
bool touch_init(void);
const char *touch_chip(void);
uint8_t touch_addr(void);
/* Polls once; true with the coordinates on a new press (finger down edge). */
bool touch_poll_press(int *x, int *y);
