/* Host test of Roue des defis' rules (the UART acceptance sequence):
 * gcc -DGAME_RULES_ONLY -I source/main tests/test_dare.c source/main/game/dare.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game/dare.h"

static uint32_t s_seed = 12345;
uint32_t game_random(void) { return s_seed = s_seed * 1103515245u + 12345u; }

int main(void)
{
    assert(DARE_COUNT >= 47 && DARE_COUNT <= 64);
    assert(strcmp(DARES[0], "SAUTER 10 FOIS") == 0);
    assert(strcmp(DARES[3], "FAIRE 3 POMPES") == 0);
    assert(strcmp(DARES[5], "DEBARRASSER LA TABLE AU PROCHAIN REPAS") == 0);
    assert(strcmp(DARES[6], "REPETE : VISE CE CHIC SLIP CHARLIE") == 0);
    for (int i = 0; i < DARE_COUNT; i++) {
        for (const char *p = DARES[i]; *p; p++) {
            assert(*p >= ' ' && *p <= 'Z'); /* the font: upper case ASCII only */
        }
        for (int j = 0; j < i; j++) {
            assert(strcmp(DARES[i], DARES[j]) != 0);
        }
    }

    dare_t g;
    memset(&g, 0, sizeof(g));
    dare_start(&g, CELL_X, true);
    assert(g.r.turn == CELL_X && g.dare == DARE_NONE && !dare_spinning(&g));
    assert(dare_pass(&g) == DARE_ERR_NONE); /* ERR no dare */
    assert(dare_spin(&g, 4000, 40, game_random()) == DARE_OK);
    assert(dare_spinning(&g) && dare_left(&g) == DARE_COUNT - 1);
    assert(dare_spin(&g, 4000, 40, game_random()) == DARE_ERR_SPINNING);
    assert(dare_pass(&g) == DARE_ERR_NONE); /* no dare while the wheel turns */

    /* the wheel clicks once per wedge, fast at first, then slower */
    int clicks, total = 0, first_s = 0;
    bool stopped = false;
    uint32_t last_pos = dare_wheel_pos(&g);
    for (int t = 0; t < 4000 && !stopped; t += 20) {
        stopped = dare_spin_step(&g, 20, &clicks);
        total += clicks;
        if (t < 1000) {
            first_s += clicks;
        }
        if (!stopped) {
            assert(dare_wheel_pos(&g) >= last_pos);
            last_pos = dare_wheel_pos(&g);
        }
    }
    assert(stopped && total == 40 && first_s >= 16); /* over 40 % of the clicks in the first quarter */
    assert(g.dare >= 0 && !dare_spinning(&g) && g.rest == 40 % DARE_WEDGES);
    assert(dare_wheel_pos(&g) == (uint32_t)g.rest * 1000);
    assert(dare_spin(&g, 4000, 40, game_random()) == DARE_ERR_SHOWN);
    assert(dare_pass(&g) == DARE_OK);
    assert(g.r.turn == CELL_O && g.dare == DARE_NONE && g.done[CELL_X] == 1);

    /* O spins in uneven steps and lands too */
    assert(dare_spin(&g, 3000, 37, game_random()) == DARE_OK);
    total = 0;
    stopped = false;
    for (int t = 0; t < 3500 && !stopped; t += 70) {
        stopped = dare_spin_step(&g, 70, &clicks);
        total += clicks;
    }
    assert(stopped && total == 37 && g.rest == (40 + 37) % DARE_WEDGES && g.dare >= 0);
    assert(dare_left(&g) == DARE_COUNT - 2);

    /* a whole deck never repeats, then it refills */
    dare_t c = g;
    c.used = 0;
    uint64_t seen = 0;
    for (int i = 0; i < DARE_COUNT; i++) {
        int k = dare_pick(&c, game_random());
        assert(!(seen >> k & 1));
        seen |= 1ull << k;
    }
    assert(dare_left(&c) == 0);
    dare_pick(&c, game_random());
    assert(dare_left(&c) == DARE_COUNT - 1);

    /* a new round keeps the deck and the wheel's position */
    uint64_t used = g.used;
    uint16_t rest = g.rest;
    dare_start(&g, CELL_O, true);
    assert(g.used == used && g.rest == rest && g.r.turn == CELL_O && g.dare == DARE_NONE);
    printf("dare ok (%d dares)\n", DARE_COUNT);
    return 0;
}
