#include "tilt.h"
#include <math.h>
#include <string.h>

/* The maze: V_WALLS[r][c] is the wall right of cell (c, r), H_WALLS[r][c] the wall
 * below it; the outer frame is always closed. A tree maze (one way between any two
 * cells) with long dead ends. The way out, as cells (col,row), has a straight
 * corridor ending at a wall for every leg, so a held tilt stops the ball at each turn
 * (the UART acceptance test rolls it along this way):
 *   right to 3,0 - down to 3,2 - [right into the dead end 5,2 - back left] - left to
 *   1,2 - down to 1,5 - right to 5,5 - down to 5,7 - right to the target 7,7.
 * The dead ends: 2,1 off the start, the long arm round the right side from 3,1
 * (ends 5,1, 6,2 and 6,5), the middle block from 2,2 (ends 6,3, 2,4 and 6,4) and the
 * bottom-left arm from 0,1 (ends 4,6 and 4,7, a wall short of the way out). */
static const char *const V_WALLS[TILT_ROWS] = {
    "...|...", "..|.|..", "|....|.", "||....|", "||..|.|", "|....||", "....||.", "....|..",
};
static const char *const H_WALLS[TILT_ROWS - 1] = {
    ".--..--.", ".--.---.", "...----.", "..--..-.", "..-----.", ".----...", ".----.--",
};

#define TARGET_COL (TILT_COLS - 1)
#define TARGET_ROW (TILT_ROWS - 1)
#define WALL_HALF 1.0f /* walls are 2 px thick, centred on the grid lines */
#define BALL_R 7.0f
#define G_PX 900.0f    /* px/s^2 of a 1 g tilt */
#define DRAG 1.5f      /* rolling friction, 1/s */
#define VMAX 400.0f    /* px/s */
#define TILT_MAX 1500  /* mg */
#define SUB_MS 2       /* physics step: under 1 px per step at VMAX */

/* the wall on vertical grid line k (x = k * TILT_CELL) beside row r */
static bool v_wall(int k, int r) { return k <= 0 || k >= TILT_COLS || V_WALLS[r][k - 1] == '|'; }
/* the wall on horizontal grid line k (y = k * TILT_CELL) above/below column c */
static bool h_wall(int k, int c) { return k <= 0 || k >= TILT_ROWS || H_WALLS[k - 1][c] == '-'; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* does the ball's square at x, y overlap a wall? */
static bool blocked(float x, float y)
{
    float x0 = x - BALL_R, x1 = x + BALL_R, y0 = y - BALL_R, y1 = y + BALL_R;
    int ka = clampi((int)floorf((x0 - WALL_HALF) / TILT_CELL), 0, TILT_COLS);
    int kb = clampi((int)floorf((x1 + WALL_HALF) / TILT_CELL) + 1, 0, TILT_COLS);
    int ra = clampi((int)floorf((y0 - WALL_HALF) / TILT_CELL), 0, TILT_ROWS - 1);
    int rb = clampi((int)floorf((y1 + WALL_HALF) / TILT_CELL), 0, TILT_ROWS - 1);
    for (int k = ka; k <= kb; k++) {
        float lx = k * TILT_CELL;
        if (lx + WALL_HALF <= x0 || lx - WALL_HALF >= x1) {
            continue;
        }
        for (int r = ra; r <= rb; r++) {
            if (r * TILT_CELL - WALL_HALF < y1 && (r + 1) * TILT_CELL + WALL_HALF > y0 && v_wall(k, r)) {
                return true;
            }
        }
    }
    int ca = clampi((int)floorf((x0 - WALL_HALF) / TILT_CELL), 0, TILT_COLS - 1);
    int cb = clampi((int)floorf((x1 + WALL_HALF) / TILT_CELL), 0, TILT_COLS - 1);
    ka = clampi((int)floorf((y0 - WALL_HALF) / TILT_CELL), 0, TILT_ROWS);
    kb = clampi((int)floorf((y1 + WALL_HALF) / TILT_CELL) + 1, 0, TILT_ROWS);
    for (int k = ka; k <= kb; k++) {
        float ly = k * TILT_CELL;
        if (ly + WALL_HALF <= y0 || ly - WALL_HALF >= y1) {
            continue;
        }
        for (int c = ca; c <= cb; c++) {
            if (c * TILT_CELL - WALL_HALF < x1 && (c + 1) * TILT_CELL + WALL_HALF > x0 && h_wall(k, c)) {
                return true;
            }
        }
    }
    return false;
}

static void place_ball(tilt_t *g)
{
    g->x = g->y = TILT_CELL / 2.0f;
    g->vx = g->vy = 0;
    g->ms = 0;
    g->run = TILT_WAIT;
}

void tilt_start(tilt_t *g, uint8_t first)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->time[CELL_X] = g->time[CELL_O] = TILT_NOT_RUN;
    place_ball(g);
}

