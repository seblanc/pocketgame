#include "plat.h"
#include <string.h>

#define GRAV 1400.0f   /* px/s^2 */
#define JUMP_V 440.0f  /* px/s: about 69 px high, 0.63 s in the air */
#define WALK 100.0f    /* px/s */
#define FALL_MAX 600.0f
#define SUB_MS 5       /* physics step: under 3 px per step */
#define FEET 4         /* px either side of x that still stand on a platform */

/* five platforms, each reachable from the one below; seven coins: one on the ground
 * (to the right of the start), one on each platform and one in the air above the
 * ground's right end */
const plat_platform_t PLAT_PLATFORMS[] = {
    {30, 80, 160},   /* low left */
    {100, 150, 112}, /* middle */
    {170, 225, 150}, /* low right */
    {50, 100, 72},   /* high left */
    {175, 215, 92},  /* high right */
};
const int PLAT_NPLATS = sizeof(PLAT_PLATFORMS) / sizeof(PLAT_PLATFORMS[0]);

const plat_coin_t PLAT_COINS[] = {
    {120, PLAT_GROUND - 8}, {55, 150}, {125, 102}, {197, 140}, {75, 62}, {195, 82}, {230, 176},
};
const int PLAT_NCOINS = sizeof(PLAT_COINS) / sizeof(PLAT_COINS[0]);

#define ALL_COINS ((uint16_t)((1u << (sizeof(PLAT_COINS) / sizeof(PLAT_COINS[0]))) - 1))

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static int popcount16(uint16_t v)
{
    int n = 0;
    for (; v; v &= v - 1) {
        n++;
    }
    return n;
}

int plat_coins_left(const plat_t *g) { return popcount16(ALL_COINS & ~g->taken); }

int plat_secs_left(const plat_t *g) { return g->ms >= PLAT_LIMIT_MS ? 0 : (int)((PLAT_LIMIT_MS - g->ms) / 1000); }

uint32_t plat_level_id(const plat_t *g)
{
    /* FNV-1a over the platforms, the coins and which coins are still out */
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)PLAT_PLATFORMS;
    for (size_t i = 0; i < sizeof(PLAT_PLATFORMS); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    p = (const uint8_t *)PLAT_COINS;
    for (size_t i = 0; i < sizeof(PLAT_COINS); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    uint16_t out = ALL_COINS & ~g->taken;
    h = (h ^ (out & 0xff)) * 16777619u;
    h = (h ^ (out >> 8)) * 16777619u;
    return h;
}

static void place_player(plat_t *g)
{
    g->x = PLAT_START_X;
    g->y = PLAT_GROUND;
    g->vy = 0;
    g->on = PLAT_ON_GROUND;
    g->face = 1;
    g->ms = 0;
    g->taken = 0; /* every coin is back for the next run */
    g->last_coin = -1;
    g->run = PLAT_WAIT;
    g->ai = 0;
    g->ai_jumped = false;
}

void plat_start(plat_t *g, uint8_t first)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->coins[CELL_X] = g->coins[CELL_O] = PLAT_NOT_RUN;
    g->points[CELL_X] = g->points[CELL_O] = PLAT_NOT_RUN;
    place_player(g);
    g->id = plat_level_id(g);
}

bool plat_go(plat_t *g)
{
    if (g->r.status != GAME_PLAYING || g->run != PLAT_WAIT) {
        return false;
    }
    g->run = PLAT_GO;
    g->r.moves++;
    return true;
}

bool plat_jump(plat_t *g)
{
    if (g->r.status != GAME_PLAYING || g->run != PLAT_GO || g->on == PLAT_ON_AIR) {
        return false;
    }
    g->vy = -JUMP_V;
    g->on = PLAT_ON_AIR;
    return true;
}

/* the run of r.turn is over: the other player's run, or the round's result */
static void end_run(plat_t *g)
{
    uint8_t p = g->r.turn, o = game_other(p);
    g->coins[p] = (int8_t)popcount16(g->taken);
    g->left_s = (int8_t)plat_secs_left(g);
    g->points[p] = (int16_t)(g->coins[p] * PLAT_COIN_PTS + g->left_s);
    place_player(g);
    if (g->points[o] == PLAT_NOT_RUN) {
        g->r.turn = o;
        return;
    }
    int a = g->points[CELL_X], b = g->points[CELL_O];
    if (a == b) {
        g->r.status = GAME_DRAW;
        return;
    }
    g->r.status = GAME_WIN;
    g->r.winner = a > b ? CELL_X : CELL_O;
}

