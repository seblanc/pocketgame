#pragma once

typedef enum { SND_X, SND_O, SND_BAD, SND_WIN, SND_DRAW, SND_NEW } sound_t;

/* Starts the ES8311 codec and speaker amplifier; returns a short status
 * ("es8311" or "none (...)"). Needs the I2C bus. */
const char *sound_init(void);
void sound_play(sound_t s); /* non-blocking */
