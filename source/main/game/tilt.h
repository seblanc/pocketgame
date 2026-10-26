#pragma once
#include "game.h"

/* Tilt Duel: each player in turn tilts the board to roll a ball through an 8 x 8 maze
 * (several turns, dead ends) from the top-left cell to the target in the bottom-right
 * one; the faster run wins, a
 * run that takes 60 s times out (two timeouts or equal times are a draw). A run starts
 * on a tap (or on the first simulated tilt). Every round gets a new random maze from
 * a seed (a tree: every cell reachable, one way between any two), the same for both
 * runs of the round. The rules are pure C, also built on the host by tests/
 * (-DGAME_RULES_ONLY).
 * UART: maze [go] -> MAZE 8x8 ... seed=n id=hex reach=64 turns=n dead_ends=n / GO X;
 * sim seed <n> -> SIM seed=n (the maze of the next round); sim solve rolls the ball
 * along the way out; sim tilt <ax> <ay> <ms> -> BALL cell=c,r ...;
 * a run ends with FINISH X ms=n | TIMEOUT X, then TURN O + RUN O seed=n id=hex same=yes |
 * WIN X x=n o=n | DRAW */

#define TILT_COLS 8
#define TILT_ROWS 8
#define TILT_CELL 23     /* px */
#define TILT_LIMIT_MS 60000
#define TILT_NOT_RUN (-1) /* time[] of a player still to run */
#define TILT_TIMEOUT (-2) /* time[] of a run that timed out */

typedef enum { TILT_WAIT, TILT_GO } tilt_run_t;
typedef enum { TILT_EV_NONE, TILT_EV_FINISH, TILT_EV_TIMEOUT } tilt_ev_t;

/* the inner walls (the outer frame is always closed); a set bit is a wall */
typedef struct {
    uint8_t v[TILT_ROWS];     /* bit k-1 of v[r]: the wall on vertical grid line k beside row r */
    uint8_t h[TILT_ROWS - 1]; /* bit c of h[k-1]: the wall on horizontal grid line k at column c */
} tilt_maze_t;

typedef struct {
    int reach;     /* cells reachable from the start */
    int path;      /* cells on the way from the start to the target, both included */
    int turns;     /* changes of direction along that way */
    int dead_ends; /* cells with one opening, other than the start and the target */
} tilt_stats_t;

typedef struct {
    game_round_t r;     /* first member: see game.h; r.turn = the player running */
    float x, y, vx, vy; /* ball centre in maze px (0,0 = top-left corner), px/s */
    uint32_t ms;        /* time of the run so far */
    int32_t time[3];    /* by mark: finish ms, TILT_TIMEOUT or TILT_NOT_RUN */
    uint32_t seed;      /* the maze of this round */
    uint32_t id;        /* tilt_maze_id() of the maze when the round started */
    tilt_maze_t maze;
    uint8_t run;        /* tilt_run_t */
} tilt_t;

/* a new round on the maze of seed (the same seed always gives the same maze) */
void tilt_start(tilt_t *g, uint8_t first, uint32_t seed);
/* the maze of seed: a random tree with at least 4 turns on the way out, 4 dead ends and
 * a wall in the top row (a ball tilted right from the start stops short of the side) */
void tilt_maze_make(tilt_maze_t *m, uint32_t seed);
void tilt_maze_stats(const tilt_maze_t *m, tilt_stats_t *s);
/* a hash of the walls */
uint32_t tilt_maze_id(const tilt_maze_t *m);
/* walls by grid line, as drawn: vertical line k beside row r, horizontal line k at column c */
bool tilt_v_wall(const tilt_maze_t *m, int k, int r);
bool tilt_h_wall(const tilt_maze_t *m, int k, int c);
/* the tilt (mg) for the next step that steers the ball along the way out (sim solve) */
void tilt_autopilot(const tilt_t *g, int *tx, int *ty);
/* starts the waiting run; false if it already runs or the round is over */
bool tilt_go(tilt_t *g);
/* rolls the running ball for dt_ms with tilt tx, ty (mg; nothing while the run waits).
 * A run that ends (FINISH / TIMEOUT) hands over to the other player or ends the round. */
tilt_ev_t tilt_step(tilt_t *g, int tx, int ty, uint32_t dt_ms);
void tilt_cell(const tilt_t *g, int *col, int *row);

extern const game_def_t TILT_DUEL;
