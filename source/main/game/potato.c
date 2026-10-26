#include "potato.h"
#include <string.h>

void potato_start(potato_t *g, uint8_t first, uint32_t fuse_ms)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->fuse_ms = fuse_ms;
}

/* the potato goes to the other player */
static void hand_over(potato_t *g)
{
    g->r.turn = game_other(g->r.turn);
    g->held_ms = 0;
    g->r.moves++;
}

potato_result_t potato_pass(potato_t *g)
{
    if (g->r.status != GAME_PLAYING) {
        return POTATO_ERR_OVER;
    }
    if (g->held_ms < POTATO_HOLD_MS) {
        return POTATO_ERR_SOON;
    }
    hand_over(g);
    return POTATO_OK;
}

potato_result_t potato_skip(potato_t *g)
{
    if (g->r.status != GAME_PLAYING) {
        return POTATO_ERR_OVER;
    }
    if (g->skip_used[g->r.turn]) {
        return POTATO_ERR_USED;
    }
    g->skip_used[g->r.turn] = true;
    hand_over(g);
    return POTATO_OK;
}

bool potato_tick(potato_t *g, uint32_t dt_ms)
{
    if (g->r.status != GAME_PLAYING) {
        return false;
    }
    g->total_ms += dt_ms;
    g->held_ms += dt_ms;
    if (dt_ms < g->fuse_ms) {
        g->fuse_ms -= dt_ms;
        return false;
    }
    g->fuse_ms = 0;
    g->loser = g->r.turn;
    g->r.status = GAME_WIN;
    g->r.winner = game_other(g->r.turn);
    return true;
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(potato_t) <= GAME_STATE_MAX, "potato_t does not fit in a game slot");

#define CX (SCREEN_W / 2)
#define CY 124

static void potato_new_round(void *st, uint8_t first)
{
    potato_start(st, first, POTATO_FUSE_MIN_MS + game_random() % (POTATO_FUSE_MAX_MS - POTATO_FUSE_MIN_MS + 1));
}

static void print_turn(const potato_t *g)
{
    if (g->r.status == GAME_WIN) {
        app_out("WIN %c", game_mark_char(g->r.winner));
    } else {
        app_out("TURN %c", game_mark_char(g->r.turn));
    }
}

static void potato_print(const void *st)
{
    const potato_t *g = st;
    app_out("POTATO holder=%c held=%lu skips=%c%c", game_mark_char(g->r.turn), (unsigned long)g->held_ms,
            g->skip_used[CELL_X] ? '-' : 'X', g->skip_used[CELL_O] ? '-' : 'O');
    print_turn(g);
}

static void potato_report(const void *st)
{
    const potato_t *g = st;
    app_out("POTATO holder=%c held=%lu total=%lu run=%s", game_mark_char(g->r.turn), (unsigned long)g->held_ms,
            (unsigned long)g->total_ms, g->r.status == GAME_PLAYING ? "burning" : "over");
}

static game_event_t result_ev(const potato_t *g, potato_result_t res, uint8_t p, const char *what)
{
    switch (res) {
    case POTATO_OK:
        app_out("%s %c", what, game_mark_char(p));
        print_turn(g);
        return p == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
    case POTATO_ERR_SOON:
        app_out("ERR too soon (hold %d s)", POTATO_HOLD_MS / 1000);
        break;
    case POTATO_ERR_USED:
        app_out("ERR skip used");
        break;
    case POTATO_ERR_OVER:
        app_out("ERR game over");
        break;
    }
    return GEV_BAD;
}

/* the only move: a tap passes the potato */
static game_event_t potato_move(void *st, int m, const char *src)
{
    potato_t *g = st;
    (void)m;
    (void)src;
    uint8_t p = g->r.turn;
    return result_ev(g, potato_pass(g), p, "PASS");
}

static game_event_t potato_command(void *st, const char *arg)
{
    if (*arg) {
        app_out("ERR pass (no arguments)");
        return GEV_BAD;
    }
    return potato_move(st, 0, "uart");
}

