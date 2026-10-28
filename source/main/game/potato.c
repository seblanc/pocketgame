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

bool potato_tick(potato_t *g, uint32_t dt_ms)
{
    if (g->r.status != GAME_PLAYING) {
        return false;
    }
    g->total_ms += dt_ms;
    g->held_ms += dt_ms;
    g->tick_ms += dt_ms;
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

uint32_t potato_tick_interval(uint32_t fuse_left_ms)
{
    /* a long, slow tick-tock first, then faster and faster */
    return fuse_left_ms > 14000  ? 1400
           : fuse_left_ms > 9000 ? 1000
           : fuse_left_ms > 5000 ? 600
           : fuse_left_ms > 2500 ? 350
                                 : 170;
}

bool potato_ticks(potato_t *g)
{
    if (g->r.status != GAME_PLAYING || g->tick_ms < potato_tick_interval(g->fuse_ms)) {
        return false;
    }
    g->tick_ms = 0;
    return true;
}

int potato_hold_left_pct(const potato_t *g)
{
    if (g->held_ms >= POTATO_HOLD_MS) {
        return 0;
    }
    return (int)((POTATO_HOLD_MS - g->held_ms) * 100 / POTATO_HOLD_MS);
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(potato_t) <= GAME_STATE_MAX, "potato_t does not fit in a game slot");

#define CX (SCREEN_W / 2)
#define CY 124
#define TOP 38 /* the tinted play area, under the scores and the turn banner */

/* the holder's colour fills the whole play area, brighter than the banner's tint */
#define COL_X_TINT RGB(14, 50, 120)
#define COL_O_TINT RGB(120, 18, 18)
#define COL_FIRE RGB(255, 132, 0)
#define COL_FLAME RGB(255, 222, 40)

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
    app_out("POTATO holder=%c held=%lu", game_mark_char(g->r.turn), (unsigned long)g->held_ms);
    print_turn(g);
}

static void potato_report(const void *st)
{
    const potato_t *g = st;
    app_out("POTATO holder=%c held=%lu total=%lu run=%s ring=%d", game_mark_char(g->r.turn),
            (unsigned long)g->held_ms, (unsigned long)g->total_ms, g->r.status == GAME_PLAYING ? "burning" : "over",
            potato_hold_left_pct(g));
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
    /* shaking does nothing in this game: only the fuse burns */
    if (potato_tick(g, m->dt_ms)) {
        app_out("BOOM %c ms=%lu", game_mark_char(g->loser), (unsigned long)g->total_ms);
        print_turn(g);
        return GEV_BOOM;
    }
    /* the ticking is only a sound: a simulated round (whose steps stop at an event) stays silent */
    return potato_ticks(g) && !m->sim ? GEV_TICK : GEV_NONE;
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

/* the explosion: rays, a fireball and the word */
static void draw_boom(void)
{
    for (int i = 0; i < 16; i++) {
        float a = i * (float)M_PI / 8, r = i % 2 ? 66 : 88;
        gfx_line(CX + cosf(a) * 30, CY + sinf(a) * 30, CX + cosf(a) * r, CY + sinf(a) * r, i % 2 ? 5 : 7,
                 i % 2 ? COL_FLAME : COL_FIRE);
    }
    gfx_disc(CX, CY, 60, COL_FIRE);
    gfx_disc(CX, CY, 44, COL_FLAME);
    gfx_text_mid(CX, CY - 10, "BOUM", 3, RGB(0, 0, 0));
}

static void potato_draw(const void *st, int preview)
{
    (void)preview;
    const potato_t *g = st;
    char b[32];
    if (g->r.status == GAME_PLAYING) {
        uint8_t p = g->r.turn;
        uint16_t c = p == CELL_X ? COL_X : COL_O;
        gfx_rect(0, TOP, SCREEN_W, SCREEN_H - TOP, p == CELL_X ? COL_X_TINT : COL_O_TINT);
        /* the hold ring: full when the holder gets the potato, it empties (clockwise from
         * the top) over the minimum hold; a dim track shows where it was */
        gfx_ring(CX, CY, 70, 8, COL_LINE);
        int left = potato_hold_left_pct(g);
        if (left > 0) {
            gfx_arc(CX, CY, 70, 8, 270, 270 + 360.0f * left / 100, c);
        }
        draw_potato(CX, CY, 52);
        if (left == 0) {
            gfx_text_mid(CX, 205, "TOUCHE ET PASSE", 2, COL_TEXT);
            snprintf(b, sizeof(b), "AU %s", game_player_name(game_other(p)));
            gfx_text_mid(CX, 223, b, 2, COL_TEXT);
        }
    } else {
        uint8_t l = g->loser;
        gfx_rect(0, TOP, SCREEN_W, SCREEN_H - TOP, l == CELL_X ? COL_X_TINT : COL_O_TINT);
        draw_boom();
        /* a dark strip so the loser's name keeps its own colour readable on the tint */
        gfx_rect(0, 193, SCREEN_W, 28, COL_BG);
        snprintf(b, sizeof(b), "%s A PERDU", game_player_name(l));
        gfx_text_mid(CX, 200, b, 2, COL_TEXT);
        gfx_text_mid(CX, 226, "TOUCHE : NOUVELLE MANCHE", 1, COL_TEXT);
    }
}

static void potato_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    draw_potato(x + s / 2.0f, y + s / 2.0f, s * 0.38f);
}

static const char *const RULES[] = {
    "UNE MECHE CACHEE BRULE 15 A 30 S.",
    "SON TIC TAC, LENT AU DEBUT,",
    "ACCELERE AVANT LE BOUM.",
    "",
    "GARDE LA CONSOLE AU MOINS 2 S,",
    "PUIS TOUCHE L'ECRAN POUR PASSER.",
    "",
    "CELUI QUI TIENT LA CONSOLE AU",
    "BOUM PERD LA MANCHE.",
    NULL,
};
_Static_assert(POTATO_FUSE_MIN_MS == 15000 && POTATO_FUSE_MAX_MS == 30000 && POTATO_HOLD_MS == 2000,
               "update the rules text");

const game_def_t HOT_POTATO = {
    .id = "potato",
    .title = "PATATE CHAUDE",
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
    .rules_uart = "fuse=15-30s hold=2s boom=loses tick=faster",
};
#endif
