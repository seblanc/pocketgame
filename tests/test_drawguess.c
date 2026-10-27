/* Host test of the Draw & Guess rules (the UART acceptance sequence):
 * gcc -DGAME_RULES_ONLY -I source/main tests/test_drawguess.c source/main/game/drawguess.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game/drawguess.h"

static uint32_t s_rand = 12345;
uint32_t game_random(void) { return s_rand = s_rand * 1103515245u + 12345u; }

static void stroke(dg_t *g)
{
    assert(dg_pen(g, DG_PEN_DOWN, 100, 100) == DG_OK);
    for (int i = 1; i <= 10; i++) {
        dg_pen(g, DG_PEN_MOVE, 100 + 4 * i, 100 + 2 * i);
    }
    assert(dg_pen(g, DG_PEN_UP, 0, 0) == DG_OK);
}

static void check_choices(const dg_t *g, int correct)
{
    assert(g->correct == correct);
    assert(g->choices[correct] == g->word);
    for (int i = 0; i < DG_CHOICES; i++) {
        for (int j = i + 1; j < DG_CHOICES; j++) {
            assert(g->choices[i] != g->choices[j]);
        }
    }
}

int main(void)
{
    /* the word list: 200+ distinct single words, a-z only, short enough for a button */
    assert(DG_WORD_COUNT >= 200);
    for (int i = 0; i < DG_WORD_COUNT; i++) {
        size_t len = strlen(DG_WORDS[i]);
        assert(len >= 2 && len <= 12);
        for (size_t c = 0; c < len; c++) {
            assert(DG_WORDS[i][c] >= 'a' && DG_WORDS[i][c] <= 'z');
        }
        for (int j = i + 1; j < DG_WORD_COUNT; j++) {
            if (strcmp(DG_WORDS[i], DG_WORDS[j]) == 0) {
                printf("duplicate word %s\n", DG_WORDS[i]);
                assert(0);
            }
        }
    }
    printf("%d words\n", DG_WORD_COUNT);

    dg_t g;
    /* round 1: X draws word 3, O guesses the middle button */
    dg_start(&g, CELL_X, 3);
    assert(g.phase == DG_WORD && g.r.starter == CELL_X);
    assert(dg_guess(&g, 0) == DG_ERR_PHASE);
    assert(dg_done(&g, false) == DG_ERR_EMPTY);
    assert(dg_begin(&g) == DG_OK);
    assert(dg_done(&g, false) == DG_ERR_EMPTY);
    g.force_correct = 1;
    stroke(&g);
    assert(g.n_strokes == 1 && g.n_pts == 11);
    /* jitter below 3 px adds nothing */
    assert(dg_pen(&g, DG_PEN_DOWN, 10, 60) == DG_OK);
    assert(dg_pen(&g, DG_PEN_MOVE, 11, 61) == DG_ERR_RANGE);
    dg_pen(&g, DG_PEN_UP, 0, 0);
    assert(g.n_strokes == 2 && g.n_pts == 12);
    assert(dg_undo(&g) == DG_OK && g.n_strokes == 1 && g.n_pts == 11);
    assert(dg_done(&g, false) == DG_OK && g.phase == DG_HANDOVER && g.r.turn == CELL_O);
    check_choices(&g, 1);
    assert(dg_guess(&g, 1) == DG_ERR_PHASE);
    assert(dg_reveal(&g) == DG_OK);
    assert(dg_guess(&g, 3) == DG_ERR_RANGE);
    assert(dg_guess(&g, 1) == DG_OK);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_O);

    /* round 2: O draws, the 60 s run out, X picks a decoy */
    dg_start(&g, CELL_O, 0);
    g.force_correct = 2;
    assert(dg_begin(&g) == DG_OK);
    stroke(&g);
    assert(!dg_tick(&g, 59000));
    assert(dg_tick(&g, 2000) && g.phase == DG_HANDOVER && g.r.turn == CELL_X);
    check_choices(&g, 2);
    assert(dg_reveal(&g) == DG_OK);
    assert(dg_guess(&g, 0) == DG_OK);
    assert(g.r.status == GAME_DRAW);

    /* clear and random choices stay distinct over many rounds */
    for (int n = 0; n < 500; n++) {
        dg_start(&g, CELL_X, n % DG_WORD_COUNT);
        dg_begin(&g);
        stroke(&g);
        assert(dg_clear(&g) == DG_OK && g.n_pts == 0);
        stroke(&g);
        assert(dg_done(&g, false) == DG_OK);
        check_choices(&g, g.correct);
    }
    printf("drawguess rules OK\n");
    return 0;
}
