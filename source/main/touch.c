#include "touch.h"
#include "bbi2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bbi2c_t s_bus;
static touch_info_t s_info;
static bool s_down;

bool touch_init(const touch_info_t *t)
{
    s_info = *t;
    if (t->kind == TOUCH_NONE) {
        return false;
    }
    if (t->rst >= 0) {
        gpio_reset_pin(t->rst);
        gpio_set_direction(t->rst, GPIO_MODE_OUTPUT);
        gpio_set_level(t->rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(t->rst, 1);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    bbi2c_init(&s_bus, t->sda, t->scl, 4);
    bbi2c_recover(&s_bus);
    if (t->kind == TOUCH_CST816) {
        /* keep it awake: a sleeping CST816 does not answer polls */
        bbi2c_write_reg8(&s_bus, t->addr, 0xFE, 0x01);
    }
    return bbi2c_probe(&s_bus, t->addr) || t->kind == TOUCH_CST816;
}

/* finger count and first point; false when nothing could be read */
static bool read_point(int *n, int *x, int *y)
{
    uint8_t d[5] = {0};
    switch (s_info.kind) {
    case TOUCH_CST816:
    case TOUCH_FT6X36:
        /* both: 0x02 points, 0x03..0x06 XH XL YH YL */
        if (!bbi2c_read_reg8(&s_bus, s_info.addr, 0x02, d, 5)) {
            return false;
        }
        *n = d[0] & 0x0F;
        *x = ((d[1] & 0x0F) << 8) | d[2];
        *y = ((d[3] & 0x0F) << 8) | d[4];
        if (*n > 5) {
            *n = 0;
        }
        return true;
    case TOUCH_GT911: {
        uint8_t st;
        if (!bbi2c_read_reg16(&s_bus, s_info.addr, 0x814E, &st, 1)) {
            return false;
        }
        if (!(st & 0x80)) {
            *n = s_down ? 1 : 0; /* no fresh data: keep last state */
            *x = *y = 0;
            return true;
        }
        *n = st & 0x0F;
        if (*n > 0 && !bbi2c_read_reg16(&s_bus, s_info.addr, 0x8150, d, 4)) {
            return false;
        }
        bbi2c_write_reg16(&s_bus, s_info.addr, 0x814E, 0);
        *x = d[0] | (d[1] << 8);
        *y = d[2] | (d[3] << 8);
        return true;
    }
    default:
        return false;
    }
}

bool touch_is_down(void) { return s_down; }

bool touch_poll_press(int *x, int *y)
{
    int n = 0, px = 0, py = 0;
    if (!read_point(&n, &px, &py)) {
        n = 0; /* asleep or no data: treat as released */
    }
    bool press = n > 0 && !s_down;
    s_down = n > 0;
    if (press) {
        *x = px;
        *y = py;
    }
    return press;
}
