#include "tilt.h"
#include <math.h>
#include <string.h>

#define TARGET_COL (TILT_COLS - 1)
#define TARGET_ROW (TILT_ROWS - 1)
#define CELLS (TILT_COLS * TILT_ROWS)
#define TARGET (CELLS - 1) /* cells are numbered row by row: r * TILT_COLS + c */
#define WALL_HALF 1.0f /* walls are 2 px thick, centred on the grid lines */
#define BALL_R 7.0f
#define G_PX 900.0f    /* px/s^2 of a 1 g tilt */
#define DRAG 1.5f      /* rolling friction, 1/s */
#define VMAX 400.0f    /* px/s */
#define TILT_MAX 1500  /* mg */
#define SUB_MS 2       /* physics step: under 1 px per step at VMAX */

/* the wall on vertical grid line k (x = k * TILT_CELL) beside row r */
bool tilt_v_wall(const tilt_maze_t *m, int k, int r) { return k <= 0 || k >= TILT_COLS || (m->v[r] >> (k - 1) & 1); }
/* the wall on horizontal grid line k (y = k * TILT_CELL) above/below column c */
bool tilt_h_wall(const tilt_maze_t *m, int k, int c) { return k <= 0 || k >= TILT_ROWS || (m->h[k - 1] >> c & 1); }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- the maze ---- */

/* the four directions: right, down, left, up */
static const int DC[4] = {1, 0, -1, 0}, DR[4] = {0, 1, 0, -1};

/* is the side d of cell n open? */
static bool open_side(const tilt_maze_t *m, int n, int d)
{
    int c = n % TILT_COLS, r = n / TILT_COLS;
    switch (d) {
    case 0:
        return !tilt_v_wall(m, c + 1, r);
    case 1:
        return !tilt_h_wall(m, r + 1, c);
    case 2:
        return !tilt_v_wall(m, c, r);
    default:
        return !tilt_h_wall(m, r, c);
    }
}

static void knock_down(tilt_maze_t *m, int n, int d)
{
    int c = n % TILT_COLS, r = n / TILT_COLS;
    switch (d) {
    case 0:
        m->v[r] &= ~(1u << c);
        break;
    case 1:
        m->h[r] &= ~(1u << c);
        break;
    case 2:
        m->v[r] &= ~(1u << (c - 1));
        break;
    default:
        m->h[r - 1] &= ~(1u << c);
        break;
    }
}

/* xorshift32: the same seed gives the same maze on the board and on the host */
static uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* a depth-first carve from the start: long winding corridors, every cell reached once */
static void carve(tilt_maze_t *m, uint32_t *rng)
{
    memset(m->v, 0x7f, sizeof(m->v));
    memset(m->h, 0xff, sizeof(m->h));
    uint8_t stack[CELLS];
    bool seen[CELLS] = {false};
    int sp = 0;
    stack[sp++] = 0;
    seen[0] = true;
    while (sp > 0) {
        int n = stack[sp - 1], c = n % TILT_COLS, r = n / TILT_COLS;
        int dirs[4], k = 0;
        for (int d = 0; d < 4; d++) {
            int nc = c + DC[d], nr = r + DR[d];
            if (nc >= 0 && nc < TILT_COLS && nr >= 0 && nr < TILT_ROWS && !seen[nr * TILT_COLS + nc]) {
                dirs[k++] = d;
            }
        }
        if (k == 0) {
            sp--;
            continue;
        }
        int d = dirs[rnd(rng) % k];
        int next = (r + DR[d]) * TILT_COLS + c + DC[d];
        knock_down(m, n, d);
        seen[next] = true;
        stack[sp++] = (uint8_t)next;
    }
}

/* breadth-first from the target: par[n] is the next cell from n on the way to the
 * target (-1 for the target and unreachable cells); returns the cells reached */
static int toward_target(const tilt_maze_t *m, int8_t *par)
{
    uint8_t queue[CELLS];
    bool seen[CELLS] = {false};
    int head = 0, tail = 0;
    memset(par, -1, CELLS);
    queue[tail++] = TARGET;
    seen[TARGET] = true;
    while (head < tail) {
        int n = queue[head++];
        for (int d = 0; d < 4; d++) {
            if (!open_side(m, n, d)) {
                continue;
            }
            int next = n + DR[d] * TILT_COLS + DC[d];
            if (!seen[next]) {
                seen[next] = true;
                par[next] = (int8_t)n;
                queue[tail++] = (uint8_t)next;
            }
        }
    }
    return tail;
}