bool tilt_go(tilt_t *g)
{
    if (g->r.status != GAME_PLAYING || g->run != TILT_WAIT) {
        return false;
    }
    g->run = TILT_GO;
    g->r.moves++;
    return true;
}

void tilt_cell(const tilt_t *g, int *col, int *row)
{
    *col = clampi((int)(g->x / TILT_CELL), 0, TILT_COLS - 1);
    *row = clampi((int)(g->y / TILT_CELL), 0, TILT_ROWS - 1);
}

/* the run of r.turn ended with time t: the other player's turn, or the round's result */
static void end_run(tilt_t *g, int32_t t)
{
    uint8_t p = g->r.turn, o = game_other(p);
    g->time[p] = t;
    place_ball(g);
    if (g->time[o] == TILT_NOT_RUN) {
        g->r.turn = o;
        return;
    }
    int32_t a = g->time[CELL_X], b = g->time[CELL_O];
    if (a == b) {
        g->r.status = GAME_DRAW; /* also two timeouts */
        return;
    }
    g->r.status = GAME_WIN;
    g->r.winner = a < 0 ? CELL_O : b < 0 ? CELL_X : a < b ? CELL_X : CELL_O;
}

static void roll(tilt_t *g, float ax, float ay, float dt)
{
    g->vx += ax * dt;
    g->vy += ay * dt;
    float k = 1 - DRAG * dt;
    g->vx = fmaxf(-VMAX, fminf(VMAX, g->vx * k));
    g->vy = fmaxf(-VMAX, fminf(VMAX, g->vy * k));
    /* one axis at a time, so the ball slides along a wall it hits */
    float nx = g->x + g->vx * dt;
    if (blocked(nx, g->y)) {
        g->vx = 0;
    } else {
        g->x = nx;
    }
    float ny = g->y + g->vy * dt;
    if (blocked(g->x, ny)) {
        g->vy = 0;
    } else {
        g->y = ny;
    }
}

tilt_ev_t tilt_step(tilt_t *g, int tx, int ty, uint32_t dt_ms)
{
    if (g->r.status != GAME_PLAYING || g->run != TILT_GO) {
        return TILT_EV_NONE;
    }
    float ax = clampi(tx, -TILT_MAX, TILT_MAX) * (G_PX / 1000.0f);
    float ay = clampi(ty, -TILT_MAX, TILT_MAX) * (G_PX / 1000.0f);
    while (dt_ms > 0) {
        uint32_t s = dt_ms < SUB_MS ? dt_ms : SUB_MS;
        if (ax == 0 && ay == 0 && g->vx == 0 && g->vy == 0) {
            s = dt_ms; /* at rest on a level board: only the clock runs */
        }
        if (g->ms + s > TILT_LIMIT_MS) {
            s = TILT_LIMIT_MS - g->ms;
        }
        dt_ms -= s;
        g->ms += s;
        if (s && (ax != 0 || ay != 0 || g->vx != 0 || g->vy != 0)) {
            roll(g, ax, ay, s / 1000.0f);
        }
        int c, r;
        tilt_cell(g, &c, &r);
        if (c == TARGET_COL && r == TARGET_ROW) {
            end_run(g, (int32_t)g->ms);
            return TILT_EV_FINISH;
        }
        if (g->ms >= TILT_LIMIT_MS) {
            end_run(g, TILT_TIMEOUT);
            return TILT_EV_TIMEOUT;
        }
    }
    return TILT_EV_NONE;
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <stdio.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(tilt_t) <= GAME_STATE_MAX, "tilt_t does not fit in a game slot");