static bool over(const plat_platform_t *p, float x) { return x + FEET >= p->x0 && x - FEET <= p->x1; }

/* collects the first coin the player's box touches */
static bool grab_coin(plat_t *g)
{
    for (int i = 0; i < PLAT_NCOINS; i++) {
        const plat_coin_t *c = &PLAT_COINS[i];
        if (g->taken >> i & 1) {
            continue;
        }
        if (g->x - PLAT_HALF_W < c->x + PLAT_COIN_R && g->x + PLAT_HALF_W > c->x - PLAT_COIN_R &&
            g->y - PLAT_H < c->y + PLAT_COIN_R && g->y > c->y - PLAT_COIN_R) {
            g->taken |= (uint16_t)(1u << i);
            g->last_coin = (int8_t)i;
            return true;
        }
    }
    return false;
}

/* one physics step of dt s; true when the player landed */
static bool move(plat_t *g, float walk, float dt)
{
    g->x = clampf(g->x + walk * WALK * dt, PLAT_X_MIN, PLAT_X_MAX);
    if (g->on < PLAT_NPLATS && !over(&PLAT_PLATFORMS[g->on], g->x)) {
        g->on = PLAT_ON_AIR; /* walked off the edge */
        g->vy = 0;
    }
    if (g->on != PLAT_ON_AIR) {
        return false;
    }
    float oy = g->y;
    g->vy = g->vy + GRAV * dt > FALL_MAX ? FALL_MAX : g->vy + GRAV * dt;
    g->y += g->vy * dt; /* no ceiling: a jump may rise past the top edge */
    if (g->vy <= 0) {
        return false;
    }
    /* platforms are landed on from above only */
    for (int i = 0; i < PLAT_NPLATS; i++) {
        const plat_platform_t *p = &PLAT_PLATFORMS[i];
        if (oy <= p->y && g->y >= p->y && over(p, g->x)) {
            g->y = p->y;
            g->vy = 0;
            g->on = (uint8_t)i;
            return true;
        }
    }
    if (g->y >= PLAT_GROUND) {
        g->y = PLAT_GROUND;
        g->vy = 0;
        g->on = PLAT_ON_GROUND;
        return true;
    }
    return false;
}

#define TILT_DEAD 55  /* mg: about 3 degrees */
#define TILT_FULL 260 /* mg: about 15 degrees */
#define TILT_MIN 0.3f /* walk share just past the dead zone: no sluggish creep */

float plat_tilt_walk(int tx)
{
    int a = tx < 0 ? -tx : tx;
    if (a <= TILT_DEAD) {
        return 0;
    }
    float w = TILT_MIN + (1 - TILT_MIN) * clampf((float)(a - TILT_DEAD) / (TILT_FULL - TILT_DEAD), 0, 1);
    return tx < 0 ? -w : w;
}

plat_ev_t plat_step(plat_t *g, float walk, uint32_t *dt_ms)
{
    if (g->r.status != GAME_PLAYING || g->run != PLAT_GO) {
        *dt_ms = 0;
        return PLAT_EV_NONE;
    }
    walk = clampf(walk, -1, 1);
    if (walk) {
        g->face = walk > 0 ? 1 : -1;
    }
    for (;;) {
        if (grab_coin(g)) {
            return PLAT_EV_COIN;
        }
        if (g->ms >= PLAT_LIMIT_MS || plat_coins_left(g) == 0) {
            end_run(g);
            return PLAT_EV_FINISH;
        }
        if (*dt_ms == 0) {
            return PLAT_EV_NONE;
        }
        uint32_t s = *dt_ms < SUB_MS ? *dt_ms : SUB_MS;
        if (walk == 0 && g->on != PLAT_ON_AIR) {
            s = *dt_ms; /* standing still: only the clock runs */
        }
        if (g->ms + s > PLAT_LIMIT_MS) {
            s = PLAT_LIMIT_MS - g->ms;
        }
        *dt_ms -= s;
        g->ms += s;
        if (move(g, walk, s / 1000.0f)) {
            return PLAT_EV_LAND;
        }
    }
}

/* The autopilot's route: walk to x, then (jump) jump there and hold air while in the
 * air, or (walk) go on to the next waypoint. Walking off an edge falls on the way. */
typedef struct {
    int16_t x;
    bool jump;
    int8_t air;
} waypoint_t;

