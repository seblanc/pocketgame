#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pin maps of known ESP32-S3 boards with a 1.54" ST7789 240x240 screen. The seller
 * publishes no pinout, so boot reads the display ID on each map, and when none
 * answers it searches the free GPIOs for the panel's lines (see lcdscan.h). */

#define NO_PIN (-1)
#define MAX_HOLD 3

typedef struct {
    const char *name;
    int lcd_mosi, lcd_sclk, lcd_cs, lcd_dc, lcd_rst, lcd_bl, bl_on;
    /* lines the panel needs held at a level that no single role explains
     * (several chip selects, a power enable...) */
    int n_hold_lo, hold_lo[MAX_HOLD];
    int n_hold_hi, hold_hi[MAX_HOLD];
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
    board_profile_t prof; /* the pin map in use */
    const board_profile_t *profile; /* == &prof */
    bool lcd_detected; /* display answered RDDID on these pins */
    const char *lcd_source; /* "profile", "saved", "search" or "default" */
    uint32_t lcd_id;
    uint64_t idle_low; /* GPIOs that read low with only the internal pull-up at boot */
    uint64_t i2c_pins; /* SDA/SCL of every I2C bus the scan found */
    touch_info_t touch;
} board_t;

/* Probes the display and touch controller, printing what it finds; fills *out. */
void board_detect(board_t *out);
/* Slow scan of every free pin pair for I2C devices (DIAG lines only); skips the
 * pins in busy and the ones the board already uses. */
void board_scan_i2c(board_t *b, uint64_t busy);
/* GPIO mask of the display lines and holds of p. */
uint64_t board_lcd_mask(const board_profile_t *p);
/* Drops the saved display pins so the next boot searches again. */
void board_forget_lcd(void);
const char *touch_kind_name(touch_kind_t k);

#define PIN_BIT(p) ((p) >= 0 ? (1ULL << (p)) : 0ULL)