#define MAZE_X0 ((SCREEN_W - TILT_COLS * TILT_CELL) / 2)
#define MAZE_Y0 40
#define INFO_Y 228

static void time_str(char *b, size_t n, int32_t t)
{
    if (t == TILT_NOT_RUN) {
        snprintf(b, n, "-");
    } else if (t == TILT_TIMEOUT) {
        snprintf(b, n, "timeout");
    } else {
        snprintf(b, n, "%ld", (long)t);
    }
}

/* TURN O | WIN X x=.. o=.. | DRAW x=.. o=.. */
static void print_result(const tilt_t *g)
{
    char x[12], o[12];
    time_str(x, sizeof(x), g->time[CELL_X]);
    time_str(o, sizeof(o), g->time[CELL_O]);
    if (g->r.status == GAME_WIN) {
        app_out("WIN %c x=%s o=%s", game_mark_char(g->r.winner), x, o);
    } else if (g->r.status == GAME_DRAW) {
        app_out("DRAW x=%s o=%s", x, o);
    } else {
        app_out("TURN %c", game_mark_char(g->r.turn));
    }
}

static void tilt_new_round(void *st, uint8_t first) { tilt_start(st, first); }

static void tilt_print(const void *st)
{
    app_out("MAZE %dx%d start=0,0 target=%d,%d limit=%ds", TILT_COLS, TILT_ROWS, TARGET_COL, TARGET_ROW,
            TILT_LIMIT_MS / 1000);
    print_result(st);
}

static void tilt_report(const void *st)
{
    const tilt_t *g = st;
    int c, r;
    tilt_cell(g, &c, &r);
    app_out("BALL cell=%d,%d x=%d y=%d ms=%lu run=%s", c, r, (int)g->x, (int)g->y, (unsigned long)g->ms,
            g->r.status != GAME_PLAYING ? "over" : g->run == TILT_GO ? "go" : "wait");
}

static game_event_t start_run(tilt_t *g, const char *src)
{
    if (!tilt_go(g)) {
        return GEV_NONE;
    }
    app_out("GO %c src=%s", game_mark_char(g->r.turn), src);
    return g->r.turn == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
}

/* the only move: a tap starts the waiting run */
static game_event_t tilt_move(void *st, int m, const char *src)
{
    tilt_t *g = st;
    if (g->r.status != GAME_PLAYING) {
        app_out("ERR game over");
        return GEV_BAD;
    }
    (void)m;
    return start_run(g, src);
}

/* maze: the state; maze go: start the run */
static game_event_t tilt_command(void *st, const char *arg)
{
    if (strcmp(arg, "go") == 0) {
        return tilt_move(st, 0, "uart");
    }
    if (*arg) {
        app_out("ERR maze [go]");
        return GEV_BAD;
    }
    tilt_report(st);
    print_result(st);
    return GEV_NONE;
}

static game_event_t tilt_step_ev(void *st, const motion_t *m)
{
    tilt_t *g = st;
    if (m->sim && g->r.status == GAME_PLAYING && g->run == TILT_WAIT) {
        start_run(g, "sim");
    }
    uint8_t p = g->r.turn;
    switch (tilt_step(g, m->tx, m->ty, m->dt_ms)) {
    case TILT_EV_NONE:
        return GEV_NONE;
    case TILT_EV_FINISH:
        app_out("FINISH %c ms=%ld", game_mark_char(p), (long)g->time[p]);
        break;
    case TILT_EV_TIMEOUT:
        app_out("TIMEOUT %c", game_mark_char(p));
        break;
    }
    print_result(g);
    if (g->r.status == GAME_WIN) {
        return GEV_WIN;
    }
    if (g->r.status == GAME_DRAW) {
        return GEV_DRAW;
    }
    return p == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
}

