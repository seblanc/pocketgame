/* Host test of the tic-tac-toe rules:
 * gcc -DGAME_RULES_ONLY -I source/main tests/test_game.c source/main/game/tictactoe.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game/tictactoe.h"

static void play_all(ttt_t *g, const int *cells, int n)
{
    for (int i = 0; i < n; i++) {
        assert(ttt_play(g, cells[i]) == MOVE_OK);
    }
}

int main(void)
{
    ttt_t g;
    char b[10];

    ttt_new(&g);
    ttt_board_str(&g, b);
    assert(strcmp(b, ".........") == 0);
    assert(g.r.turn == CELL_X);

    /* same sequence as the acceptance test: occupied cell, top-row win, no move after */
    assert(ttt_play(&g, 0) == MOVE_OK && g.r.turn == CELL_O);
    assert(ttt_play(&g, 0) == MOVE_ERR_OCCUPIED && g.r.turn == CELL_O);
    assert(ttt_play(&g, 3) == MOVE_OK && g.r.turn == CELL_X);
    play_all(&g, (int[]){1, 4, 2}, 3);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_X);
    assert(g.win_line[0] == 0 && g.win_line[1] == 1 && g.win_line[2] == 2);
    assert(ttt_play(&g, 5) == MOVE_ERR_OVER);
    assert(ttt_play(&g, 9) == MOVE_ERR_OVER);

    ttt_new(&g);
    assert(ttt_play(&g, 9) == MOVE_ERR_RANGE);
    assert(ttt_play(&g, -1) == MOVE_ERR_RANGE);
    play_all(&g, (int[]){0, 1, 2, 4, 3, 5, 7, 6, 8}, 9);
    assert(g.r.status == GAME_DRAW);
    ttt_board_str(&g, b);
    assert(strcmp(b, "XOXXOOOXX") == 0);

    /* win on the last move is a win, not a draw */
    ttt_new(&g);
    play_all(&g, (int[]){0, 1, 2, 3, 5, 4, 7, 6}, 8);
    assert(g.r.status == GAME_PLAYING);
    assert(ttt_play(&g, 8) == MOVE_OK);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_X);

    /* O wins on a diagonal */
    ttt_new(&g);
    play_all(&g, (int[]){0, 2, 1, 4, 8, 6}, 6);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_O);

    /* O starts a round: same draw sequence, marks swapped */
    ttt_start(&g, CELL_O);
    assert(g.r.turn == CELL_O && g.r.starter == CELL_O);
    assert(ttt_play(&g, 0) == MOVE_OK && g.r.turn == CELL_X);
    play_all(&g, (int[]){1, 2, 4, 3, 5, 7, 6, 8}, 8);
    assert(g.r.status == GAME_DRAW);
    ttt_board_str(&g, b);
    assert(strcmp(b, "OXOOXXXOO") == 0);
    assert(game_other(CELL_X) == CELL_O && game_other(CELL_O) == CELL_X);

    puts("game tests passed");
    return 0;
}
