#pragma once
#include "game.h"

/* Tic-tac-toe. The rules are pure C, also built on the host by tests/ (with
 * -DGAME_RULES_ONLY, which leaves out the screen and console part).
 * UART: play <0-8> -> MOVE X 4 src=uart, BOARD X.O......, TURN O | WIN X line=a,b,c | DRAW,
 * ERR occupied | ERR game over | ERR bad cell */

typedef enum {
    MOVE_OK,
    MOVE_ERR_RANGE,
    MOVE_ERR_OCCUPIED,
    MOVE_ERR_OVER,
} move_result_t;

typedef struct {
    game_round_t r;     /* first member: see game.h */
    uint8_t cells[9];   /* CELL_EMPTY / CELL_X / CELL_O, row-major */
    uint8_t win_line[3];
} ttt_t;

void ttt_new(ttt_t *g); /* X starts */
void ttt_start(ttt_t *g, uint8_t first);
move_result_t ttt_play(ttt_t *g, int cell);
/* "X.O......" style string, out must hold 10 bytes */
void ttt_board_str(const ttt_t *g, char *out);

extern const game_def_t TICTACTOE;
