#include "backlight.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "nvs.h"

#define NVS_NS "board"
#define MAX_OPTS 64

/* most likely backlight GPIOs on such boards first */
static const int ORDER[] = {15, 38, 45, 46, 48, 21, 47, 40, 39, 14, 16, 17, 18, 12, 9, 1, 2,
                            3,  4,  5,  6,  7,  8,  41, 42, 43, 44, 10, 11, 13};
#define NORDER (sizeof(ORDER) / sizeof(ORDER[0]))

typedef struct {
    int8_t pin, on;
    bool dead;
} opt_t;

static opt_t s_opt[MAX_OPTS];
static int s_n;
static int s_idx = -1; /* -1: the saved option, not one from the list */
static int s_pin = -1, s_on = 1;

static void add(int pin, int on, uint64_t busy)
{
    if (pin < 0 || (busy & PIN_BIT(pin)) || s_n >= MAX_OPTS) {
        return;
    }
    for (int i = 0; i < s_n; i++) {
        if (s_opt[i].pin == pin && s_opt[i].on == on) {
            return;
        }
    }
    s_opt[s_n++] = (opt_t){.pin = (int8_t)pin, .on = (int8_t)on};
}

static void apply(int pin, int on)
{
    if (s_pin >= 0 && s_pin != pin) {
        gpio_reset_pin(s_pin);
    }
    s_pin = pin;
    s_on = on;
    if (pin >= 0) {
        gpio_reset_pin(pin);
        gpio_set_level(pin, on);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    }
}

bool backlight_init(const board_t *b, uint64_t busy)
{
    busy |= PIN_BIT(0) | b->i2c_pins;
    const board_profile_t *p = b->profile;
    /* a known pin map brings its own backlight pin */
    if (p->lcd_bl >= 0 && (!strcmp(b->lcd_source, "profile") || !strcmp(b->lcd_source, "default"))) {
        add(p->lcd_bl, p->bl_on, busy);
    }
    /* a line held low by the board is typically a transistor gate with a pull-down:
     * active high. A line that idles high is more likely active low. */
    for (size_t i = 0; i < NORDER; i++) {
        if (b->idle_low & PIN_BIT(ORDER[i])) {
            add(ORDER[i], 1, busy);
        }
    }
    for (size_t i = 0; i < NORDER; i++) {
        if (!(b->idle_low & PIN_BIT(ORDER[i]))) {
            add(ORDER[i], 0, busy);
        }
    }
    for (size_t i = 0; i < NORDER; i++) {
        if (!(b->idle_low & PIN_BIT(ORDER[i]))) {
            add(ORDER[i], 1, busy);
        }
    }

    nvs_handle_t h;
    int8_t pin = -1, on = 1;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i8(h, "bl_pin", &pin) != ESP_OK || nvs_get_i8(h, "bl_on", &on) != ESP_OK) {
            pin = -1;
        }
        nvs_close(h);
    }
    if (pin >= 0 && !(busy & PIN_BIT(pin))) {
        s_idx = -1;
        apply(pin, on);
        return true;
    }
    s_idx = 0;
    if (s_n > 0) {
        apply(s_opt[0].pin, s_opt[0].on);
    }
    return false;
}

void backlight_exclude(uint64_t pins)
{
    for (int i = 0; i < s_n; i++) {
        if (pins & PIN_BIT(s_opt[i].pin)) {
            s_opt[i].dead = true;
        }
    }
    if (s_idx >= 0 && s_idx < s_n && s_opt[s_idx].dead) {
        backlight_next();
    }
}

void backlight_next(void)
{
    for (int k = 1; k <= s_n; k++) {
        int i = ((s_idx < 0 ? -1 : s_idx) + k) % s_n;
        if (!s_opt[i].dead) {
            s_idx = i;
            apply(s_opt[i].pin, s_opt[i].on);
            return;
        }
    }
}

void backlight_set(int pin, int on)
{
    s_idx = -1;
    apply(pin, on ? 1 : 0);
}

void backlight_confirm(void)
{
    nvs_handle_t h;
    if (s_pin >= 0 && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i8(h, "bl_pin", (int8_t)s_pin);
        nvs_set_i8(h, "bl_on", (int8_t)s_on);
        nvs_commit(h);
        nvs_close(h);
    }
}

void backlight_forget(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "bl_pin");
        nvs_erase_key(h, "bl_on");
        nvs_commit(h);
        nvs_close(h);
    }
}

void backlight_current(int *pin, int *on, int *index, int *count)
{
    *pin = s_pin;
    *on = s_on;
    *index = s_idx;
    *count = s_n;
}
