#include "scroll.h"
#include <math.h>
#include <stdlib.h>

#define FRICTION 3.2f  /* glide slows by e^-FRICTION per second */
#define SPRING_K 170.0f /* edge spring stiffness, 1/s^2; critically damped */
#define MAX_VEL 2600.0f
#define REST_VEL 10.0f
#define STALE_MS 70 /* finger still this long before lifting: no glide */
#define SUBSTEP 0.004f

/* iOS rubber band: d px of pull shows as less and less, never more than one viewport */
static float rubber(float d, float view) { return (1.0f - 1.0f / (d * 0.55f / view + 1.0f)) * view; }

/* the pull that rubber() shows as stretch r */
static float unrubber(float r, float view)
{
    float f = r / view;
    f = f > 0.95f ? 0.95f : f;
    return view / 0.55f * (1.0f / (1.0f - f) - 1.0f);
}

void scroll_reset(scroller_t *s, int view_h, int content_h)
{
    s->pos = 0;
    s->vel = 0;
    s->view = view_h > 1 ? (float)view_h : 1.0f;
    s->max = content_h > view_h ? (float)(content_h - view_h) : 0.0f;
    s->held = false;
}

void scroll_press(scroller_t *s, int y, uint32_t now_ms)
{
    s->held = true;
    s->vel = 0;
    /* caught while stretched: continue from the pull that shows this stretch */
    s->grab_pos = s->pos < 0 ? -unrubber(-s->pos, s->view)
                  : s->pos > s->max ? s->max + unrubber(s->pos - s->max, s->view)
                                    : s->pos;
    s->grab_y = s->last_y = y;
    s->last_ms = now_ms;
}

void scroll_drag(scroller_t *s, int y, uint32_t now_ms)
{
    if (!s->held) {
        return;
    }
    /* the unstretched position follows the finger 1:1; the band is applied past either edge */
    float raw = s->grab_pos + (float)(s->grab_y - y);
    if (raw < 0) {
        s->pos = -rubber(-raw, s->view);
    } else if (raw > s->max) {
        s->pos = s->max + rubber(raw - s->max, s->view);
    } else {
        s->pos = raw;
    }

    uint32_t dt = now_ms - s->last_ms;
    if (dt > 0 && y != s->last_y) {
        float v = (float)(s->last_y - y) * 1000.0f / (float)dt;
        s->vel = 0.6f * v + 0.4f * s->vel;
        s->last_y = y;
        s->last_ms = now_ms;
    }
}

void scroll_release(scroller_t *s, uint32_t now_ms)
{
    if (!s->held) {
        return;
    }
    s->held = false;
    if (now_ms - s->last_ms > STALE_MS) {
        s->vel = 0;
    }
    s->vel = s->vel > MAX_VEL ? MAX_VEL : s->vel < -MAX_VEL ? -MAX_VEL : s->vel;
}

bool scroll_moving(const scroller_t *s)
{
    return !s->held && (s->vel != 0 || s->pos < 0 || s->pos > s->max);
}

bool scroll_step(scroller_t *s, float dt)
{
    if (!scroll_moving(s)) {
        return false;
    }
    for (float t = 0; t < dt; t += SUBSTEP) {
        float h = dt - t < SUBSTEP ? dt - t : SUBSTEP;
        if (s->pos < 0 || s->pos > s->max) {
            float edge = s->pos < 0 ? 0 : s->max;
            float acc = -SPRING_K * (s->pos - edge) - 2.0f * sqrtf(SPRING_K) * s->vel;
            s->vel += acc * h;
            float next = s->pos + s->vel * h;
            /* the critically damped spring never crosses back over the edge */
            if ((s->pos < edge) != (next < edge) || (s->pos > edge) != (next > edge)) {
                next = edge;
                s->vel = 0;
            }
            s->pos = next;
        } else {
            s->pos += s->vel * h;
            s->vel *= expf(-FRICTION * h);
        }
    }
    float edge = s->pos < 0 ? 0 : s->pos > s->max ? s->max : s->pos;
    if (fabsf(s->vel) < REST_VEL && fabsf(s->pos - edge) < 0.5f) {
        s->pos = edge;
        s->vel = 0;
        return false;
    }
    return true;
}

int scroll_offset(const scroller_t *s) { return (int)lroundf(s->pos); }