static const waypoint_t ROUTE[] = {
    {120, false, 0}, /* the ground coin */
    {55, true, 0},   /* straight up onto the low left platform and its coin */
    {76, true, 1},   /* over to the middle platform, landing on its coin */
    {125, false, 0},
    {108, true, -1}, /* up to the high left platform */
    {75, false, 0},
    {108, false, 0}, /* off its right edge, down to the middle one */
    {146, true, 1},  /* over to the high right platform */
    {195, false, 0},
    {222, false, 0}, /* off its right edge, down to the low right one */
    {197, false, 0},
    {235, false, 0}, /* off to the ground at the right end */
    {235, true, 0},  /* the coin in the air */
};
#define ROUTE_LEN ((int)(sizeof(ROUTE) / sizeof(ROUTE[0])))
#define AI_TOL 1.5f /* px from a waypoint that counts as there (walking 2 px per 20 ms) */

static int toward(float from, int to)
{
    float d = to - from;
    return d > AI_TOL ? 1 : d < -AI_TOL ? -1 : 0;
}

void plat_autopilot(plat_t *g, int *dir, bool *jump)
{
    *dir = 0;
    *jump = false;
    if (g->r.status != GAME_PLAYING) {
        return;
    }
    while (g->ai < ROUTE_LEN) {
        const waypoint_t *w = &ROUTE[g->ai];
        if (g->on == PLAT_ON_AIR) {
            *dir = g->ai_jumped ? w->air : toward(g->x, w->x);
            return;
        }
        if (g->ai_jumped) {
            /* landed after the jump: on to the next waypoint */
            g->ai_jumped = false;
            g->ai++;
            continue;
        }
        *dir = toward(g->x, w->x);
        if (*dir) {
            return;
        }
        if (w->jump) {
            *jump = true;
            *dir = w->air;
            g->ai_jumped = true;
            return;
        }
        g->ai++;
    }
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <stdio.h>
#include "app.h"
#include "display.h"
#include "sound.h"

_Static_assert(sizeof(plat_t) <= GAME_STATE_MAX, "plat_t does not fit in a game slot");

#define INFO_Y 226
#define COL_COIN RGB(255, 200, 40)
#define COL_COIN_HI RGB(255, 240, 170)

static uint8_t mark_of(const plat_t *g) { return g->r.turn; }

/* the coins of player p so far: the finished run's, or the running one's */
static int coins_of(const plat_t *g, uint8_t p)
{
    if (g->coins[p] != PLAT_NOT_RUN) {
        return g->coins[p];
    }
    return g->r.status == GAME_PLAYING && g->r.turn == p ? popcount16(g->taken) : 0;
}

/* the points of player p: the finished run's, or the running one's coins so far */
static int points_of(const plat_t *g, uint8_t p)
{
    if (g->points[p] != PLAT_NOT_RUN) {
        return g->points[p];
    }
    return coins_of(g, p) * PLAT_COIN_PTS;
}

/* TURN O | WIN X x=.. o=.. | DRAW x=.. o=.. (points) */
static void print_result(const plat_t *g)
{
    if (g->r.status == GAME_WIN) {
        app_out("WIN %c x=%d o=%d", game_mark_char(g->r.winner), g->points[CELL_X], g->points[CELL_O]);
    } else if (g->r.status == GAME_DRAW) {
        app_out("DRAW x=%d o=%d", g->points[CELL_X], g->points[CELL_O]);
    } else {
        app_out("TURN %c", game_mark_char(g->r.turn));
    }
}

static void print_level(const plat_t *g)
{
    app_out("LEVEL plats=%d coins=%d id=%08lx limit=%ds coin_pts=%d sec_pts=1 ceiling=none", PLAT_NPLATS,
            PLAT_NCOINS, (unsigned long)g->id, PLAT_LIMIT_MS / 1000, PLAT_COIN_PTS);
}

static void plat_new_round(void *st, uint8_t first) { plat_start(st, first); }

static void plat_print(const void *st)
{
    const plat_t *g = st;
    print_level(g);
    print_result(g);
    if (g->r.status == GAME_PLAYING && g->run == PLAT_WAIT) {
        app_out("HINT plat arrow=right jump=RIGHT walk=tilt");
    }
}

static void plat_report(const void *st)
{
    const plat_t *g = st;
    char on[12];
    if (g->on == PLAT_ON_GROUND) {
        snprintf(on, sizeof(on), "ground");
    } else if (g->on == PLAT_ON_AIR) {
        snprintf(on, sizeof(on), "air");
    } else {
        snprintf(on, sizeof(on), "plat%d", g->on);
    }
    app_out("PLAYER x=%d y=%d on=%s coins=%d left=%d ms=%lu run=%s", (int)g->x, (int)g->y, on,
            popcount16(g->taken), plat_coins_left(g), (unsigned long)g->ms,
            g->r.status != GAME_PLAYING ? "over" : g->run == PLAT_GO ? "go" : "wait");
}

static void start_run(plat_t *g, const char *src)
{
    if (plat_go(g)) {
        app_out("GO %c src=%s", game_mark_char(mark_of(g)), src);
    }
}

/* the only move: RIGHT or a tap jumps (the first one also starts the run) */
static game_event_t plat_move(void *st, int m, const char *src)
{
    (void)m;
    plat_t *g = st;
    if (g->r.status != GAME_PLAYING) {
        app_out("ERR game over");
        return GEV_BAD;
    }
    start_run(g, src);
    if (!plat_jump(g)) {
        app_out("ERR airborne");
        /* a key or finger pressed in mid-air is no mistake worth a buzz */
        return strcmp(src, "touch") == 0 || strcmp(src, "button") == 0 ? GEV_NONE : GEV_BAD;
    }
    app_out("JUMP %c", game_mark_char(mark_of(g)));
    return GEV_NONE;
}

/* level: the state; level jump: a tap */
static game_event_t plat_command(void *st, const char *arg)
{
    if (strcmp(arg, "jump") == 0) {
        return plat_move(st, 0, "uart");
    }
    if (*arg) {
        app_out("ERR level [jump]");
        return GEV_BAD;
    }
    print_level(st);
    plat_report(st);
    print_result(st);
    return GEV_NONE;
}

static game_event_t plat_step_ev(void *st, const motion_t *m)
{
    plat_t *g = st;
    if (m->sim && g->r.status == GAME_PLAYING && g->run == PLAT_WAIT) {
        start_run(g, "sim");
    }
    if (m->jump && plat_jump(g)) {
        app_out("JUMP %c", game_mark_char(mark_of(g)));
    }
    uint8_t p = g->r.turn;
    uint32_t dt = m->dt_ms;
    /* UART walks hold a direction; otherwise the board's left / right tilt walks */
    float walk = m->dir ? (float)m->dir : plat_tilt_walk(m->tx);
    for (;;) {
        switch (plat_step(g, walk, &dt)) {
        case PLAT_EV_NONE:
            return GEV_NONE;
        case PLAT_EV_LAND:
            if (g->on == PLAT_ON_GROUND) {
                app_out("LAND ground");
            } else {
                app_out("LAND plat=%d", g->on);
            }
            break;
        case PLAT_EV_COIN:
            app_out("COIN %c idx=%d n=%d", game_mark_char(p), g->last_coin, popcount16(g->taken));
            app_out("SCORE X=%d O=%d", coins_of(g, CELL_X), coins_of(g, CELL_O));
            sound_play(SND_COIN);
            break;
        case PLAT_EV_FINISH: {
            app_out("FINISH %c coins=%d left=%ds points=%d", game_mark_char(p), g->coins[p], g->left_s,
                    g->points[p]);
            print_result(g);
            if (g->r.status != GAME_PLAYING) {
                sound_play(SND_END); /* then the app's win or draw tune */
                return g->r.status == GAME_WIN ? GEV_WIN : GEV_DRAW;
            }
            /* the second run of the round: the same level, every coin back */
            uint32_t id = plat_level_id(g);
            app_out("RUN %c id=%08lx same=%s", game_mark_char(g->r.turn), (unsigned long)id,
                    id == g->id ? "yes" : "no");
            app_out("HINT plat arrow=right jump=RIGHT walk=tilt");
            return GEV_RUN_END; /* the app plays the end of the run */
        }
        }
    }
}

static void plat_autopilot_ev(const void *st, motion_t *m)
{
    /* the route's progress lives in the round's state */
    plat_autopilot((plat_t *)st, &m->dir, &m->jump);
}

/* tilt walks while a run is going; RIGHT jumps */
static bool plat_steer(const void *st)
{
    const plat_t *g = st;
    return g->r.status == GAME_PLAYING && g->run == PLAT_GO;
}

static int plat_hit(int x, int y)
{
    (void)x;
    (void)y;
    return 0; /* a tap anywhere jumps */
}

static void plat_banner(const void *st, char *buf, int n, uint8_t *mark)
{
    const plat_t *g = st;
    if (g->r.status == GAME_WIN) {
        snprintf(buf, n, "%c GAGNE", game_player_char(g->r.winner));
        *mark = g->r.winner;
    } else if (g->r.status == GAME_DRAW) {
        snprintf(buf, n, "EGALITE");
        *mark = CELL_EMPTY;
    } else if (g->run == PLAT_WAIT) {
        snprintf(buf, n, "%c PRET", game_player_char(g->r.turn));
        *mark = g->r.turn;
    } else {
        /* the countdown */
        snprintf(buf, n, "%c %luS", game_player_char(g->r.turn), (unsigned long)((PLAT_LIMIT_MS - g->ms + 999) / 1000));
        *mark = g->r.turn;
    }
}

static void draw_coin(float x, float y, float r)
{
    gfx_disc(x, y, r, COL_COIN);
    gfx_disc(x - r * 0.3f, y - r * 0.3f, r * 0.3f, COL_COIN_HI);
}

/* the round's result over the level: both players' points, big, their coins and
 * seconds left, and the winner */
static void draw_final(const plat_t *g)
{
    const int x0 = 20, y0 = 62, w = 200, h = 128;
    uint16_t edge = g->r.status == GAME_WIN ? (g->r.winner == CELL_X ? COL_X : COL_O) : COL_TEXT;
    gfx_rect(x0, y0, w, h, COL_BG);
    gfx_rrect(x0, y0, w, h, 6, edge);
    gfx_text_mid(SCREEN_W / 2, y0 + 10, "SCORE FINAL", 1, COL_DIM);
    char buf[24];
    const uint8_t who[2] = {CELL_X, CELL_O};
    for (int i = 0; i < 2; i++) {
        uint8_t p = who[i];
        int cx = SCREEN_W / 2 + (i ? 50 : -50);
        uint16_t c = p == CELL_X ? COL_X : COL_O;
        snprintf(buf, sizeof(buf), "%c %d", game_player_char(p), g->points[p]);
        gfx_text_mid(cx, y0 + 26, buf, 3, c);
        snprintf(buf, sizeof(buf), "%d PIECES", g->coins[p]);
        gfx_text_mid(cx, y0 + 54, buf, 1, COL_TEXT);
        /* the time bonus: only a run that took every coin has seconds left */
        snprintf(buf, sizeof(buf), "+%dS RESTANTES", g->points[p] - g->coins[p] * PLAT_COIN_PTS);
        gfx_text_mid(cx, y0 + 64, buf, 1, COL_DIM);
    }
    if (g->r.status == GAME_WIN) {
        snprintf(buf, sizeof(buf), "%c GAGNE !", game_player_char(g->r.winner));
    } else {
        snprintf(buf, sizeof(buf), "EGALITE");
    }
    gfx_text_mid(SCREEN_W / 2, y0 + 82, buf, 2, edge);
    gfx_text_mid(SCREEN_W / 2, y0 + 108, "DROITE : NOUVELLE MANCHE", 1, COL_DIM);
}

/* before a run: how to play, with an arrow up to the RIGHT key on the top edge */
static void draw_howto(const plat_t *g)
{
    const int x0 = 14, y0 = 58, w = 178, h = 96;
    const int ax = 214; /* the arrow, up to just under the top right score */
    uint16_t c = g->r.turn == CELL_X ? COL_X : COL_O;
    gfx_rect(x0, y0, w, h, COL_BG);
    gfx_rrect(x0, y0, w, h, 6, c);
    gfx_text_mid(x0 + w / 2, y0 + 10, "DROITE = SAUT", 2, COL_ACCENT);
    gfx_text_mid(x0 + w / 2, y0 + 34, "PENCHE POUR MARCHER", 1, COL_TEXT);
    gfx_text_mid(x0 + w / 2, y0 + 50, "PIECE 10 PTS", 1, COL_COIN);
    gfx_text_mid(x0 + w / 2, y0 + 60, "+1 PT PAR SECONDE RESTANTE", 1, COL_COIN);
    gfx_text_mid(x0 + w / 2, y0 + 78, "APPUIE SUR DROITE", 1, c);
    /* from the box's "DROITE = SAUT" line over to the corner, then up to the key */
    gfx_line(x0 + w, y0 + 17, ax, y0 + 17, 3, COL_ACCENT);
    gfx_line(ax, y0 + 17, ax, 34, 3, COL_ACCENT);
    gfx_line(ax, 32, ax - 9, 43, 3, COL_ACCENT);
    gfx_line(ax, 32, ax + 9, 43, 3, COL_ACCENT);
    gfx_text_mid(ax, y0 + 24, "SAUT", 1, COL_ACCENT);
}

static void plat_draw(const void *st, int preview)
{
    (void)preview;
    const plat_t *g = st;
    for (int i = 0; i < PLAT_NPLATS; i++) {
        const plat_platform_t *p = &PLAT_PLATFORMS[i];
        gfx_rect(p->x0, p->y, p->x1 - p->x0, 5, COL_LINE);
        gfx_rect(p->x0, p->y, p->x1 - p->x0, 2, COL_DIM);
    }
    gfx_rect(0, PLAT_GROUND, SCREEN_W, 2, COL_DIM);
    gfx_rect(0, PLAT_GROUND + 2, SCREEN_W, 8, COL_LINE);
    if (g->r.status == GAME_PLAYING) {
        for (int i = 0; i < PLAT_NCOINS; i++) {
            if (!(g->taken >> i & 1)) {
                draw_coin(PLAT_COINS[i].x, PLAT_COINS[i].y, PLAT_COIN_R);
            }
        }
        uint8_t p = g->r.turn;
        int x = (int)g->x, y = (int)g->y;
        gfx_rect(x - PLAT_HALF_W, y - PLAT_H, 2 * PLAT_HALF_W, PLAT_H, p == CELL_X ? COL_X : COL_O);
        gfx_rect(x + g->face * 2 - 1, y - PLAT_H + 3, 2, 3, COL_BG); /* the eye looks the way it walks */
    }

    char line[24];
    int x = points_of(g, CELL_X), o = points_of(g, CELL_O);
    bool x_known = g->coins[CELL_X] != PLAT_NOT_RUN || (g->r.status == GAME_PLAYING && g->r.turn == CELL_X);
    bool o_known = g->coins[CELL_O] != PLAT_NOT_RUN || (g->r.status == GAME_PLAYING && g->r.turn == CELL_O);
    if (x_known) {
        snprintf(line, sizeof(line), "A %d", x);
    } else {
        snprintf(line, sizeof(line), "A -");
    }
    draw_coin(10, INFO_Y + 3, 4);
    gfx_text(18, INFO_Y, line, 1, COL_X);
    if (o_known) {
        snprintf(line, sizeof(line), "B %d", o);
    } else {
        snprintf(line, sizeof(line), "B -");
    }
    gfx_text_right(222, INFO_Y, line, 1, COL_O);
    draw_coin(230, INFO_Y + 3, 4);
    if (g->r.status != GAME_PLAYING) {
        draw_final(g);
    } else {
        if (g->run == PLAT_WAIT) {
            draw_howto(g);
            snprintf(line, sizeof(line), "DROITE = DEPART");
        } else {
            snprintf(line, sizeof(line), "%d/%d  %luS", popcount16(g->taken), PLAT_NCOINS,
                     (unsigned long)((PLAT_LIMIT_MS - g->ms + 999) / 1000));
        }
        gfx_text_mid(SCREEN_W / 2, INFO_Y, line, 1, COL_TEXT);
    }
}

static void plat_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    float k = s / 48.0f;
    gfx_rect(x + (int)(4 * k), y + (int)(40 * k), (int)(40 * k), 2, COL_DIM);   /* ground */
    gfx_rect(x + (int)(8 * k), y + (int)(28 * k), (int)(14 * k), 2, COL_DIM);   /* platforms */
    gfx_rect(x + (int)(26 * k), y + (int)(17 * k), (int)(14 * k), 2, COL_DIM);
    draw_coin(x + 15 * k, y + 23 * k, 3 * k);
    draw_coin(x + 33 * k, y + 12 * k, 3 * k);
    gfx_rect(x + (int)(30 * k), y + (int)(31 * k), (int)(5 * k), (int)(9 * k), COL_X); /* the player */
}

const game_def_t COIN_PLATFORMER = {
    .id = "plat",
    .title = "CHASSE AUX PIECES",
    .verb = "level",
    .new_round = plat_new_round,
    .print = plat_print,
    .move = plat_move,
    .command = plat_command,
    .hit = plat_hit,
    .draw = plat_draw,
    .icon = plat_icon,
    .step = plat_step_ev,
    .report = plat_report,
    .autopilot = plat_autopilot_ev,
    .banner = plat_banner,
    .steer = plat_steer,
};
#endif