static game_event_t potato_step(void *st, const motion_t *m)
{
    potato_t *g = st;
    if (g->r.status != GAME_PLAYING) {
        if (m->shake && m->sim) {
            app_out("ERR game over");
            return GEV_BAD;
        }
        return GEV_NONE;
    }
    game_event_t ev = GEV_NONE;
    if (m->shake) {
        uint8_t p = g->r.turn;
        potato_result_t res = potato_skip(g);
        /* a used skip is only reported when asked for (a real shake may repeat) */
        if (res == POTATO_OK || m->sim) {
            ev = result_ev(g, res, p, "SKIP");
        }
    }
    if (potato_tick(g, m->dt_ms)) {
        app_out("BOOM %c ms=%lu", game_mark_char(g->loser), (unsigned long)g->total_ms);
        print_turn(g);
        return GEV_BOOM;
    }
    return ev;
}

/* "sim fuse <ms>": the fuse from now on, for a repeatable test */
static bool potato_sim_set(void *st, const char *key, long val)
{
    potato_t *g = st;
    if (strcmp(key, "fuse") != 0 || val <= 0) {
        return false;
    }
    g->fuse_ms = (uint32_t)val;
    app_out("SIM fuse=%ld", val);
    return true;
}

static int potato_hit(int x, int y)
{
    (void)x;
    (void)y;
    return 0; /* a tap anywhere passes */
}

/* a potato: a lumpy brown disc with a few eyes */
static void draw_potato(float cx, float cy, float r)
{
    uint16_t body = RGB(176, 128, 72), eye = RGB(96, 64, 32);
    gfx_disc(cx - r * 0.25f, cy, r * 0.8f, body);
    gfx_disc(cx + r * 0.3f, cy - r * 0.05f, r * 0.72f, body);
    gfx_disc(cx - r * 0.35f, cy - r * 0.25f, r * 0.12f, eye);
    gfx_disc(cx + r * 0.4f, cy + r * 0.2f, r * 0.1f, eye);
    gfx_disc(cx + r * 0.05f, cy + r * 0.35f, r * 0.08f, eye);
}

static void potato_draw(const void *st, int preview)
{
    (void)preview;
    const potato_t *g = st;
    if (g->r.status == GAME_PLAYING) {
        uint16_t c = g->r.turn == CELL_X ? COL_X : COL_O;
        gfx_ring(CX, CY, 70, 3, c);
        draw_potato(CX, CY, 52);
        bool can_pass = g->held_ms >= POTATO_HOLD_MS;
        gfx_text_mid(CX, 204, can_pass ? "TAP TO PASS" : "HOLD...", 2, can_pass ? COL_TEXT : COL_DIM);
        gfx_text_mid(CX, 226, g->skip_used[g->r.turn] ? "SKIP USED" : "SHAKE = SKIP (ONCE)", 1, COL_DIM);
    } else {
        gfx_ring(CX, CY, 70, 3, COL_ACCENT);
        gfx_text_mid(CX, CY - 7, "BOOM!", 2, COL_TEXT);
        char b[24];
        snprintf(b, sizeof(b), "%c HELD IT", game_mark_char(g->loser));
        gfx_text_mid(CX, 204, b, 2, g->loser == CELL_X ? COL_X : COL_O);
        gfx_text_mid(CX, 226, "TAP FOR A NEW ROUND", 1, COL_DIM);
    }
}

static void potato_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    draw_potato(x + s / 2.0f, y + s / 2.0f, s * 0.38f);
}

static const char *const RULES[] = {
    "A HIDDEN FUSE BURNS FOR 8 TO 20 S.",
    "NOBODY KNOWS WHEN IT GOES BOOM.",
    "",
    "HOLD THE BOARD AT LEAST 2 S,",
    "THEN TAP PASS TO HAND IT ON.",
    "",
    "IN A HURRY? SHAKE IT TO SKIP:",
    "ONE SKIP PER PLAYER PER ROUND.",
    "",
    "WHOEVER HOLDS THE BOARD AT",
    "BOOM LOSES THE ROUND.",
    NULL,
};
_Static_assert(POTATO_FUSE_MIN_MS == 8000 && POTATO_FUSE_MAX_MS == 20000 && POTATO_HOLD_MS == 2000,
               "update the rules text");

const game_def_t HOT_POTATO = {
    .id = "potato",
    .title = "HOT POTATO",
    .verb = "pass",
    .new_round = potato_new_round,
    .print = potato_print,
    .move = potato_move,
    .command = potato_command,
    .hit = potato_hit,
    .draw = potato_draw,
    .icon = potato_icon,
    .step = potato_step,
    .report = potato_report,
    .sim_set = potato_sim_set,
    .rules = RULES,
    .rules_uart = "fuse=8-20s hold=2s skip=1 boom=loses",
};
#endif
