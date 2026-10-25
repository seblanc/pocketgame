#pragma once
#include <stdint.h>

/* Pure tic-tac-toe rules, no hardware: also built on the host by tests/. */

#define CELL_EMPTY 0
#define CELL_X 1
#define CELL_O 2

typedef enum { GAME_PLAYING, GAME_WIN, GAME_DRAW } game_status_t;

typedef enum {
    MOVE_OK,
    MOVE_ERR_RANGE,
    MOVE_ERR_OCCUPIED,
    MOVE_ERR_OVER,
} move_result_t;

typedef struct {
    uint8_t cells[9];   /* CELL_EMPTY / CELL_X / CELL_O, row-major */
    uint8_t turn;       /* CELL_X or CELL_O: who moves next */
    game_status_t status;
    uint8_t winner;     /* valid when status == GAME_WIN */
    uint8_t win_line[3];
    uint8_t moves;
    uint8_t starter;    /* CELL_X or CELL_O: who has the first move of this round */
} game_t;

void game_new(game_t *g); /* X starts */
void game_start(game_t *g, uint8_t first);
/* CELL_X <-> CELL_O */
uint8_t game_other(uint8_t mark);
move_result_t game_play(game_t *g, int cell);
/* "X.O......" style string, out must hold 10 bytes */
void game_board_str(const game_t *g, char *out);
char game_mark_char(uint8_t mark);
