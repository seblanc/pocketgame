#pragma once
#include "game.h"

/* Tilt Duel: each player in turn tilts the board to roll a ball through an 8 x 8 maze
 * (several turns, dead ends) from the top-left cell to the target in the bottom-right
 * one; the faster run wins, a
 * run that takes 60 s times out (two timeouts or equal times are a draw). A run starts
 * on a tap (or on the first simulated tilt). The rules are pure C, also built on the
 * host by tests/ (-DGAME_RULES_ONLY).
 * UART: maze [go] -> MAZE ... / GO X; sim tilt <ax> <ay> <ms> -> BALL cell=c,r ...;
 * a run ends with FINISH X ms=n | TIMEOUT X, then TURN O | WIN X x=n o=n | DRAW */

#define TILT_COLS 8
#define TILT_ROWS 8
#define TILT_CELL 23     /* px */
#define TILT_LIMIT_MS 60000
#define TILT_NOT_RUN (-1) /* time[] of a player still to run */
#define TILT_TIMEOUT (-2) /* time[] of a run that timed out */

typedef enum { TILT_WAIT, TILT_GO } tilt_run_t;
typedef enum { TILT_EV_NONE, TILT_EV_FINISH, TILT_EV_TIMEOUT } tilt_ev_t;

typedef struct {
    game_round_t r;     /* first member: see game.h; r.turn = the player running */
    float x, y, vx, vy; /* ball centre in maze px (0,0 = top-left corner), px/s */
    uint32_t ms;        /* time of the run so far */
    int32_t time[3];    /* by mark: finish ms, TILT_TIMEOUT or TILT_NOT_RUN */
    uint8_t run;        /* tilt_run_t */
} tilt_t;

void tilt_start(tilt_t *g, uint8_t first);
/* starts the waiting run; false if it already runs or the round is over */
bool tilt_go(tilt_t *g);
/* rolls the running ball for dt_ms with tilt tx, ty (mg; nothing while the run waits).
 * A run that ends (FINISH / TIMEOUT) hands over to the other player or ends the round. */
tilt_ev_t tilt_step(tilt_t *g, int tx, int ty, uint32_t dt_ms);
void tilt_cell(const tilt_t *g, int *col, int *row);

extern const game_def_t TILT_DUEL;
