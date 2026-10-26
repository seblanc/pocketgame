/* Host test of the motion games' rules (the UART acceptance sequences):
 * gcc -DGAME_RULES_ONLY -I source/main tests/test_motion.c source/main/game/tilt.c
 *     source/main/game/potato.c source/main/game/steady.c -lm */
#include <assert.h>
#include <stdio.h>
#include "game/potato.h"
#include "game/steady.h"
#include "game/tilt.h"

uint32_t game_random(void) { return 0; }

static tilt_ev_t tilt_for(tilt_t *g, int tx, int ty, uint32_t ms)
{
    tilt_go(g);
    for (; ms; ms -= ms < 20 ? ms : 20) {
        tilt_ev_t ev = tilt_step(g, tx, ty, ms < 20 ? ms : 20);
        if (ev != TILT_EV_NONE) {
            return ev;
        }
    }
    return TILT_EV_NONE;
}

int main(void)
{
    int c, r;
    tilt_t t;
    tilt_start(&t, CELL_X);
    assert(tilt_for(&t, -1000, 0, 1000) == TILT_EV_NONE);
    tilt_cell(&t, &c, &r);
    assert(c == 0 && r == 0);
    /* the way through the maze, leg by leg, as in the UART acceptance test */
    static const struct {
        int tx, ty, c, r;
    } LEGS[] = {
        {1000, 0, 3, 0}, {0, 1000, 3, 2}, {1000, 0, 5, 2}, {-1000, 0, 1, 2},
        {0, 1000, 1, 5}, {1000, 0, 5, 5}, {0, 1000, 5, 7},
    };
    for (size_t i = 0; i < sizeof(LEGS) / sizeof(LEGS[0]); i++) {
        assert(tilt_for(&t, LEGS[i].tx, LEGS[i].ty, 3000) == TILT_EV_NONE);
        tilt_cell(&t, &c, &r);
        printf("leg %zu: ball at %d,%d\n", i, c, r);
        assert(c == LEGS[i].c && r == LEGS[i].r);
    }
    assert(tilt_for(&t, 1000, 0, 3000) == TILT_EV_FINISH);
    assert(t.time[CELL_X] > 4000 && t.r.turn == CELL_O);
    assert(tilt_for(&t, 0, 0, 61000) == TILT_EV_TIMEOUT);
    assert(t.r.status == GAME_WIN && t.r.winner == CELL_X);

    potato_t p;
    potato_start(&p, CELL_X, 10000);
    assert(potato_pass(&p) == POTATO_ERR_SOON);
    assert(!potato_tick(&p, 2500));
    assert(potato_pass(&p) == POTATO_OK && p.r.turn == CELL_O);
    assert(potato_skip(&p) == POTATO_OK && p.r.turn == CELL_X);
    assert(potato_skip(&p) == POTATO_OK && p.r.turn == CELL_O);
    assert(!potato_tick(&p, 2500));
    assert(potato_pass(&p) == POTATO_OK && p.r.turn == CELL_X);
    assert(potato_skip(&p) == POTATO_ERR_USED);
    assert(potato_tick(&p, 6000));
    assert(p.loser == CELL_X && p.r.status == GAME_WIN && p.r.winner == CELL_O);

    steady_t s;
    steady_start(&s, CELL_X);
    steady_go(&s);
    assert(steady_step(&s, 0, 0, 5000) == STEADY_EV_NONE);
    assert(s.jumps == 1); /* the circle jumped once */
    assert(steady_step(&s, 1000, 0, 500) == STEADY_EV_OUT);
    assert(s.time[CELL_X] >= 5000 && s.r.turn == CELL_O);
    steady_go(&s);
    assert(steady_step(&s, 1000, 0, 500) == STEADY_EV_OUT);
    assert(s.time[CELL_O] < 1000);
    assert(s.r.status == GAME_WIN && s.r.winner == CELL_X);
    printf("motion rules ok\n");
    return 0;
}
