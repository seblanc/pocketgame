#include "tictactoe.h"
#include <string.h>

static const uint8_t LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
    {0, 4, 8}, {2, 4, 6},
};

void ttt_start(ttt_t *g, uint8_t first)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
}

void ttt_new(ttt_t *g) { ttt_start(g, CELL_X); }

move_result_t ttt_play(ttt_t *g, int cell)
{
    if (g->r.status != GAME_PLAYING) {
        return MOVE_ERR_OVER;
    }
    if (cell < 0 || cell > 8) {
        return MOVE_ERR_RANGE;
    }
    if (g->cells[cell] != CELL_EMPTY) {
        return MOVE_ERR_OCCUPIED;
    }
    uint8_t mark = g->r.turn;
    g->cells[cell] = mark;
    g->r.moves++;

    for (int i = 0; i < 8; i++) {
        const uint8_t *l = LINES[i];
        if (g->cells[l[0]] == mark && g->cells[l[1]] == mark && g->cells[l[2]] == mark) {
            g->r.status = GAME_WIN;
            g->r.winner = mark;
            memcpy(g->win_line, l, 3);
            return MOVE_OK;
        }
    }
    if (g->r.moves == 9) {
        g->r.status = GAME_DRAW;
        return MOVE_OK;
    }
    g->r.turn = game_other(mark);
    return MOVE_OK;
}

void ttt_board_str(const ttt_t *g, char *out)
{
    for (int i = 0; i < 9; i++) {
        out[i] = game_mark_char(g->cells[i]);
    }
    out[9] = '\0';
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <math.h>
#include <stdlib.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(ttt_t) <= GAME_STATE_MAX, "ttt_t does not fit in a game slot");

/* the grid */
#define GRID_X0 21
#define GRID_Y0 39
#define CELL_PX 66

static void ttt_new_round(void *st, uint8_t first) { ttt_start(st, first); }

static void ttt_print(const void *st)
{
    const ttt_t *g = st;
    char b[10];
    ttt_board_str(g, b);
    app_out("BOARD %s", b);
    if (g->r.status == GAME_WIN) {
        app_out("WIN %c line=%d,%d,%d", game_mark_char(g->r.winner), g->win_line[0], g->win_line[1],
                g->win_line[2]);
    } else if (g->r.status == GAME_DRAW) {
        app_out("DRAW");
    } else {
        app_out("TURN %c", game_mark_char(g->r.turn));
    }
}

static game_event_t ttt_move(void *st, int cell, const char *src)
{
    ttt_t *g = st;
    uint8_t mover = g->r.turn;
    switch (ttt_play(g, cell)) {
    case MOVE_ERR_RANGE:
        app_out("ERR bad cell");
        return GEV_BAD;
    case MOVE_ERR_OCCUPIED:
        app_out("ERR occupied");
        return GEV_BAD;
    case MOVE_ERR_OVER:
        app_out("ERR game over");
        return GEV_BAD;
    case MOVE_OK:
        break;
    }
    app_out("MOVE %c %d src=%s", game_mark_char(mover), cell, src);
    ttt_print(g);
    if (g->r.status == GAME_WIN) {
        return GEV_WIN;
    }
    if (g->r.status == GAME_DRAW) {
        return GEV_DRAW;
    }
    return mover == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
}

/* play <0-8> */
static game_event_t ttt_command(void *st, const char *arg)
{
    char *end;
    long cell = strtol(arg, &end, 10);
    if (end == arg) {
        app_out("ERR bad cell");
        return GEV_NONE;
    }
    return ttt_move(st, (int)cell, "uart");
}

static int ttt_hit(int x, int y)
{
    int col = (x - GRID_X0) / CELL_PX, row = (y - GRID_Y0) / CELL_PX;
    if (x < GRID_X0 || y < GRID_Y0 || col > 2 || row > 2) {
        return -1;
    }
    return row * 3 + col;
}

static void cell_center(int cell, float *cx, float *cy)
{
    *cx = GRID_X0 + (cell % 3) * CELL_PX + CELL_PX / 2.0f;
    *cy = GRID_Y0 + (cell / 3) * CELL_PX + CELL_PX / 2.0f;
}

static void ttt_draw(const void *st, int preview)
{
    (void)preview; /* moves on the press: never aims */
    const ttt_t *g = st;
    int grid = 3 * CELL_PX;
    for (int i = 1; i < 3; i++) {
        gfx_rect(GRID_X0 + i * CELL_PX - 1, GRID_Y0 + 6, 2, grid - 12, COL_LINE);
        gfx_rect(GRID_X0 + 6, GRID_Y0 + i * CELL_PX - 1, grid - 12, 2, COL_LINE);
    }

    bool over = g->r.status != GAME_PLAYING;
    for (int c = 0; c < 9; c++) {
        float cx, cy;
        cell_center(c, &cx, &cy);
        bool on_line = g->r.status == GAME_WIN &&
                       (c == g->win_line[0] || c == g->win_line[1] || c == g->win_line[2]);
        bool dim = over && !on_line;
        if (g->cells[c] == CELL_X) {
            uint16_t col = dim ? COL_X_DIM : COL_X;
            gfx_line(cx - 17, cy - 17, cx + 17, cy + 17, 4, col);
            gfx_line(cx - 17, cy + 17, cx + 17, cy - 17, 4, col);
        } else if (g->cells[c] == CELL_O) {
            gfx_ring(cx, cy, 19, 4, dim ? COL_O_DIM : COL_O);
        }
    }

    if (g->r.status == GAME_WIN) {
        float ax, ay, bx, by;
        cell_center(g->win_line[0], &ax, &ay);
        cell_center(g->win_line[2], &bx, &by);
        float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        float ext = 26 / len;
        gfx_line(ax - dx * ext, ay - dy * ext, bx + dx * ext, by + dy * ext, 3, COL_TEXT);
    }
}

static void ttt_icon(int x, int y, int s)
{
    float t = s / 3.0f;
    for (int i = 1; i < 3; i++) {
        gfx_rect(x + (int)(i * t), y, 1, s, COL_LINE);
        gfx_rect(x, y + (int)(i * t), s, 1, COL_LINE);
    }
    float m = t * 0.25f;
    gfx_line(x + m, y + m, x + t - m, y + t - m, 1.6f, COL_X);
    gfx_line(x + m, y + t - m, x + t - m, y + m, 1.6f, COL_X);
    gfx_ring(x + 1.5f * t, y + 1.5f * t, t * 0.3f, 1.6f, COL_O);
    gfx_line(x + 2 * t + m, y + 2 * t + m, x + 3 * t - m, y + 3 * t - m, 1.6f, COL_X);
    gfx_line(x + 2 * t + m, y + 3 * t - m, x + 3 * t - m, y + 2 * t + m, 1.6f, COL_X);
}

const game_def_t TICTACTOE = {
    .id = "ttt",
    .title = "TIC-TAC-TOE",
    .verb = "play",
    .new_round = ttt_new_round,
    .print = ttt_print,
    .move = ttt_move,
    .command = ttt_command,
    .hit = ttt_hit,
    .draw = ttt_draw,
    .icon = ttt_icon,
};
#endif