void tilt_maze_stats(const tilt_maze_t *m, tilt_stats_t *s)
{
    int8_t par[CELLS];
    s->reach = toward_target(m, par); /* the walls are symmetric: the same from the start */
    if (par[0] < 0) {
        s->reach = 1;
    }
    s->path = 1;
    s->turns = 0;
    int step = 0;
    for (int n = 0; n != TARGET && par[n] >= 0; n = par[n]) {
        int st = par[n] - n;
        if (step && st != step) {
            s->turns++;
        }
        step = st;
        s->path++;
    }
    s->dead_ends = 0;
    for (int n = 1; n < TARGET; n++) {
        int sides = 0;
        for (int d = 0; d < 4; d++) {
            sides += open_side(m, n, d);
        }
        s->dead_ends += sides == 1;
    }
}

uint32_t tilt_maze_id(const tilt_maze_t *m)
{
    /* FNV-1a over the wall bytes */
    const uint8_t *p = (const uint8_t *)m;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*m); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

#define MAZE_TRIES 64

void tilt_maze_make(tilt_maze_t *m, uint32_t seed)
{
    uint32_t rng = seed * 2654435761u ^ 0x9e3779b9u; /* spread small seeds; never 0 */
    if (rng == 0) {
        rng = 1;
    }
    /* carve until the maze has a few turns and dead ends (almost always the first one) */
    for (int i = 0; i < MAZE_TRIES; i++) {
        carve(m, &rng);
        tilt_stats_t s;
        tilt_maze_stats(m, &s);
        if (s.reach == CELLS && s.turns >= 4 && s.dead_ends >= 4 && m->v[0] != 0) {
            return;
        }
    }
}

/* ---- the ball ---- */

