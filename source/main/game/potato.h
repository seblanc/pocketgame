#pragma once
#include "game.h"

/* Hot Potato: the board is passed between the players while a hidden fuse (random,
 * 8-20 s) burns; whoever holds it when the fuse goes off loses. The holder may pass
 * after holding it 2 s. The fuse burns from the start of the round and ticks, faster
 * and faster as the boom comes near. Shaking the board does nothing. The rules are
 * pure C, also built on the host by tests/ (-DGAME_RULES_ONLY).
 * UART: pass -> PASS X, TURN O | ERR too soon | ERR game over; sim shake -> SHAKE
 * ignored; the end -> BOOM X, WIN O; sim fuse <ms> -> SIM fuse=<ms> */

#define POTATO_HOLD_MS 2000
#define POTATO_FUSE_MIN_MS 8000
#define POTATO_FUSE_MAX_MS 20000

typedef enum { POTATO_OK, POTATO_ERR_SOON, POTATO_ERR_OVER } potato_result_t;

typedef struct {
    game_round_t r;    /* first member: see game.h; r.turn = the holder */
    uint32_t fuse_ms;  /* left until the boom (never shown) */
    uint32_t held_ms;  /* since the holder got it */
    uint32_t total_ms; /* of the round */
    uint32_t tick_ms;  /* since the fuse last ticked */
    uint8_t loser;     /* the holder at the boom */
} potato_t;

/* fuse_ms: the hidden fuse (POTATO_FUSE_MIN_MS..MAX_MS for a real round) */
void potato_start(potato_t *g, uint8_t first, uint32_t fuse_ms);
potato_result_t potato_pass(potato_t *g);
/* the fuse burns dt_ms; true when it went off (the round is over) */
bool potato_tick(potato_t *g, uint32_t dt_ms);
/* the time between two ticks of the fuse: shorter as less of it is left */
uint32_t potato_tick_interval(uint32_t fuse_left_ms);
/* true when the fuse ticks now (call after potato_tick) */
bool potato_ticks(potato_t *g);

extern const game_def_t HOT_POTATO;
