#pragma once
#include "board.h"

typedef enum { SND_X, SND_O, SND_BAD, SND_WIN, SND_DRAW, SND_NEW } sound_t;

/* Returns a short name of the audio output in use ("i2s", "buzzer", "none"). */
const char *sound_init(const board_profile_t *p);
void sound_play(sound_t s); /* non-blocking */
