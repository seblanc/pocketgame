/* Host test of the Connect 4 rules:
 * gcc -DGAME_RULES_ONLY -I source/main tests/test_connect4.c source/main/game/connect4.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game/connect4.h"

static c4_result_t drop_all(c4_t *g, const int *cols, int n)
{
    c4_result_t r = C4_OK;
    for (int i = 0; i < n && r == C4_OK && g->r.status == GAME_PLAYING; i++) {
        r = c4_drop(g, cols[i], NULL);
    }
    return r;
}

int main(void)
{
    c4_t g;
    char b[C4_CELLS + 1];

    /* the acceptance test's sequences */
    c4_start(&g, CELL_X);
    c4_board_str(&g, b);
    assert(strlen(b) == 42 && strspn(b, ".") == 42);
    assert(c4_drop(&g, 7, NULL) == C4_ERR_RANGE && c4_drop(&g, -1, NULL) == C4_ERR_RANGE);
    assert(drop_all(&g, (int[]){0, 0, 0, 0, 0, 0}, 6) == C4_OK && g.r.status == GAME_PLAYING);
    assert(c4_drop(&g, 0, NULL) == C4_ERR_FULL && g.r.turn == CELL_X);
    assert(drop_all(&g, (int[]){1, 2, 1, 2, 1, 2, 1}, 7) == C4_OK);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_X && g.win_n == 4); /* vertical */
    assert(c4_drop(&g, 3, NULL) == C4_ERR_OVER);

    c4_start(&g, CELL_O);
    assert(drop_all(&g, (int[]){0, 0, 1, 1, 2, 2, 3}, 7) == C4_OK);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_O && g.r.moves == 7); /* horizontal */

    c4_start(&g, CELL_X);
    assert(drop_all(&g, (int[]){0, 1, 1, 2, 2, 3, 2, 3, 3, 6, 3}, 11) == C4_OK);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_X && g.r.moves == 11); /* diagonal / */

    c4_start(&g, CELL_O);
    for (int i = 0; i < 6; i++) {
        assert(drop_all(&g, (int[]){0, 2, 1, 3, 4, 6, 5}, 7) == C4_OK);
    }
    assert(g.r.status == GAME_DRAW && g.r.moves == 42);

    /* diagonal \ and the board string (top row first) */
    c4_start(&g, CELL_X);
    assert(drop_all(&g, (int[]){3, 2, 2, 1, 1, 0, 1, 0, 0, 6, 0}, 11) == C4_OK);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_X);
    c4_board_str(&g, b);
    assert(b[c4_str_index(0, 3)] == 'X' && b[41 - 3] == 'X');
    assert(b[c4_str_index(3, 0)] == 'X' && b[2 * 7] == 'X');

    puts("connect 4 tests passed");
    return 0;
}
