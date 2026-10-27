#pragma once
#include "game.h"

/* Draw & Guess (Pictionary for two): the drawer (r.starter) sees a secret word from a
 * built-in list and taps to start, draws it with a finger within 60 s (UNDO, CLEAR,
 * DONE), then a hand-over screen asks to pass the board; the guesser taps to see the
 * drawing and picks one of 3 words (the secret word and 2 distinct decoys, shuffled).
 * A right guess is a win for the guesser (a point), a wrong one ends the round as a
 * draw; either way the other player draws the next round. The rules are pure C, also
 * built on the host by tests/ (-DGAME_RULES_ONLY).
 * UART: done | reveal | guess <0-2> | start | undo | clear (also "dg <sub>"):
 * READY drawer=X, SCREEN handover, CHOICES a,b,c correct=n word=w, RESULT correct|wrong,
 * SCORE X=n O=n, TURN drawer=O, TIME up drawer=X, ERR not guessing | ERR empty;
 * sim word <idx> -> WORD idx=n word=w, sim correct <0-2> -> SIM correct=n,
 * sim stroke -> STROKE n=k pts=m */

#define DG_LIMIT_MS 60000
#define DG_CHOICES 3
#define DG_MAX_STROKES 80
#define DG_MAX_PTS 1400

/* the canvas while drawing, in screen pixels */
#define DG_CANVAS_X 0
#define DG_CANVAS_Y 38
#define DG_CANVAS_W 240
#define DG_CANVAS_H 146

typedef enum { DG_WORD, DG_DRAW, DG_HANDOVER, DG_GUESS, DG_RESULT } dg_phase_t;

typedef enum { DG_OK, DG_ERR_PHASE, DG_ERR_EMPTY, DG_ERR_RANGE, DG_ERR_FULL } dg_result_t;

typedef struct {
    game_round_t r;     /* first member: see game.h; r.starter = the drawer, r.turn = who holds the board */
    uint8_t phase;      /* dg_phase_t */
    uint8_t word;       /* index into DG_WORDS */
    int8_t force_correct; /* sim: the button of the secret word, -1 random */
    uint8_t choices[DG_CHOICES];
    uint8_t correct;    /* the button of the secret word */
    int8_t picked;      /* the guesser's button, -1 none yet */
    bool pen_down;      /* a stroke is being drawn */
    uint16_t n_strokes; /* finished and current strokes */
    uint16_t n_pts;     /* points of all strokes */
    uint32_t drawn_ms;  /* drawing time used */
} dg_t;

extern const char *const DG_WORDS[];
extern const int DG_WORD_COUNT;

/* The stroke points: too many for a game slot, so they live in this module (one canvas,
 * there is one board). Stroke s is points stroke_start[s] .. stroke_start[s+1]-1 (the last
 * one ends at n_pts). */
extern uint8_t dg_pts[DG_MAX_PTS][2];
extern uint16_t dg_stroke_start[DG_MAX_STROKES];

/* word: the secret word's index (random for a real round) */
void dg_start(dg_t *g, uint8_t first, int word);
/* the drawer's tap on the word screen */
dg_result_t dg_begin(dg_t *g);
/* a finger on the canvas: down starts a stroke (and the drawing), move extends it, up ends it */
dg_result_t dg_pen(dg_t *g, int phase, int x, int y);
dg_result_t dg_undo(dg_t *g);
dg_result_t dg_clear(dg_t *g);
/* DONE (or the time limit when force): to the hand-over screen with the 3 choices drawn */
dg_result_t dg_done(dg_t *g, bool force);
/* the guesser's tap on the hand-over screen */
dg_result_t dg_reveal(dg_t *g);
/* the guesser picks button i: the round ends, r.winner = guesser when right */
dg_result_t dg_guess(dg_t *g, int i);
/* the drawing clock runs dt_ms; true when the 60 s are up (then dg_done was applied) */
bool dg_tick(dg_t *g, uint32_t dt_ms);

#define DG_PEN_DOWN GAME_PEN_DOWN
#define DG_PEN_MOVE GAME_PEN_MOVE
#define DG_PEN_UP GAME_PEN_UP

extern const game_def_t DRAW_GUESS;