/* does the ball's square at x, y overlap a wall of m? */
static bool blocked(const tilt_maze_t *m, float x, float y)
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
            if (r * TILT_CELL - WALL_HALF < y1 && (r + 1) * TILT_CELL + WALL_HALF > y0 && tilt_v_wall(m, k, r)) {
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
            if (c * TILT_CELL - WALL_HALF < x1 && (c + 1) * TILT_CELL + WALL_HALF > x0 && tilt_h_wall(m, k, c)) {
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

void tilt_start(tilt_t *g, uint8_t first, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->time[CELL_X] = g->time[CELL_O] = TILT_NOT_RUN;
    g->seed = seed;
    tilt_maze_make(&g->maze, seed);
    g->id = tilt_maze_id(&g->maze);
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
    if (blocked(&g->maze, nx, g->y)) {
        g->vx = 0;
    } else {
        g->x = nx;
    }
    float ny = g->y + g->vy * dt;
    if (blocked(&g->maze, g->x, ny)) {
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

/* The autopilot steers each axis to a wanted speed that falls off near the waypoint
 * (so the ball arrives without overshooting); it turns only from the centre of a cell. */
#define AUTO_VMAX 200.0f /* px/s */
#define AUTO_KP 4.0f     /* 1/s: wanted speed per px left to the waypoint */
#define AUTO_KV 15.0f    /* 1/s: how hard the speed follows */
#define AUTO_ALIGN 2.0f  /* px off the corridor's middle that still passes an opening */
#define AUTO_VSIDE 30.0f /* px/s sideways that still counts as lined up */

static int auto_axis(float err, float v)
{
    float want = fmaxf(-AUTO_VMAX, fminf(AUTO_VMAX, AUTO_KP * err));
    float a = AUTO_KV * (want - v) + DRAG * v; /* px/s^2, the rolling friction made up for */
    return clampi((int)lroundf(a * 1000.0f / G_PX), -TILT_MAX, TILT_MAX);
}

void tilt_autopilot(const tilt_t *g, int *tx, int *ty)
{
    int8_t par[CELLS];
    toward_target(&g->maze, par);
    int c, r;
    tilt_cell(g, &c, &r);
    int n = r * TILT_COLS + c;
    /* by default the middle of the ball's cell: lines it up for the next leg */
    float wx = (c + 0.5f) * TILT_CELL, wy = (r + 0.5f) * TILT_CELL;
    if (n != TARGET && par[n] >= 0) {
        int step = par[n] - n;
        bool across = step == 1 || step == -1;
        bool lined_up = across ? fabsf(g->y - wy) <= AUTO_ALIGN && fabsf(g->vy) <= AUTO_VSIDE
                               : fabsf(g->x - wx) <= AUTO_ALIGN && fabsf(g->vx) <= AUTO_VSIDE;
        if (lined_up) {
            /* the end of the straight leg: the next turn, or the target */
            int end = par[n];
            while (end != TARGET && par[end] - end == step) {
                end = par[end];
            }
            wx = (end % TILT_COLS + 0.5f) * TILT_CELL;
            wy = (end / TILT_COLS + 0.5f) * TILT_CELL;
        }
    }
    *tx = auto_axis(wx - g->x, g->vx);
    *ty = auto_axis(wy - g->y, g->vy);
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

/* "sim seed <n>": the maze of the next round, for a repeatable test (then random again) */
static uint32_t s_next_seed;
static bool s_seeded;

static void tilt_new_round(void *st, uint8_t first)
{
    uint32_t seed = s_seeded ? s_next_seed : game_random();
    s_seeded = false;
    tilt_start(st, first, seed);
}

static bool tilt_sim_set(void *st, const char *key, long val)
{
    (void)st;
    if (strcmp(key, "seed") != 0 || val < 0) {
        return false;
    }
    s_next_seed = (uint32_t)val;
    s_seeded = true;
    app_out("SIM seed=%ld (the maze of the next new game)", val);
    return true;
}

static void print_maze(const tilt_t *g)
{
    tilt_stats_t s;
    tilt_maze_stats(&g->maze, &s);
    app_out("MAZE %dx%d start=0,0 target=%d,%d limit=%ds seed=%lu id=%08lx reach=%d turns=%d dead_ends=%d path=%d",
            TILT_COLS, TILT_ROWS, TARGET_COL, TARGET_ROW, TILT_LIMIT_MS / 1000, (unsigned long)g->seed,
            (unsigned long)g->id, s.reach, s.turns, s.dead_ends, s.path);
}

static void tilt_print(const void *st)
{
    print_maze(st);
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
    print_maze(st);
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
    /* the second run of the round is on the maze the first one had */
    uint32_t id = tilt_maze_id(&g->maze);
    app_out("RUN %c seed=%lu id=%08lx same=%s", game_mark_char(g->r.turn), (unsigned long)g->seed,
            (unsigned long)id, id == g->id ? "yes" : "no");
    return p == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
}

static void tilt_autopilot_ev(const void *st, motion_t *m) { tilt_autopilot(st, &m->tx, &m->ty); }

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
        snprintf(b, n, "TEMPS");
    } else {
        snprintf(b, n, "%ld.%ld", (long)(ms / 1000), (long)(ms / 100 % 10));
    }
}

static void draw_walls(const tilt_maze_t *m, int x0, int y0, float cell, float w, uint16_t col)
{
    for (int r = 0; r < TILT_ROWS; r++) {
        for (int k = 0; k <= TILT_COLS; k++) {
            if (tilt_v_wall(m, k, r)) {
                gfx_rect(x0 + (int)(k * cell - w / 2), y0 + (int)(r * cell - w / 2), (int)w, (int)(cell + w), col);
            }
        }
    }
    for (int k = 0; k <= TILT_ROWS; k++) {
        for (int c = 0; c < TILT_COLS; c++) {
            if (tilt_h_wall(m, k, c)) {
                gfx_rect(x0 + (int)(c * cell - w / 2), y0 + (int)(k * cell - w / 2), (int)(cell + w), (int)w, col);
            }
        }
    }
}

static void tilt_draw(const void *st, int preview)
{
    (void)preview;
    const tilt_t *g = st;
    draw_walls(&g->maze, MAZE_X0, MAZE_Y0, TILT_CELL, 2, COL_DIM);
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
            snprintf(line, sizeof(line), "TOUCHE = DEPART");
        } else {
            secs(line, sizeof(line), (int32_t)g->ms);
        }
        gfx_text_mid(SCREEN_W / 2, INFO_Y, line, 1, COL_TEXT);
    }
}

static void tilt_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    static tilt_maze_t icon; /* one fixed maze for the menu tile (display task only) */
    static bool made;
    if (!made) {
        tilt_maze_make(&icon, 1);
        made = true;
    }
    float cell = s / (float)TILT_COLS;
    draw_walls(&icon, x, y, cell, 1, COL_DIM);
    gfx_disc(x + cell / 2, y + cell / 2, cell * 0.4f, COL_X);
    gfx_disc(x + s - cell / 2, y + s - cell / 2, cell * 0.4f, COL_ACCENT);
}

const game_def_t TILT_DUEL = {
    .id = "tilt",
    .title = "LABYRINTHE",
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
    .sim_set = tilt_sim_set,
    .autopilot = tilt_autopilot_ev,
};
#endif
