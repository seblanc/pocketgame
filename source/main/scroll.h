#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Finger-driven vertical list scrolling with momentum and an iOS-style elastic edge:
 * dragging past the top or bottom stretches with growing resistance, and the list
 * springs back once released. Plain C (no ESP-IDF), so it can be tested on the host. */
typedef struct {
    float pos;  /* content offset in px: 0 = top, max = bottom, outside while stretched */
    float vel;  /* px/s, positive = content moves up (towards the bottom of the list) */
    float max;  /* content height - viewport height, >= 0 */
    float view; /* viewport height: scales the rubber band */
    bool held;
    float grab_pos;
    int grab_y, last_y;
    uint32_t last_ms;
} scroller_t;

void scroll_reset(scroller_t *s, int view_h, int content_h);
/* a new viewport height, keeping the offset (the edge spring pulls it back in range) */
void scroll_resize(scroller_t *s, int view_h, int content_h);
/* finger down at y; stops a glide */
void scroll_press(scroller_t *s, int y, uint32_t now_ms);
void scroll_drag(scroller_t *s, int y, uint32_t now_ms);
/* finger up: starts the glide with the finger's speed */
void scroll_release(scroller_t *s, uint32_t now_ms);
/* advances the glide or spring by dt seconds; false once the list is at rest */
bool scroll_step(scroller_t *s, float dt);
bool scroll_moving(const scroller_t *s);
int scroll_offset(const scroller_t *s);
