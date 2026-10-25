#include "touch.h"
#include "board.h"
#include "display.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2cbus.h"

/* The schematic does not name the touch controller, so it is told apart by
 * address. 0x2E (CHSC6x) answered on this board's bus. */
typedef enum { TP_NONE, TP_CST816, TP_FT6X36, TP_CHSC6X } tp_kind_t;

static const struct {
    uint8_t addr;
    tp_kind_t kind;
    const char *name;
} CHIPS[] = {
    {0x2E, TP_CHSC6X, "CHSC6x"},
    {0x15, TP_CST816, "CST816"},
    {0x38, TP_FT6X36, "FT6x36"},
};

static tp_kind_t s_kind = TP_NONE;
static uint8_t s_addr;
static const char *s_name = "none";
static bool s_down;

bool touch_init(void)
{
    gpio_reset_pin(TP_INT);
    gpio_set_direction(TP_INT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TP_INT, GPIO_PULLUP_ONLY);
    gpio_reset_pin(TP_RST);
    gpio_set_direction(TP_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    for (size_t i = 0; i < sizeof(CHIPS) / sizeof(CHIPS[0]); i++) {
        if (i2cbus_probe(CHIPS[i].addr)) {
            s_kind = CHIPS[i].kind;
            s_addr = CHIPS[i].addr;
            s_name = CHIPS[i].name;
            break;
        }
    }
    if (s_kind == TP_CST816) {
        /* keep it awake: a sleeping CST816 does not answer polls */
        i2cbus_write_reg(s_addr, 0xFE, 0x01);
    }
    return s_kind != TP_NONE;
}

const char *touch_chip(void) { return s_name; }
uint8_t touch_addr(void) { return s_addr; }

/* finger count and first point; false when nothing could be read */
static bool read_point(int *n, int *x, int *y)
{
    uint8_t d[5] = {0};
    switch (s_kind) {
    case TP_CHSC6X:
        /* plain 5-byte read: points, -, X, -, Y */
        if (!i2cbus_read(s_addr, d, 5)) {
            return false;
        }
        *n = d[0] == 1 ? 1 : 0;
        *x = d[2];
        *y = d[4];
        return true;
    case TP_CST816:
    case TP_FT6X36:
        /* both: 0x02 points, 0x03..0x06 XH XL YH YL */
        if (!i2cbus_read_reg(s_addr, 0x02, d, 5)) {
            return false;
        }
        *n = d[0] & 0x0F;
        *x = ((d[1] & 0x0F) << 8) | d[2];
        *y = ((d[3] & 0x0F) << 8) | d[4];
        if (*n > 5) {
            *n = 0;
        }
        return true;
    default:
        return false;
    }
}

bool touch_poll_press(int *x, int *y)
{
    int n = 0, px = 0, py = 0;
    if (!read_point(&n, &px, &py)) {
        n = 0; /* asleep or no data: treat as released */
    }
    if (px >= SCREEN_W || py >= SCREEN_H) {
        n = 0; /* noise outside the glass */
    }
    bool press = n > 0 && !s_down;
    s_down = n > 0;
    if (press) {
        *x = px;
        *y = py;
    }
    return press;
}
