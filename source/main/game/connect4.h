#pragma once
#include "game.h"

/* Connect 4: 7 columns x 6 rows, a dropped disc falls to the lowest free cell; four
 * in a row (horizontal, vertical or diagonal) wins, a full board without one is a
 * draw. The rules are pure C, also built on the host by tests/ (-DGAME_RULES_ONLY).
 * UART: drop <0-6> [<0-6> ...] (one drop per number, stops at the first error or the
 * end of the round) -> DROP X col=3 row=0 src=uart, BOARD <42 cells, top row first>,
 * TURN O | WIN X cells=a,b,c,d (indexes into BOARD) | DRAW, ERR range | ERR full |
 * ERR game over */

#define C4_COLS 7
#define C4_ROWS 6
#define C4_CELLS (C4_COLS * C4_ROWS)

typedef enum { C4_OK, C4_ERR_RANGE, C4_ERR_FULL, C4_ERR_OVER } c4_result_t;

typedef struct {
    game_round_t r;          /* first member: see game.h */
    uint8_t cells[C4_CELLS]; /* CELL_EMPTY / CELL_X / CELL_O, [row * C4_COLS + col], row 0 = bottom */
    uint8_t win[C4_COLS];    /* the winning run (4 or more cells), when r.status == GAME_WIN */
    uint8_t win_n;
} c4_t;

void c4_start(c4_t *g, uint8_t first);
/* row (may be NULL) gets the row the disc landed in */
c4_result_t c4_drop(c4_t *g, int col, int *row);
/* 42 chars, top row first, '.' for empty; out must hold C4_CELLS + 1 bytes */
void c4_board_str(const c4_t *g, char *out);
/* index into c4_board_str() of the cell at row, col */
int c4_str_index(int row, int col);

extern const game_def_t CONNECT4;
