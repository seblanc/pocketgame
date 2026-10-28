#pragma once
#include "game.h"

/* Defi Secousse (dare game): the players take turns. The mover shakes the board; once
 * the shaking adds up to a hidden random time (1.5-4 s) a sound plays and a dare from
 * the list comes up in large letters. After doing it, a tap or RIGHT hands the board to
 * the other player. The dares come from a deck: none repeats until every one was drawn.
 * The rules are pure C, also built on the host by tests/ (-DGAME_RULES_ONLY).
 * UART: dare get <idx> -> DARE idx=n text=..; dare count -> DARES n=..; pass -> PASS X,
 * TURN O | ERR no dare; sim shake <ms> -> SHAKE X ms=.. need=.. | DARE X idx=n left=n text=..
 * | ERR dare shown; sim cycle -> CYCLE draws=n unique=n total=n (a whole deck, no repeats) */

#define DARE_NEED_MIN_MS 1500
#define DARE_NEED_MAX_MS 4000
#define DARE_NONE (-1)

/* the dares, upper case without accents (the font has none) */
extern const char *const DARES[];
extern const int DARE_COUNT;

typedef struct {
    game_round_t r;    /* first member: see game.h; r.turn = the player shaking or daring */
    uint64_t used;     /* dares drawn since the deck was last full, by index */
    uint32_t shake_ms; /* the mover's shaking so far */
    uint32_t need_ms;  /* the shaking that reveals the dare (never shown) */
    uint32_t anim_ms;  /* the shaking, for the wobble on screen */
    int16_t dare;      /* the dare on screen, DARE_NONE while shaking */
    uint8_t done[3];   /* dares done, by mark */
    bool shaking;      /* shaken in the last step: the console wobbles on screen */
} dare_t;

/* a new round started by first; the deck carries over (keep_deck) so dares do not repeat */
void dare_start(dare_t *g, uint8_t first, uint32_t need_ms, bool keep_deck);
/* the next dare from the deck (refilled when empty); rnd picks among the ones left */
int dare_pick(dare_t *g, uint32_t rnd);
/* dares left in the deck before it refills */
int dare_left(const dare_t *g);
/* dt_ms more shaking; true when it reveals a dare (g->dare), rnd picks it */
bool dare_shake(dare_t *g, uint32_t dt_ms, uint32_t rnd);
/* the dare was done: the other player shakes next for need_ms; false without a dare */
bool dare_pass(dare_t *g, uint32_t need_ms);

extern const game_def_t DEFI_SECOUSSE;
