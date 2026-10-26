#pragma once
#include "game.h"

/* Steady Hand: each player in turn keeps a bubble, driven by the board's tilt, inside a
 * circle that shrinks over time; the run ends when the bubble's centre leaves the
 * circle (or after 60 s). The longer survival wins (equal times are a draw). A run
 * starts on a tap (or on the first simulated tilt). The rules are pure C, also built on
 * the host by tests/ (-DGAME_RULES_ONLY).
 * UART: hold [go] -> GO X; sim tilt <ax> <ay> <ms> -> BUBBLE d=<px> r=<px> ...;
 * a run ends with OUT X ms=n, then TURN O | WIN X x=n o=n | DRAW x=n o=n */

#define STEADY_LIMIT_MS 60000
#define STEADY_R0 80.0f      /* px: the circle at the start */
#define STEADY_RMIN 8.0f     /* px: what it shrinks towards */
#define STEADY_SHRINK_MS 10000.0f /* time constant of the shrink */
#define STEADY_PX_PER_G 150.0f /* bubble offset for a 1 g tilt */
#define STEADY_TAU_MS 120.0f /* the bubble follows the tilt with this lag */
#define STEADY_NOT_RUN (-1)

typedef enum { STEADY_WAIT, STEADY_GO } steady_run_t;

typedef struct {
    game_round_t r;   /* first member: see game.h; r.turn = the player holding */
    float x, y;       /* bubble offset from the circle's centre, px */
    uint32_t ms;      /* time of the run so far */
    int32_t time[3];  /* by mark: survival ms or STEADY_NOT_RUN */
    uint8_t run;      /* steady_run_t */
} steady_t;

void steady_start(steady_t *g, uint8_t first);
/* starts the waiting run; false if it already runs or the round is over */
bool steady_go(steady_t *g);
/* the circle's radius after ms of a run */
float steady_radius(uint32_t ms);
/* moves the bubble for dt_ms with tilt tx, ty (mg); true when the run ended (the
 * bubble left, or the time limit): the other player's turn or the round's result */
bool steady_step(steady_t *g, int tx, int ty, uint32_t dt_ms);

extern const game_def_t STEADY_HAND;
