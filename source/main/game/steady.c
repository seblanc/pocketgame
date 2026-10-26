#include "steady.h"
#include <math.h>
#include <string.h>

#define SUB_MS 5
#define TILT_MAX 1500 /* mg */

static void place_bubble(steady_t *g)
{
    g->x = g->y = 0;
    g->ms = 0;
    g->run = STEADY_WAIT;
}

void steady_start(steady_t *g, uint8_t first)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->time[CELL_X] = g->time[CELL_O] = STEADY_NOT_RUN;
    place_bubble(g);
}

bool steady_go(steady_t *g)
{
    if (g->r.status != GAME_PLAYING || g->run != STEADY_WAIT) {
        return false;
    }
    g->run = STEADY_GO;
    g->r.moves++;
    return true;
}

float steady_radius(uint32_t ms)
{
    return STEADY_RMIN + (STEADY_R0 - STEADY_RMIN) * expf(-(float)ms / STEADY_SHRINK_MS);
}

static void end_run(steady_t *g)
{
    uint8_t p = g->r.turn, o = game_other(p);
    g->time[p] = (int32_t)g->ms;
    place_bubble(g);
    if (g->time[o] == STEADY_NOT_RUN) {
        g->r.turn = o;
        return;
    }
    if (g->time[CELL_X] == g->time[CELL_O]) {
        g->r.status = GAME_DRAW;
        return;
    }
    g->r.status = GAME_WIN;
    g->r.winner = g->time[CELL_X] > g->time[CELL_O] ? CELL_X : CELL_O;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool steady_step(steady_t *g, int tx, int ty, uint32_t dt_ms)
{
    if (g->r.status != GAME_PLAYING || g->run != STEADY_GO) {
        return false;
    }
    float px = clampi(tx, -TILT_MAX, TILT_MAX) * (STEADY_PX_PER_G / 1000.0f);
    float py = clampi(ty, -TILT_MAX, TILT_MAX) * (STEADY_PX_PER_G / 1000.0f);
    while (dt_ms > 0) {
        uint32_t s = dt_ms < SUB_MS ? dt_ms : SUB_MS;
        if (g->ms + s > STEADY_LIMIT_MS) {
            s = STEADY_LIMIT_MS - g->ms;
        }
        dt_ms -= s;
        g->ms += s;
        float k = 1 - expf(-(float)s / STEADY_TAU_MS);
        g->x += (px - g->x) * k;
        g->y += (py - g->y) * k;
        if (hypotf(g->x, g->y) > steady_radius(g->ms) || g->ms >= STEADY_LIMIT_MS) {
            end_run(g);
            return true;
        }
    }
    return false;
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <stdio.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(steady_t) <= GAME_STATE_MAX, "steady_t does not fit in a game slot");

#define CX (SCREEN_W / 2)
#define CY 128
#define BUBBLE_R 6
#define INFO_Y 226

static void time_str(char *b, size_t n, int32_t t)
{
    if (t == STEADY_NOT_RUN) {
        snprintf(b, n, "-");
    } else {
        snprintf(b, n, "%ld", (long)t);
    }
}

static void print_result(const steady_t *g)
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

static void steady_new_round(void *st, uint8_t first) { steady_start(st, first); }

static void steady_print(const void *st)
{
    app_out("STEADY r0=%d limit=%ds", (int)STEADY_R0, STEADY_LIMIT_MS / 1000);
    print_result(st);
}

static void steady_report(const void *st)
{
    const steady_t *g = st;
    app_out("BUBBLE d=%d r=%d ms=%lu run=%s", (int)hypotf(g->x, g->y), (int)steady_radius(g->ms),
            (unsigned long)g->ms, g->r.status != GAME_PLAYING ? "over" : g->run == STEADY_GO ? "go" : "wait");
}

static game_event_t start_run(steady_t *g, const char *src)
{
    if (!steady_go(g)) {
        return GEV_NONE;
    }
    app_out("GO %c src=%s", game_mark_char(g->r.turn), src);
    return g->r.turn == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
}

static game_event_t steady_move(void *st, int m, const char *src)
{
    steady_t *g = st;
    (void)m;
    if (g->r.status != GAME_PLAYING) {
        app_out("ERR game over");
        return GEV_BAD;
    }
    return start_run(g, src);
}

static game_event_t steady_command(void *st, const char *arg)
{
    if (strcmp(arg, "go") == 0) {
        return steady_move(st, 0, "uart");
    }
    if (*arg) {
        app_out("ERR hold [go]");
        return GEV_BAD;
    }
    steady_report(st);
    print_result(st);
    return GEV_NONE;
}

static game_event_t steady_step_ev(void *st, const motion_t *m)
{
    steady_t *g = st;
    if (m->sim && g->r.status == GAME_PLAYING && g->run == STEADY_WAIT) {
        start_run(g, "sim");
    }
    uint8_t p = g->r.turn;
    if (!steady_step(g, m->tx, m->ty, m->dt_ms)) {
        return GEV_NONE;
    }
    app_out("OUT %c ms=%ld", game_mark_char(p), (long)g->time[p]);
    print_result(g);
    if (g->r.status == GAME_WIN) {
        return GEV_WIN;
    }
    if (g->r.status == GAME_DRAW) {
        return GEV_DRAW;
    }
    return GEV_BAD; /* the bubble left: the low buzz */
}

static int steady_hit(int x, int y)
{
    (void)x;
    (void)y;
    return 0; /* a tap anywhere starts the run */
}

static void secs(char *b, size_t n, int32_t ms)
{
    if (ms == STEADY_NOT_RUN) {
        snprintf(b, n, "--");
    } else {
        snprintf(b, n, "%ld.%ld", (long)(ms / 1000), (long)(ms / 100 % 10));
    }
}

static void steady_draw(const void *st, int preview)
{
    (void)preview;
    const steady_t *g = st;
    uint16_t pc = g->r.turn == CELL_X ? COL_X : COL_O;
    gfx_ring(CX, CY, STEADY_R0 + 4, 1, COL_LINE);
    if (g->r.status == GAME_PLAYING) {
        gfx_ring(CX, CY, steady_radius(g->ms), 2, COL_ACCENT);
        gfx_line(CX - 4, CY, CX + 4, CY, 1, COL_DIM);
        gfx_line(CX, CY - 4, CX, CY + 4, 1, COL_DIM);
        gfx_disc(CX + g->x, CY + g->y, BUBBLE_R, pc);
    }
    char b[16], line[24];
    secs(b, sizeof(b), g->time[CELL_X]);
    snprintf(line, sizeof(line), "X %s", b);
    gfx_text(16, INFO_Y, line, 1, COL_X);
    secs(b, sizeof(b), g->time[CELL_O]);
    snprintf(line, sizeof(line), "O %s", b);
    gfx_text_right(224, INFO_Y, line, 1, COL_O);
    if (g->r.status == GAME_PLAYING) {
        if (g->run == STEADY_WAIT) {
            snprintf(line, sizeof(line), "TAP TO START");
        } else {
            secs(line, sizeof(line), (int32_t)g->ms);
        }
        gfx_text_mid(SCREEN_W / 2, INFO_Y, line, 1, COL_TEXT);
    }
}

static void steady_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    gfx_ring(x + s / 2.0f, y + s / 2.0f, s * 0.36f, 1.5f, COL_ACCENT);
    gfx_ring(x + s / 2.0f, y + s / 2.0f, s * 0.2f, 1, COL_DIM);
    gfx_disc(x + s * 0.58f, y + s * 0.44f, s * 0.09f, COL_X);
}

const game_def_t STEADY_HAND = {
    .id = "steady",
    .title = "STEADY HAND",
    .verb = "hold",
    .new_round = steady_new_round,
    .print = steady_print,
    .move = steady_move,
    .command = steady_command,
    .hit = steady_hit,
    .draw = steady_draw,
    .icon = steady_icon,
    .step = steady_step_ev,
    .report = steady_report,
};
#endif
