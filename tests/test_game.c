/* Host test of the game rules: gcc -I source/main tests/test_game.c source/main/game.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

static void play_all(game_t *g, const int *cells, int n)
{
    for (int i = 0; i < n; i++) {
        assert(game_play(g, cells[i]) == MOVE_OK);
    }
}

int main(void)
{
    game_t g;
    char b[10];

    game_new(&g);
    game_board_str(&g, b);
    assert(strcmp(b, ".........") == 0);
    assert(g.turn == CELL_X);

    /* same sequence as the acceptance test: occupied cell, top-row win, no move after */
    assert(game_play(&g, 0) == MOVE_OK && g.turn == CELL_O);
    assert(game_play(&g, 0) == MOVE_ERR_OCCUPIED && g.turn == CELL_O);
    assert(game_play(&g, 3) == MOVE_OK && g.turn == CELL_X);
    play_all(&g, (int[]){1, 4, 2}, 3);
    assert(g.status == GAME_WIN && g.winner == CELL_X);
    assert(g.win_line[0] == 0 && g.win_line[1] == 1 && g.win_line[2] == 2);
    assert(game_play(&g, 5) == MOVE_ERR_OVER);
    assert(game_play(&g, 9) == MOVE_ERR_OVER);

    game_new(&g);
    assert(game_play(&g, 9) == MOVE_ERR_RANGE);
    assert(game_play(&g, -1) == MOVE_ERR_RANGE);
    play_all(&g, (int[]){0, 1, 2, 4, 3, 5, 7, 6, 8}, 9);
    assert(g.status == GAME_DRAW);
    game_board_str(&g, b);
    assert(strcmp(b, "XOXXOOOXX") == 0);

    /* win on the last move is a win, not a draw */
    game_new(&g);
    play_all(&g, (int[]){0, 1, 2, 3, 5, 4, 7, 6}, 8);
    assert(g.status == GAME_PLAYING);
    assert(game_play(&g, 8) == MOVE_OK);
    assert(g.status == GAME_WIN && g.winner == CELL_X);

    /* O wins on a diagonal */
    game_new(&g);
    play_all(&g, (int[]){0, 2, 1, 4, 8, 6}, 6);
    assert(g.status == GAME_WIN && g.winner == CELL_O);

    puts("game tests passed");
    return 0;
}
