#pragma once
#include <stdbool.h>

typedef enum { SND_X, SND_O, SND_BAD, SND_WIN, SND_DRAW, SND_NEW, SND_TICK } sound_t;

/* Starts the ES8311 codec and speaker amplifier; returns a short status
 * ("es8311" or "none (...)"). Needs the I2C bus. */
const char *sound_init(void);
void sound_play(sound_t s); /* non-blocking; silent at volume 0 */
/* volume 0..100 (codec DAC gain, 0 = silent); may be called before sound_init */
void sound_set_level(int vol);
