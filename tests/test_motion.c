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
    assert(tilt_for(&t, 1000, 0, 3000) == TILT_EV_NONE);
    tilt_cell(&t, &c, &r);
    assert(c == 7 && r == 0);
    assert(tilt_for(&t, 0, 1000, 3000) == TILT_EV_FINISH);
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
    assert(!steady_step(&s, 0, 0, 5000));
    assert(steady_step(&s, 1000, 0, 500));
    assert(s.time[CELL_X] >= 5000 && s.r.turn == CELL_O);
    steady_go(&s);
    assert(steady_step(&s, 1000, 0, 500));
    assert(s.time[CELL_O] < 1000);
    assert(s.r.status == GAME_WIN && s.r.winner == CELL_X);
    printf("motion rules ok\n");
    return 0;
}
