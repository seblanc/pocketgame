#pragma once
#include "game.h"

/* Coin Platformer: each player in turn has a 60 s run on the same level of small
 * platforms with coins on them. Tilting the board left / right walks (flat or held
 * upright), RIGHT or a tap on the screen jumps (from the ground or a platform only); gravity pulls the player down, platforms are landed on
 * from above (jumped through from below) and walking off one falls to the level below.
 * Touching a coin collects it. A run starts on the first tap and ends after 60 s or
 * when every coin is taken; then the other player runs the level with all coins back.
 * More coins wins, equal coins is a draw. The rules are pure C, also built on the
 * host by tests/ (-DGAME_RULES_ONLY).
 * UART: level -> LEVEL plats=n coins=n id=hex, PLAYER ..., TURN X; jump (a tap) ->
 * GO X + JUMP X | ERR airborne; sim jump | sim move L|R <ms> | sim wait <ms> |
 * sim autoplay; events LAND ground | LAND plat=n, COIN X idx=n n=n + SCORE X=n O=n,
 * FINISH X coins=n, then TURN O + RUN O id=hex same=yes | WIN X x=n o=n | DRAW x=n o=n */

#define PLAT_LIMIT_MS 60000
#define PLAT_TOP 40     /* the play area, below the turn banner: the ceiling */
#define PLAT_GROUND 212 /* the ground's top: the feet's y on the ground */
#define PLAT_HALF_W 5   /* the player is 10 x 14 px, x at its middle, y at its feet */
#define PLAT_H 14
#define PLAT_X_MIN PLAT_HALF_W
#define PLAT_X_MAX (240 - PLAT_HALF_W)
#define PLAT_START_X 20
#define PLAT_COIN_R 5
#define PLAT_NOT_RUN (-1) /* coins[] of a player still to run */

#define PLAT_ON_AIR 0xff
#define PLAT_ON_GROUND 0xfe /* else the index of the platform stood on */

typedef struct {
    int16_t x0, x1, y; /* the top surface from x0 to x1 */
} plat_platform_t;

typedef struct {
    int16_t x, y; /* centre */
} plat_coin_t;

/* the one level, the same for every round */
extern const plat_platform_t PLAT_PLATFORMS[];
extern const int PLAT_NPLATS;
extern const plat_coin_t PLAT_COINS[];
extern const int PLAT_NCOINS;

typedef enum { PLAT_WAIT, PLAT_GO } plat_run_t;
typedef enum { PLAT_EV_NONE, PLAT_EV_LAND, PLAT_EV_COIN, PLAT_EV_FINISH } plat_ev_t;

typedef struct {
    game_round_t r;   /* first member: see game.h; r.turn = the player running */
    float x, y, vy;   /* feet middle in screen px; px/s, > 0 down */
    uint32_t ms;      /* time of the run so far */
    uint32_t id;      /* plat_level_id() when the round started */
    uint16_t taken;   /* bit i: coin i collected in this run */
    int8_t coins[3];  /* by mark: coins of the run, PLAT_NOT_RUN */
    int8_t face;      /* -1 left, 1 right: the way the player looks */
    int8_t last_coin; /* the coin of the last PLAT_EV_COIN */
    uint8_t on;       /* PLAT_ON_AIR | PLAT_ON_GROUND | platform index */
    uint8_t run;      /* plat_run_t */
    uint8_t ai;       /* the autopilot's place on its route, from the start of the run */
    bool ai_jumped;   /* the autopilot jumped from its waypoint and waits for the landing */
} plat_t;

void plat_start(plat_t *g, uint8_t first);
/* a hash of the level and the coins still out: the same for both runs of a round */
uint32_t plat_level_id(const plat_t *g);
/* starts the waiting run; false if it already runs or the round is over */
bool plat_go(plat_t *g);
/* a jump of the running player; false while airborne */
bool plat_jump(plat_t *g);
/* Advances the running round by up to *dt_ms walking at walk (-1 full speed left .. 1
 * full speed right) and returns at the first event, *dt_ms then holds the time left
 * (call again until it is 0). A run that ends (PLAT_EV_FINISH) hands over to the other
 * player or ends the round. */
plat_ev_t plat_step(plat_t *g, float walk, uint32_t *dt_ms);
/* the walk speed for a screen tilt tx in mg (> 0 right edge lower): none in a small
 * dead zone around level, then growing to full speed at about 25 degrees. The x
 * tilt is the same whether the board lies flat or is held upright facing the player. */
float plat_tilt_walk(int tx);
/* sim autoplay: the walk direction and whether to jump for the next step, along a
 * fixed route that collects every coin */
void plat_autopilot(plat_t *g, int *dir, bool *jump);
int plat_coins_left(const plat_t *g);

extern const game_def_t COIN_PLATFORMER;
