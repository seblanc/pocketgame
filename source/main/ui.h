#pragma once
#include "app.h"

typedef enum {
    HIT_NONE,
    HIT_TILE,       /* arg = game index */
    HIT_CELL,       /* arg = cell 0..8 */
    HIT_NEW_GAME,   /* tap anywhere after a finished game */
    HIT_YES,
    HIT_NO,
    HIT_SLIDER,     /* arg = volume 0..100 */
    HIT_WIFI_SETUP,
    HIT_WIFI_DONE,
} hit_t;

/* Draws the whole screen of v and sends it to the panel. */
void ui_render(const app_view_t *v);
/* What a tap at x,y on v's screen means. */
hit_t ui_hit(const app_view_t *v, int x, int y, int *arg);
/* volume for a finger at x on the slider */
int ui_slider_value(int x);
