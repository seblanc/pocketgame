#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "board.h"

/* The backlight pin cannot be detected electrically, so the firmware tries
 * candidate (pin, level) options one after another until the player taps the
 * lit screen; the confirmed option is saved and used on every later boot. */

/* Builds the candidate list (pins in busy are never used) and switches on the
 * saved option, or the first candidate. Returns true if a saved option exists. */
bool backlight_init(const board_t *b, uint64_t busy);
/* Removes more pins from the candidates (e.g. I2C buses found later). */
void backlight_exclude(uint64_t pins);
/* Releases the current pin and switches on the next candidate. */
void backlight_next(void);
/* Uses a given pin and on-level now (not saved). */
void backlight_set(int pin, int on);
/* Saves the current option. */
void backlight_confirm(void);
/* Drops the saved option. */
void backlight_forget(void);
void backlight_current(int *pin, int *on, int *index, int *count);
