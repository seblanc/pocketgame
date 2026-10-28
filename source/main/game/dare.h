#pragma once
#include "game.h"

/* Roue des defis (dare game, UART id "dare"): the players take turns. The mover taps
 * (or presses RIGHT) to spin a wheel of fortune; it clicks past each wedge, slows down
 * over a random 3-5 s and stops, then a sound plays and a dare from the list comes up in
 * large letters. After doing it, a tap or RIGHT hands the board to the other player.
 * The dares come from a deck: none repeats until every one was drawn. The board stays
 * still in the hands: no shaking. The rules are pure C, also built on the host by tests/
 * (-DGAME_RULES_ONLY).
 * UART: dare get <idx> -> DARE idx=n text=..; dare count -> DARES n=..; spin -> SPIN X
 * ms=.. segs=.. | ERR spinning | ERR dare shown; the wheel stops -> DARE X idx=n left=n
 * text=..; pass -> PASS X, TURN O | ERR no dare; sim wait <ms> runs the wheel on a
 * simulated clock; sim cycle -> CYCLE draws=n unique=n total=n (a whole deck, no repeats) */

#define DARE_SPIN_MIN_MS 3000
#define DARE_SPIN_MAX_MS 5000
#define DARE_WEDGES 12 /* on the wheel; each click is one wedge passing the pointer */
#define DARE_SPIN_MIN_WEDGES (3 * DARE_WEDGES) /* at least three turns */
#define DARE_NONE (-1)

/* the dares, upper case without accents (the font has none) */
extern const char *const DARES[];
extern const int DARE_COUNT;

typedef enum { DARE_OK, DARE_ERR_SHOWN, DARE_ERR_SPINNING, DARE_ERR_NONE, DARE_ERR_OVER } dare_result_t;

typedef struct {
    game_round_t r;    /* first member: see game.h; r.turn = the player spinning or daring */
    uint64_t used;     /* dares drawn since the deck was last full, by index */
    uint32_t spin_ms;  /* into the spin */
    uint32_t spin_len; /* of the spin, 0 while the wheel stands still */
    uint16_t segs;     /* wedges the spin turns the wheel by */
    uint16_t clicks;   /* wedges passed so far in this spin */
    uint16_t rest;     /* where the wheel stands (wedges, 0..DARE_WEDGES-1) before the spin */
    int16_t pending;   /* the dare the spin lands on */
    int16_t dare;      /* the dare on screen, DARE_NONE while not shown */
    uint8_t done[3];   /* dares done, by mark */
} dare_t;

/* a new round started by first; the deck carries over (keep_deck) so dares do not repeat */
void dare_start(dare_t *g, uint8_t first, bool keep_deck);
/* the next dare from the deck (refilled when empty); rnd picks among the ones left */
int dare_pick(dare_t *g, uint32_t rnd);
/* dares left in the deck before it refills */
int dare_left(const dare_t *g);
/* spins the wheel for len_ms by segs wedges; the dare (picked with rnd) shows when it stops */
dare_result_t dare_spin(dare_t *g, uint32_t len_ms, uint16_t segs, uint32_t rnd);
/* the wheel turns dt_ms; *clicks: wedges passed in it; true when it stopped (g->dare shown) */
bool dare_spin_step(dare_t *g, uint32_t dt_ms, int *clicks);
/* the wheel's turn in thousandths of a wedge (0 = the rest position of the round's start) */
uint32_t dare_wheel_pos(const dare_t *g);
static inline bool dare_spinning(const dare_t *g) { return g->spin_len > 0; }
/* the dare was done: the other player spins next */
dare_result_t dare_pass(dare_t *g);

extern const game_def_t ROUE_DES_DEFIS;
