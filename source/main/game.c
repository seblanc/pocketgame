#include "game.h"
#include <string.h>

static const uint8_t LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
    {0, 4, 8}, {2, 4, 6},
};

void game_start(game_t *g, uint8_t first)
{
    memset(g, 0, sizeof(*g));
    g->turn = g->starter = first == CELL_O ? CELL_O : CELL_X;
    g->status = GAME_PLAYING;
}

void game_new(game_t *g) { game_start(g, CELL_X); }

uint8_t game_other(uint8_t mark) { return mark == CELL_X ? CELL_O : CELL_X; }

move_result_t game_play(game_t *g, int cell)
{
    if (g->status != GAME_PLAYING) {
        return MOVE_ERR_OVER;
    }
    if (cell < 0 || cell > 8) {
        return MOVE_ERR_RANGE;
    }
    if (g->cells[cell] != CELL_EMPTY) {
        return MOVE_ERR_OCCUPIED;
    }
    uint8_t mark = g->turn;
    g->cells[cell] = mark;
    g->moves++;

    for (int i = 0; i < 8; i++) {
        const uint8_t *l = LINES[i];
        if (g->cells[l[0]] == mark && g->cells[l[1]] == mark && g->cells[l[2]] == mark) {
            g->status = GAME_WIN;
            g->winner = mark;
            memcpy(g->win_line, l, 3);
            return MOVE_OK;
        }
    }
    if (g->moves == 9) {
        g->status = GAME_DRAW;
        return MOVE_OK;
    }
    g->turn = game_other(mark);
    return MOVE_OK;
}

char game_mark_char(uint8_t mark)
{
    return mark == CELL_X ? 'X' : mark == CELL_O ? 'O' : '.';
}

void game_board_str(const game_t *g, char *out)
{
    for (int i = 0; i < 9; i++) {
        out[i] = game_mark_char(g->cells[i]);
    }
    out[9] = '\0';
}
