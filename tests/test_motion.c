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

/* sim solve: the autopilot until the run ends (or 65 s, as on the board) */
static tilt_ev_t tilt_solve(tilt_t *g)
{
    tilt_go(g);
    for (uint32_t ms = 0; ms < 65000; ms += 20) {
        int tx, ty;
        tilt_autopilot(g, &tx, &ty);
        tilt_ev_t ev = tilt_step(g, tx, ty, 20);
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
    /* every seed gives a full tree maze with turns and dead ends that the autopilot solves */
    int32_t worst = 0;
    for (uint32_t seed = 0; seed < 2000; seed++) {
        tilt_start(&t, CELL_X, seed * 7919u + (seed & 1 ? 0x80000000u : 0));
        tilt_stats_t s;
        tilt_maze_stats(&t.maze, &s);
        assert(s.reach == TILT_COLS * TILT_ROWS && s.turns >= 4 && s.dead_ends >= 4 && t.maze.v[0]);
        assert(tilt_solve(&t) == TILT_EV_FINISH);
        worst = t.time[CELL_X] > worst ? t.time[CELL_X] : worst;
    }
    printf("tilt: 2000 mazes solved, slowest run %ld ms\n", (long)worst);

    /* the UART acceptance sequence on seed 7 */
    tilt_maze_t again;
    tilt_maze_make(&again, 7);
    tilt_start(&t, CELL_X, 7);
    assert(tilt_maze_id(&again) == t.id);
    tilt_maze_make(&again, 8);
    assert(tilt_maze_id(&again) != t.id);
    assert(tilt_for(&t, -1000, 0, 1000) == TILT_EV_NONE);
    tilt_cell(&t, &c, &r);
    assert(c == 0 && r == 0);
    assert(tilt_for(&t, 1000, 0, 3000) == TILT_EV_NONE);
    tilt_cell(&t, &c, &r);
    assert(c < TILT_COLS - 1 && r == 0); /* a wall in the top row */
    assert(tilt_solve(&t) == TILT_EV_FINISH);
    assert(t.r.turn == CELL_O && tilt_maze_id(&t.maze) == t.id);
    assert(tilt_for(&t, 0, 0, 61000) == TILT_EV_TIMEOUT);
    assert(t.r.status == GAME_WIN && t.r.winner == CELL_X);

    potato_t p;
    potato_start(&p, CELL_X, 10000);
    assert(potato_pass(&p) == POTATO_ERR_SOON);
    assert(!potato_tick(&p, 2500));
    assert(potato_pass(&p) == POTATO_OK && p.r.turn == CELL_O);
    assert(potato_ticks(&p) && !potato_ticks(&p)); /* 2.5 s since the start: one tick, then none */
    assert(potato_skip(&p) == POTATO_OK && p.r.turn == CELL_X);
    assert(potato_skip(&p) == POTATO_OK && p.r.turn == CELL_O);
    assert(!potato_tick(&p, 2500));
    assert(potato_pass(&p) == POTATO_OK && p.r.turn == CELL_X);
    assert(potato_skip(&p) == POTATO_ERR_USED);
    /* the ticking speeds up toward the boom */
    assert(potato_tick_interval(15000) > potato_tick_interval(6000));
    assert(potato_tick_interval(6000) > potato_tick_interval(3000));
    assert(potato_tick_interval(3000) > potato_tick_interval(500));
    assert(potato_tick(&p, 5000));
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
