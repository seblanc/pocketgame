#include "connect4.h"
#include <stdbool.h>
#include <string.h>

static uint8_t at(const c4_t *g, int row, int col)
{
    if (row < 0 || row >= C4_ROWS || col < 0 || col >= C4_COLS) {
        return CELL_EMPTY;
    }
    return g->cells[row * C4_COLS + col];
}

void c4_start(c4_t *g, uint8_t first)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
}

/* the run of mark through row, col along dr, dc: true and recorded if 4 or longer */
static bool check_run(c4_t *g, int row, int col, int dr, int dc, uint8_t mark)
{
    int r0 = row, c0 = col;
    while (at(g, r0 - dr, c0 - dc) == mark) {
        r0 -= dr;
        c0 -= dc;
    }
    int n = 0;
    while (at(g, r0 + n * dr, c0 + n * dc) == mark) {
        n++;
    }
    if (n < 4) {
        return false;
    }
    g->win_n = n;
    for (int i = 0; i < n; i++) {
        g->win[i] = (r0 + i * dr) * C4_COLS + c0 + i * dc;
    }
    return true;
}

c4_result_t c4_drop(c4_t *g, int col, int *row_out)
{
    if (g->r.status != GAME_PLAYING) {
        return C4_ERR_OVER;
    }
    if (col < 0 || col >= C4_COLS) {
        return C4_ERR_RANGE;
    }
    int row = 0;
    while (row < C4_ROWS && at(g, row, col) != CELL_EMPTY) {
        row++;
    }
    if (row == C4_ROWS) {
        return C4_ERR_FULL;
    }
    uint8_t mark = g->r.turn;
    g->cells[row * C4_COLS + col] = mark;
    g->r.moves++;
    if (row_out) {
        *row_out = row;
    }

    static const int8_t DIRS[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    for (int i = 0; i < 4; i++) {
        if (check_run(g, row, col, DIRS[i][0], DIRS[i][1], mark)) {
            g->r.status = GAME_WIN;
            g->r.winner = mark;
            return C4_OK;
        }
    }
    if (g->r.moves == C4_CELLS) {
        g->r.status = GAME_DRAW;
        return C4_OK;
    }
    g->r.turn = game_other(mark);
    return C4_OK;
}

int c4_str_index(int row, int col) { return (C4_ROWS - 1 - row) * C4_COLS + col; }

void c4_board_str(const c4_t *g, char *out)
{
    for (int row = 0; row < C4_ROWS; row++) {
        for (int col = 0; col < C4_COLS; col++) {
            out[c4_str_index(row, col)] = game_mark_char(at(g, row, col));
        }
    }
    out[C4_CELLS] = '\0';
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(c4_t) <= GAME_STATE_MAX, "c4_t does not fit in a game slot");

/* the grid, below the turn banner */
#define C4_PX 32
#define C4_X0 ((SCREEN_W - C4_COLS * C4_PX) / 2)
#define C4_Y0 42
#define DISC_R 12.5f

static void c4_new_round(void *st, uint8_t first) { c4_start(st, first); }

static void c4_print(const void *st)
{
    const c4_t *g = st;
    char b[C4_CELLS + 1];
    c4_board_str(g, b);
    app_out("BOARD %s", b);
    if (g->r.status == GAME_WIN) {
        char cells[40];
        int n = 0;
        for (int i = 0; i < g->win_n; i++) {
            int c = g->win[i];
            n += snprintf(cells + n, sizeof(cells) - n, i ? ",%d" : "%d", c4_str_index(c / C4_COLS, c % C4_COLS));
        }
        app_out("WIN %c cells=%s", game_mark_char(g->r.winner), cells);
    } else if (g->r.status == GAME_DRAW) {
        app_out("DRAW");
    } else {
        app_out("TURN %c", game_mark_char(g->r.turn));
    }
}

static game_event_t c4_move(void *st, int col, const char *src)
{
    c4_t *g = st;
    uint8_t mover = g->r.turn;
    int row = 0;
    switch (c4_drop(g, col, &row)) {
    case C4_ERR_RANGE:
        app_out("ERR range (columns 0-%d)", C4_COLS - 1);
        return GEV_BAD;
    case C4_ERR_FULL:
        app_out("ERR full column %d", col);
        return GEV_BAD;
    case C4_ERR_OVER:
        app_out("ERR game over");
        return GEV_BAD;
    case C4_OK:
        break;
    }
    app_out("DROP %c col=%d row=%d src=%s", game_mark_char(mover), col, row, src);
    c4_print(g);
    if (g->r.status == GAME_WIN) {
        return GEV_WIN;
    }
    if (g->r.status == GAME_DRAW) {
        return GEV_DRAW;
    }
    return mover == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
}

/* drop <col> [<col> ...]: stops at the first rejected drop or when the round ends */
static game_event_t c4_command(void *st, const char *arg)
{
    game_event_t ev = GEV_NONE;
    const char *p = arg;
    for (;;) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        char *end;
        long col = strtol(p, &end, 10);
        if (end == p) {
            app_out("ERR range (columns 0-%d)", C4_COLS - 1);
            return GEV_BAD;
        }
        p = end;
        ev = c4_move(st, col < 0 || col >= C4_COLS ? -1 : (int)col, "uart");
        if (ev == GEV_BAD || ev == GEV_WIN || ev == GEV_DRAW) {
            break;
        }
    }
    if (ev == GEV_NONE) {
        app_out("ERR range (columns 0-%d)", C4_COLS - 1);
        ev = GEV_BAD;
    }
    return ev;
}

