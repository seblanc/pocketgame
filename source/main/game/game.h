#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The common game interface. Each game is one module in this folder (its rules,
 * board drawing, touch mapping and UART command) exporting one game_def_t; the app
 * lists them in GAMES[] (main.c), which gives the home menu its tiles. All games are
 * two-player, X (blue) against O (red), taking turns on one screen. The app owns
 * the frame around a game: scores, turn banner, leave confirmation, sounds, and who
 * starts the next round (alternating after every finished round). */

#define CELL_EMPTY 0
#define CELL_X 1
#define CELL_O 2

typedef enum { GAME_PLAYING, GAME_WIN, GAME_DRAW } game_status_t;

/* The first member of every game's state, so the app reads any round the same way. */
typedef struct {
    game_status_t status;
    uint8_t turn;    /* CELL_X or CELL_O: who moves next */
    uint8_t starter; /* CELL_X or CELL_O: who has the first move of this round */
    uint8_t winner;  /* valid when status == GAME_WIN */
    uint8_t moves;
} game_round_t;

/* what a move did, for the app's sound and bookkeeping */
typedef enum {
    GEV_NONE,   /* nothing happened (no sound) */
    GEV_MOVE_X, /* a move, the round goes on */
    GEV_MOVE_O,
    GEV_BAD,    /* rejected: the ERR line is printed, the turn does not change */
    GEV_WIN,
    GEV_DRAW,
    GEV_BOOM,   /* a win for r.winner announced with the boom (Hot Potato) */
    GEV_RUN_END, /* a timed run ended, the other player's run is next (Coin Platformer) */
    GEV_DARE,    /* the wheel stopped: a dare comes up (Roue des defis) */
    GEV_TICK,    /* the fuse ticks, only a sound (Hot Potato, live rounds only) */
    GEV_CLICK,   /* the wheel clicks past a wedge, only a sound (Roue des defis) */
} game_event_t;

/* The motion games (Tilt Duel, Hot Potato, Steady Hand) are driven by time steps: the
 * app feeds the IMU every ~20 ms while such a game is on screen, or the UART "sim"
 * command feeds made-up tilt and shakes on a simulated clock (then the live sensor and
 * the real clock are ignored until the next round). */
typedef struct {
    int tx, ty;     /* screen tilt in mg: tx > 0 right edge lower, ty > 0 bottom edge lower */
    bool shake;     /* a firm shake in this step */
    uint32_t dt_ms; /* time this step covers */
    bool sim;       /* injected over UART: a run waiting for its tap starts by itself */
    int dir;        /* games with steer(), over UART only: -1 walk left, 1 right, 0 by tilt */
    bool jump;      /* a jump in this step (Coin Platformer's autopilot) */
    bool shaking;   /* the board is being shaken all through this step (no game uses it now) */
} motion_t;

/* a random number for the games (hidden timers); the app provides it */
uint32_t game_random(void);

/* room for one game's state in the app's view (each module checks its size) */
#define GAME_STATE_MAX 96

typedef struct {
    const char *id;    /* UART name: "open ttt", "STATUS ... game=ttt" */
    const char *title; /* menu tile */
    const char *verb;  /* the game's UART command: "play" */
    /* empty board, first moves first (round.starter) */
    void (*new_round)(void *st, uint8_t first);
    /* prints BOARD ..., then TURN X | WIN X ... | DRAW */
    void (*print)(const void *st);
    /* applies one move (cell, column...), prints the result or the ERR line */
    game_event_t (*move)(void *st, int m, const char *src);
    /* the verb's argument text from the console; returns the last move's event */
    game_event_t (*command)(void *st, const char *arg);
    /* the move for a tap at x,y on a running round, -1 for none */
    int (*hit)(int x, int y);
    /* true: a touch aims (press, drag) and the move is made on release, at the move
     * hit() gives under the finger; false: the move is made on the press */
    bool drag;
    /* the board, below the turn banner (y >= 36); preview: the move being aimed (drag
     * games, -1 for none), shown in the top strip instead of the banner and scores */
    void (*draw)(const void *st, int preview);
    /* menu tile icon, s x s px at x,y */
    void (*icon)(int x, int y, int s);
    /* motion games only (NULL for the others) */
    /* advances the round by m->dt_ms; prints what happened, GEV_NONE for most steps */
    game_event_t (*step)(void *st, const motion_t *m);
    /* one line of live state after a sim command: BALL ... | BUBBLE ... | POTATO ... */
    void (*report)(const void *st);
    /* "sim <key> <value>" of this game (Hot Potato: fuse); false for an unknown key */
    bool (*sim_set)(void *st, const char *key, long val);
    /* "sim solve" (Tilt Duel): sets m's tilt for the next step toward the goal */
    void (*autopilot)(const void *st, motion_t *m);
    /* optional help screen (a "?" on the game screen, "rules" on the console): its lines
     * (NULL-terminated, up to 12 lines of 36 characters) and the console summary after "RULES " */
    const char *const *rules;
    const char *rules_uart;
    /* optional, all NULL/false for the board and motion games above: */
    /* more console verbs of the game; command() then gets the whole line ("guess 1") */
    const char *const *more_verbs;
    /* the READY line's name for the starter ("READY drawer=X"), NULL for "turn" */
    const char *starter_key;
    /* hit() that depends on the round (Draw & Guess phases); GAME_HIT_PEN: the finger draws */
    int (*hit_state)(const void *st, int x, int y);
    /* the finger on the canvas (GAME_PEN_DOWN | MOVE | UP); true when the drawing changed */
    bool (*pen)(void *st, int phase, int x, int y);
    /* step() is only a clock (no tilt): the app redraws a few times a second, not every step */
    bool timed;
    /* the turn banner's text and the player whose colour it takes (CELL_EMPTY: neutral) */
    void (*banner)(const void *st, char *buf, int n, uint8_t *mark);
    /* a finished round was scored: the scores and who starts the next round */
    void (*finished)(const void *st, int wins_x, int wins_o, uint8_t next);
    /* non-NULL: a steering game (Coin Platformer), walked by tilt, a RIGHT press is a
     * move (a jump) or, once the round is over, the next round; true while a run goes */
    bool (*steer)(const void *st);
    /* true: a RIGHT press is a tap of the running round (Roue des defis: spin or next player) */
    bool right_taps;
} game_def_t;

#define GAME_HIT_PEN (-2)
#define GAME_PEN_DOWN 0
#define GAME_PEN_MOVE 1
#define GAME_PEN_UP 2

static inline const game_round_t *game_round(const void *st) { return (const game_round_t *)st; }

/* CELL_X <-> CELL_O */
static inline uint8_t game_other(uint8_t mark) { return mark == CELL_X ? CELL_O : CELL_X; }

static inline char game_mark_char(uint8_t mark) { return mark == CELL_X ? 'X' : mark == CELL_O ? 'O' : '.'; }

/* the player's name on screen: A (blue, CELL_X) and B (red, CELL_O); the console keeps X/O */
static inline char game_player_char(uint8_t mark) { return mark == CELL_X ? 'A' : mark == CELL_O ? 'B' : '-'; }

/* resets r for a new round started by first */
static inline void game_round_start(game_round_t *r, uint8_t first)
{
    r->status = GAME_PLAYING;
    r->turn = r->starter = first == CELL_O ? CELL_O : CELL_X;
    r->winner = CELL_EMPTY;
    r->moves = 0;
}
