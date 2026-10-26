#pragma once
#include "game.h"

/* Steady Hand: each player in turn keeps a bubble, driven by the board's tilt, inside a
 * target circle. The circle shrinks; every STEADY_MOVE_MS it jumps to a new random
 * place on the screen (a little smaller each time) and shrinks again, and so on. After
 * a jump the player has STEADY_GRACE_MS to bring the bubble into the new circle. The run
 * ends when the bubble's centre leaves the circle (or after 60 s). The longer survival
 * wins (equal times are a draw). A run starts on a tap (or on the first simulated
 * tilt). The rules are pure C, also built on the host by tests/ (-DGAME_RULES_ONLY).
 * UART: hold [go] -> GO X; sim tilt <ax> <ay> <ms> -> BUBBLE d=<px> r=<px> ...;
 * CIRCLE move x=<px> y=<px> r=<px> at each jump (screen position of the new circle);
 * a run ends with OUT X ms=n, then TURN O | WIN X x=n o=n | DRAW x=n o=n */

#define STEADY_LIMIT_MS 60000
#define STEADY_R0 80.0f          /* px: the circle at the start */
#define STEADY_R_STEP 8.0f       /* each jump starts this much smaller... */
#define STEADY_R_START_MIN 36.0f /* ...down to this */
#define STEADY_SHRINK 0.55f      /* the circle shrinks to this part of its size between jumps */
#define STEADY_MOVE_MS 4000      /* time between jumps */
#define STEADY_GRACE_MS 1200     /* after a jump: time to reach the new circle */
#define STEADY_PX_PER_G 200.0f   /* bubble offset for a 1 g tilt */
#define STEADY_TAU_MS 120.0f     /* the bubble follows the tilt with this lag */
#define STEADY_NOT_RUN (-1)

/* where the circle may go, as offsets from the play area's centre (screen 120,128) */
#define STEADY_AREA_HALF_W 116
#define STEADY_AREA_HALF_H 86

typedef enum { STEADY_WAIT, STEADY_GO } steady_run_t;
typedef enum { STEADY_EV_NONE, STEADY_EV_OUT } steady_ev_t;

typedef struct {
    game_round_t r;   /* first member: see game.h; r.turn = the player holding */
    float x, y;       /* bubble offset from the play area's centre, px */
    float cx, cy;     /* the circle's centre, the same way */
    float r0;         /* the circle's size at the last jump (or the start) */
    uint32_t ms;      /* time of the run so far */
    uint32_t seg_ms;  /* time since the last jump */
    int32_t time[3];  /* by mark: survival ms or STEADY_NOT_RUN */
    uint8_t run;      /* steady_run_t */
    uint8_t jumps;    /* jumps in this run (wraps): the console reports each one */
    bool reached;     /* the bubble has been inside the circle since the last jump */
} steady_t;

void steady_start(steady_t *g, uint8_t first);
/* starts the waiting run; false if it already runs or the round is over */
bool steady_go(steady_t *g);
/* the circle's radius now */
float steady_radius(const steady_t *g);
/* the bubble's distance from the circle's centre, px */
float steady_dist(const steady_t *g);
/* moves the bubble (and the circle) for dt_ms with tilt tx, ty (mg). STEADY_EV_OUT when
 * the run ended (the bubble left, or the time limit): the other player's turn or the
 * round's result. Jumps of the circle count up g->jumps. */
steady_ev_t steady_step(steady_t *g, int tx, int ty, uint32_t dt_ms);

extern const game_def_t STEADY_HAND;