/* anywhere in a column aims at it; the disc drops there on release */
static int c4_hit(int x, int y)
{
    (void)y;
    if (x < C4_X0 || x >= C4_X0 + C4_COLS * C4_PX) {
        return -1;
    }
    return (x - C4_X0) / C4_PX;
}

static void disc_center(int row, int col, float *cx, float *cy)
{
    *cx = C4_X0 + col * C4_PX + C4_PX / 2.0f;
    *cy = C4_Y0 + (C4_ROWS - 1 - row) * C4_PX + C4_PX / 2.0f;
}

static bool on_win(const c4_t *g, int cell)
{
    for (int i = 0; i < g->win_n; i++) {
        if (g->win[i] == cell) {
            return true;
        }
    }
    return false;
}

static void c4_draw(const void *st, int preview)
{
    const c4_t *g = st;
    if (preview >= 0 && preview < C4_COLS && g->r.status == GAME_PLAYING) {
        /* the disc about to drop, in the mover's colour, above its column */
        float cx, cy;
        disc_center(C4_ROWS, preview, &cx, &cy);
        cy -= 3; /* clear of the board frame */
        gfx_disc(cx, cy, DISC_R, g->r.turn == CELL_X ? COL_X : COL_O);
    }
    gfx_rrect(C4_X0 - 3, C4_Y0 - 3, C4_COLS * C4_PX + 6, C4_ROWS * C4_PX + 6, 8, COL_LINE);
    bool over = g->r.status != GAME_PLAYING;
    for (int row = 0; row < C4_ROWS; row++) {
        for (int col = 0; col < C4_COLS; col++) {
            int cell = row * C4_COLS + col;
            uint8_t m = g->cells[cell];
            float cx, cy;
            disc_center(row, col, &cx, &cy);
            if (m == CELL_EMPTY) {
                gfx_ring(cx, cy, DISC_R - 0.5f, 1, COL_LINE);
                continue;
            }
            bool win = g->r.status == GAME_WIN && on_win(g, cell);
            bool dim = over && !win;
            uint16_t c = m == CELL_X ? (dim ? COL_X_DIM : COL_X) : (dim ? COL_O_DIM : COL_O);
            gfx_disc(cx, cy, DISC_R, c);
            if (win) {
                gfx_ring(cx, cy, DISC_R + 2, 2, COL_TEXT);
            }
        }
    }
}

static void c4_icon(int x, int y, int s)
{
    /* a 4 x 4 corner of a board with a blue diagonal four */
    static const char *const ROWS[4] = {"...X", "..XO", ".XOO", "XOXO"};
    float t = s / 4.0f;
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            float cx = x + (c + 0.5f) * t, cy = y + (r + 0.5f) * t;
            char m = ROWS[r][c];
            if (m == '.') {
                gfx_ring(cx, cy, t * 0.32f, 1, COL_LINE);
            } else {
                gfx_disc(cx, cy, t * 0.34f, m == 'X' ? COL_X : COL_O);
            }
        }
    }
}

const game_def_t CONNECT4 = {
    .id = "c4",
    .title = "CONNECT 4",
    .verb = "drop",
    .new_round = c4_new_round,
    .print = c4_print,
    .move = c4_move,
    .command = c4_command,
    .hit = c4_hit,
    .drag = true,
    .draw = c4_draw,
    .icon = c4_icon,
};
#endif
