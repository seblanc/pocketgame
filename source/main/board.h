#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pin maps of known ESP32-S3 boards with a 1.54" ST7789 240x240 screen. The seller
 * publishes no pinout, so boot probes each map and keeps the one whose display
 * answers its ID read; the touch controller is searched on I2C separately. */

#define NO_PIN (-1)

typedef struct {
    const char *name;
    int lcd_mosi, lcd_sclk, lcd_cs, lcd_dc, lcd_rst, lcd_bl, bl_on;
    int lcd_w, lcd_h, gap_x, gap_y;
    bool invert;
    int tp_sda, tp_scl, tp_rst;
    int i2s_bclk, i2s_ws, i2s_dout, amp_en; /* I2S class-D amplifier (MAX98357 style) */
    int buzzer;
    int buttons[3];
} board_profile_t;

typedef enum { TOUCH_NONE, TOUCH_CST816, TOUCH_FT6X36, TOUCH_GT911 } touch_kind_t;

typedef struct {
    touch_kind_t kind;
    uint8_t addr;
    int sda, scl, rst;
} touch_info_t;

typedef struct {
    const board_profile_t *profile;
    bool lcd_detected; /* display answered RDDID on these pins */
    uint32_t lcd_id;
    touch_info_t touch;
} board_t;

/* Probes displays and I2C buses, printing what it finds; fills *out. */
void board_detect(board_t *out);
const char *touch_kind_name(touch_kind_t k);
