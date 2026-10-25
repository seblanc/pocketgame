#pragma once
#include <stdint.h>
#include "esp_err.h"

#define SCREEN_W 240
#define SCREEN_H 240

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* Starts the ST7789 panel, clears it and switches the backlight on. */
esp_err_t display_init(void);
/* Sends the frame buffer to the panel; drawing goes to the buffer only. */
void display_flush(void);

/* Frame-buffer drawing. Lines, arcs and discs are anti-aliased; angles are in
 * degrees, 0 = right, 90 = down (screen coordinates). */
void gfx_clear(uint16_t c); /* also resets the clip */
/* limits all drawing to rows y0..y1-1 (scrolling lists) */
void gfx_clip_y(int y0, int y1);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_rrect(int x, int y, int w, int h, int r, uint16_t c); /* 1 px rounded outline */
void gfx_line(float x0, float y0, float x1, float y1, float width, uint16_t c);
void gfx_arc(float cx, float cy, float r, float width, float a0, float a1, uint16_t c);
void gfx_ring(float cx, float cy, float r, float width, uint16_t c);
void gfx_disc(float cx, float cy, float r, uint16_t c);
/* 5x7 font, upper case only (lower case is drawn as upper case) */
int gfx_text_w(const char *s, int scale);
void gfx_text(int x, int y, const char *s, int scale, uint16_t c);
void gfx_text_mid(int cx, int y, const char *s, int scale, uint16_t c);
void gfx_text_right(int x1, int y, const char *s, int scale, uint16_t c);
