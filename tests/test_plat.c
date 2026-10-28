/* Host test of Coin Platformer's rules (the UART acceptance sequence):
 * gcc -DGAME_RULES_ONLY -I source/main tests/test_plat.c source/main/game/plat.c -lm */
#include <assert.h>
#include <stdio.h>
#include "game/plat.h"

uint32_t game_random(void) { return 0; }

static int s_lands_plat, s_lands_ground, s_coins;

/* steps of 20 ms like the board's sim; stops at the end of a run */
static plat_ev_t run_for(plat_t *g, int dir, uint32_t ms, bool autopilot)
{
    if (g->run == PLAT_WAIT) {
        plat_go(g);
    }
    while (ms > 0) {
        uint32_t dt = ms < 20 ? ms : 20;
        ms -= dt;
        bool jump = false;
        if (autopilot) {
            plat_autopilot(g, &dir, &jump);
        }
        if (jump) {
            assert(plat_jump(g));
        }
        for (;;) {
            plat_ev_t ev = plat_step(g, dir, &dt);
            if (ev == PLAT_EV_NONE) {
                break;
            }
            if (ev == PLAT_EV_LAND) {
                if (g->on == PLAT_ON_GROUND) {
                    s_lands_ground++;
                } else {
                    s_lands_plat++;
                }
            } else if (ev == PLAT_EV_COIN) {
                s_coins++;
                printf("coin %d at %.0f,%.0f ms=%u\n", g->last_coin, g->x, g->y, (unsigned)g->ms);
            } else if (ev == PLAT_EV_FINISH) {
                return ev;
            }
        }
    }
    return PLAT_EV_NONE;
}

int main(void)
{
    plat_t g;
    assert(sizeof(plat_t) <= GAME_STATE_MAX);
    plat_start(&g, CELL_X);
    assert(PLAT_NPLATS >= 4 && PLAT_NCOINS >= 5);
    uint32_t id = g.id;

    /* jump from the ground, no second jump in the air, back to the ground */
    plat_go(&g);
    assert(plat_jump(&g));
    assert(!plat_jump(&g));
    s_lands_ground = 0;
    run_for(&g, 0, 1500, false);
    assert(s_lands_ground == 1 && g.on == PLAT_ON_GROUND);
    assert(g.taken == 0);

    /* walking right collects the ground coin only */
    s_coins = 0;
    run_for(&g, 1, 3000, false);
    assert(s_coins == 1 && g.on == PLAT_ON_GROUND);

    /* X's run times out with 1 coin, O gets the same level, every coin back */
    assert(run_for(&g, 0, 61000, false) == PLAT_EV_FINISH);
    assert(g.coins[CELL_X] == 1 && g.r.turn == CELL_O && g.r.status == GAME_PLAYING);
    assert(plat_level_id(&g) == id);

    /* O's autopilot lands on platforms and collects every coin */
    s_coins = s_lands_plat = 0;
    assert(run_for(&g, 0, 65000, true) == PLAT_EV_FINISH);
    printf("autoplay: coins=%d plat landings=%d\n", g.coins[CELL_O], s_lands_plat);
    assert(s_lands_plat >= 1);
    assert(g.coins[CELL_O] == PLAT_NCOINS);
    assert(g.r.status == GAME_WIN && g.r.winner == CELL_O);

    /* two idle runs: a draw */
    plat_start(&g, CELL_O);
    assert(g.r.turn == CELL_O);
    assert(run_for(&g, 0, 61000, false) == PLAT_EV_FINISH && g.coins[CELL_O] == 0 && g.r.turn == CELL_X);
    assert(run_for(&g, 0, 61000, false) == PLAT_EV_FINISH && g.coins[CELL_X] == 0);
    assert(g.r.status == GAME_DRAW);

    puts("plat OK");
    return 0;
}
