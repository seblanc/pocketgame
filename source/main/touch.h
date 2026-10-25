#pragma once
#include <stdbool.h>
#include "board.h"

/* Starts the touch controller; returns false if there is none. */
bool touch_init(const touch_info_t *t);
/* Polls once; true with the coordinates on a new press (finger down edge). */
bool touch_poll_press(int *x, int *y);
