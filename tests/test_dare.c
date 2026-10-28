/* Host test of Defi Secousse's rules (the UART acceptance sequence):
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
    dare_start(&g, CELL_X, 2500, true);
    assert(g.r.turn == CELL_X && g.dare == DARE_NONE);
    assert(!dare_pass(&g, 2000)); /* ERR no dare */
    for (int t = 0; t < 1000; t += 20) {
        assert(!dare_shake(&g, 20, game_random()));
    }
    assert(g.shake_ms == 1000);
    bool shown = false;
    for (int t = 0; t < 3000 && !shown; t += 20) {
        shown = dare_shake(&g, 20, game_random());
    }
    assert(shown && g.dare >= 0 && g.shake_ms == 2500);
    assert(!dare_shake(&g, 20, game_random())); /* no shaking while a dare is shown */
    assert(dare_left(&g) == DARE_COUNT - 1);
    assert(dare_pass(&g, 4000));
    assert(g.r.turn == CELL_O && g.dare == DARE_NONE && g.shake_ms == 0 && g.done[CELL_X] == 1);

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

    /* a new round keeps the deck */
    uint64_t used = g.used;
    dare_start(&g, CELL_O, 1500, true);
    assert(g.used == used && g.r.turn == CELL_O);
    printf("dare ok (%d dares)\n", DARE_COUNT);
    return 0;
}
