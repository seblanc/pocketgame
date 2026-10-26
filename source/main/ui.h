#pragma once
#include "app.h"

typedef enum {
    HIT_NONE,
    HIT_TILE,       /* arg = game index */
    HIT_MOVE,       /* arg = the game's move: cell, column... */
    HIT_AIM,        /* drag game: arg = the move under the finger (-1 none), made on release */
    HIT_NEW_GAME,   /* tap anywhere after a finished game */
    HIT_YES,
    HIT_NO,
    HIT_SLIDER,     /* on the volume bar itself; arg = volume 0..100 */
    HIT_WIFI_SETUP, /* acts on release, like HIT_TILE */
    HIT_WIFI_DONE,
    HIT_UPDATE_CHECK, /* settings list: acts on release, like HIT_TILE */
    HIT_UPDATE_BACK,
    HIT_UPDATE_GO,    /* update screen: install the newer release, or check again */
    HIT_RULES,        /* the "?" of a game with a help screen */
    HIT_RULES_DONE,
} hit_t;

/* Draws the whole screen of v and sends it to the panel. */
void ui_render(const app_view_t *v);
/* What a tap at x,y on v's screen means. */
hit_t ui_hit(const app_view_t *v, int x, int y, int *arg);
/* volume for a finger at x on the slider */
int ui_slider_value(int x);
/* the scrolling list of screen s (menu, settings): false if it has none */
bool ui_list_size(screen_t s, int *view_h, int *content_h);
