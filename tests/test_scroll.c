/* Host test of the list scrolling: gcc -I source/main tests/test_scroll.c source/main/scroll.c -lm */
#include <assert.h>
#include <stdio.h>
#include "scroll.h"

/* runs the glide/spring to rest; returns the furthest offset seen */
static float settle(scroller_t *s, float *lowest)
{
    float hi = s->pos, lo = s->pos;
    for (int i = 0; i < 500 && scroll_step(s, 0.02f); i++) {
        hi = s->pos > hi ? s->pos : hi;
        lo = s->pos < lo ? s->pos : lo;
    }
    *lowest = lo;
    return hi;
}

int main(void)
{
    scroller_t s;
    float lo;

    /* drag 1:1 inside the content */
    scroll_reset(&s, 175, 284);
    assert(s.max == 109);
    scroll_press(&s, 150, 0);
    scroll_drag(&s, 110, 100);
    assert(scroll_offset(&s) == 40);
    scroll_drag(&s, 110, 300);
    scroll_release(&s, 400); /* finger rested: no glide */
    assert(!scroll_moving(&s));

    /* pulling past the top stretches less than the finger moved, then springs back */
    scroll_reset(&s, 175, 284);
    scroll_press(&s, 50, 0);
    scroll_drag(&s, 150, 100);
    assert(s.pos < 0 && s.pos > -100);
    scroll_release(&s, 110);
    settle(&s, &lo);
    assert(scroll_offset(&s) == 0 && !scroll_moving(&s));

    /* a fast flick glides, overshoots the bottom a little and settles on it */
    scroll_reset(&s, 175, 284);
    scroll_press(&s, 200, 0);
    for (int t = 1; t <= 4; t++) {
        scroll_drag(&s, 200 - 20 * t, 20 * t);
    }
    scroll_release(&s, 90);
    assert(s.vel > 500);
    float hi = settle(&s, &lo);
    assert(hi > s.max && hi < s.max + 90);
    assert(scroll_offset(&s) == 109);

    /* content shorter than the view: only the elastic bounce */
    scroll_reset(&s, 193, 119);
    assert(s.max == 0);
    scroll_press(&s, 100, 0);
    scroll_drag(&s, 40, 50);
    assert(s.pos > 0 && s.pos < 60);
    scroll_release(&s, 300);
    settle(&s, &lo);
    assert(scroll_offset(&s) == 0);

    puts("scroll ok");
    return 0;
}
