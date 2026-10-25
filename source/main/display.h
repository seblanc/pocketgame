#pragma once
#include "board.h"
#include "esp_err.h"
#include "game.h"

#define SCREEN_W 240
#define SCREEN_H 240

/* grid geometry, shared with touch hit-testing */
#define GRID_X0 21
#define GRID_Y0 39
#define CELL_PX 66

typedef struct {
    game_t game;
    int wins_x, wins_o;
    bool calibrating; /* backlight search running: the next tap confirms the lit screen */
} view_t;

esp_err_t display_init(const board_profile_t *p);
void display_render(const view_t *v);
/* cell index under screen point, or -1 */
int display_cell_at(int x, int y);