static int tilt_hit(int x, int y)
{
    (void)x;
    (void)y;
    return 0; /* a tap anywhere starts the run */
}

static void secs(char *b, size_t n, int32_t ms)
{
    if (ms == TILT_NOT_RUN) {
        snprintf(b, n, "--");
    } else if (ms == TILT_TIMEOUT) {
        snprintf(b, n, "TIME");
    } else {
        snprintf(b, n, "%ld.%ld", (long)(ms / 1000), (long)(ms / 100 % 10));
    }
}

static void draw_walls(int x0, int y0, float cell, float w, uint16_t col)
{
    for (int r = 0; r < TILT_ROWS; r++) {
        for (int k = 0; k <= TILT_COLS; k++) {
            if (v_wall(k, r)) {
                gfx_rect(x0 + (int)(k * cell - w / 2), y0 + (int)(r * cell - w / 2), (int)w, (int)(cell + w), col);
            }
        }
    }
    for (int k = 0; k <= TILT_ROWS; k++) {
        for (int c = 0; c < TILT_COLS; c++) {
            if (h_wall(k, c)) {
                gfx_rect(x0 + (int)(c * cell - w / 2), y0 + (int)(k * cell - w / 2), (int)(cell + w), (int)w, col);
            }
        }
    }
}

static void tilt_draw(const void *st, int preview)
{
    (void)preview;
    const tilt_t *g = st;
    draw_walls(MAZE_X0, MAZE_Y0, TILT_CELL, 2, COL_DIM);
    float tx = MAZE_X0 + (TARGET_COL + 0.5f) * TILT_CELL, ty = MAZE_Y0 + (TARGET_ROW + 0.5f) * TILT_CELL;
    gfx_ring(tx, ty, 8, 2, COL_ACCENT);
    gfx_disc(tx, ty, 3, COL_ACCENT);
    if (g->r.status == GAME_PLAYING) {
        gfx_disc(MAZE_X0 + g->x, MAZE_Y0 + g->y, BALL_R, g->r.turn == CELL_X ? COL_X : COL_O);
    }

    char b[16];
    secs(b, sizeof(b), g->time[CELL_X]);
    char line[24];
    snprintf(line, sizeof(line), "X %s", b);
    gfx_text(MAZE_X0, INFO_Y, line, 1, COL_X);
    secs(b, sizeof(b), g->time[CELL_O]);
    snprintf(line, sizeof(line), "O %s", b);
    gfx_text_right(MAZE_X0 + TILT_COLS * TILT_CELL, INFO_Y, line, 1, COL_O);
    if (g->r.status == GAME_PLAYING) {
        if (g->run == TILT_WAIT) {
            snprintf(line, sizeof(line), "TAP TO START");
        } else {
            secs(line, sizeof(line), (int32_t)g->ms);
        }
        gfx_text_mid(SCREEN_W / 2, INFO_Y, line, 1, COL_TEXT);
    }
}

static void tilt_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    float cell = s / (float)TILT_COLS;
    draw_walls(x, y, cell, 1, COL_DIM);
    gfx_disc(x + cell / 2, y + cell / 2, cell * 0.4f, COL_X);
    gfx_disc(x + s - cell / 2, y + s - cell / 2, cell * 0.4f, COL_ACCENT);
}

const game_def_t TILT_DUEL = {
    .id = "tilt",
    .title = "TILT DUEL",
    .verb = "maze",
    .new_round = tilt_new_round,
    .print = tilt_print,
    .move = tilt_move,
    .command = tilt_command,
    .hit = tilt_hit,
    .draw = tilt_draw,
    .icon = tilt_icon,
    .step = tilt_step_ev,
    .report = tilt_report,
};
#endif
