#include "board.h"
#include <stdio.h>
#include <string.h>
#include "bbi2c.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcdscan.h"
#include "nvs.h"

#define B3(a, b, c) {a, b, c}
#define NVS_NS "board"
#define NVS_KEY_LCD "lcd1" /* bump when board_profile_t changes */

static const board_profile_t PROFILES[] = {
    {
        /* xiaozhi-style 1.54" TFT cube boards (Xingzhi / Zhengchen wiring) */
        .name = "cube154",
        .lcd_mosi = 10, .lcd_sclk = 9, .lcd_cs = 14, .lcd_dc = 8, .lcd_rst = 18, .lcd_bl = 13, .bl_on = 1,
        .lcd_w = 240, .lcd_h = 240, .invert = true,
        .tp_sda = NO_PIN, .tp_scl = NO_PIN, .tp_rst = NO_PIN,
        .i2s_bclk = 15, .i2s_ws = 16, .i2s_dout = 7, .amp_en = NO_PIN,
        .buzzer = NO_PIN,
        .buttons = B3(0, 39, 40),
    },
    {
        /* xiaozhi "bread-compact" LCD wiring, used by many AliExpress AI boards */
        .name = "compact",
        .lcd_mosi = 47, .lcd_sclk = 21, .lcd_cs = 41, .lcd_dc = 40, .lcd_rst = 45, .lcd_bl = 42, .bl_on = 1,
        .lcd_w = 240, .lcd_h = 240, .invert = true,
        .tp_sda = NO_PIN, .tp_scl = NO_PIN, .tp_rst = NO_PIN,
        .i2s_bclk = 15, .i2s_ws = 16, .i2s_dout = 7, .amp_en = NO_PIN,
        .buzzer = NO_PIN,
        .buttons = B3(0, NO_PIN, NO_PIN),
    },
    {
        /* Waveshare ESP32-S3-Touch-LCD-1.69 family wiring (CST816 touch); the
         * board found so far has its touch chip on exactly these pins */
        .name = "ws169",
        .lcd_mosi = 7, .lcd_sclk = 6, .lcd_cs = 5, .lcd_dc = 4, .lcd_rst = 8, .lcd_bl = 15, .bl_on = 1,
        .lcd_w = 240, .lcd_h = 240, .invert = true,
        .tp_sda = 11, .tp_scl = 10, .tp_rst = 13,
        .i2s_bclk = NO_PIN, .i2s_ws = NO_PIN, .i2s_dout = NO_PIN, .amp_en = NO_PIN,
        .buzzer = NO_PIN, /* GPIO42 is an I2C bus (audio codec) on this board */
        .buttons = B3(0, NO_PIN, NO_PIN),
    },
    {
        /* Spotpear 1.54" muma-style wiring */
        .name = "muma154",
        .lcd_mosi = 2, .lcd_sclk = 4, .lcd_cs = 5, .lcd_dc = 47, .lcd_rst = 38, .lcd_bl = 42, .bl_on = 1,
        .lcd_w = 240, .lcd_h = 240, .invert = true,
        .tp_sda = NO_PIN, .tp_scl = NO_PIN, .tp_rst = NO_PIN,
        .i2s_bclk = NO_PIN, .i2s_ws = NO_PIN, .i2s_dout = NO_PIN, .amp_en = NO_PIN,
        .buzzer = NO_PIN,
        .buttons = B3(0, NO_PIN, NO_PIN),
    },
};
#define NPROFILES (sizeof(PROFILES) / sizeof(PROFILES[0]))
#define DEFAULT_PROFILE 0

/* a pin map found by the search: geometry and extras of a plain 1.54" board */
static const board_profile_t SEARCHED = {
    .name = "search",
    .lcd_mosi = NO_PIN, .lcd_sclk = NO_PIN, .lcd_cs = NO_PIN, .lcd_dc = NO_PIN, .lcd_rst = NO_PIN,
    .lcd_bl = NO_PIN, .bl_on = 1,
    .lcd_w = 240, .lcd_h = 240, .invert = true,
    .tp_sda = NO_PIN, .tp_scl = NO_PIN, .tp_rst = NO_PIN,
    .i2s_bclk = NO_PIN, .i2s_ws = NO_PIN, .i2s_dout = NO_PIN, .amp_en = NO_PIN,
    .buzzer = NO_PIN,
    .buttons = B3(0, NO_PIN, NO_PIN),
};

/* GPIOs a scan may touch: not USB (19/20), flash or PSRAM (26..37). */
static const int SCAN_PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                                18, 21, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48};
#define NSCAN (sizeof(SCAN_PINS) / sizeof(SCAN_PINS[0]))

static const uint8_t TOUCH_ADDRS[] = {0x15, 0x38, 0x5D, 0x14};
/* touch chips plus codecs, IMUs, RTCs, PMICs and IO expanders seen on such boards */
static const uint8_t QUICK_ADDRS[] = {0x15, 0x38, 0x5D, 0x14, 0x18, 0x40, 0x6B, 0x6A,
                                      0x51, 0x34, 0x20, 0x68, 0x10, 0x11, 0x55, 0x3C};

const char *touch_kind_name(touch_kind_t k)
{
    switch (k) {
    case TOUCH_CST816: return "CST816";
    case TOUCH_FT6X36: return "FT6x36";
    case TOUCH_GT911: return "GT911";
    default: return "none";
    }
}

static touch_kind_t kind_for_addr(uint8_t a)
{
    switch (a) {
    case 0x15: return TOUCH_CST816;
    case 0x38: return TOUCH_FT6X36;
    case 0x5D:
    case 0x14: return TOUCH_GT911;
    default: return TOUCH_NONE;
    }
}

static void release_pin(int pin)
{
    if (pin >= 0) {
        gpio_reset_pin(pin);
    }
}

static void drive(int pin, int level)
{
    if (pin >= 0) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, level);
    }
}

uint64_t board_lcd_mask(const board_profile_t *p)
{
    uint64_t m = PIN_BIT(p->lcd_mosi) | PIN_BIT(p->lcd_sclk) | PIN_BIT(p->lcd_cs) | PIN_BIT(p->lcd_dc) |
                 PIN_BIT(p->lcd_rst);
    for (int i = 0; i < p->n_hold_lo; i++) {
        m |= PIN_BIT(p->hold_lo[i]);
    }
    for (int i = 0; i < p->n_hold_hi; i++) {
        m |= PIN_BIT(p->hold_hi[i]);
    }
    return m;
}

static uint64_t touch_mask(const touch_info_t *t)
{
    return t->kind == TOUCH_NONE ? 0 : PIN_BIT(t->sda) | PIN_BIT(t->scl) | PIN_BIT(t->rst);
}

static bool load_lcd(board_profile_t *p)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = sizeof(*p);
    bool ok = nvs_get_blob(h, NVS_KEY_LCD, p, &len) == ESP_OK && len == sizeof(*p);
    nvs_close(h);
    p->name = "saved";
    return ok;
}

static void save_lcd(const board_profile_t *p)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_LCD, p, sizeof(*p));
        nvs_commit(h);
        nvs_close(h);
    }
}

void board_forget_lcd(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_LCD);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* Level of every free pin with only the internal pull-up: lines that read low
 * are held down by something on the board (a pull-down, another chip). */
static uint64_t survey_pins(void)
{
    for (size_t i = 0; i < NSCAN; i++) {
        gpio_reset_pin(SCAN_PINS[i]);
        gpio_set_direction(SCAN_PINS[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(SCAN_PINS[i], GPIO_PULLUP_ONLY);
    }
    esp_rom_delay_us(500);
    uint64_t low = 0;
    char buf[160];
    int pos = 0;
    for (size_t i = 0; i < NSCAN; i++) {
        if (!gpio_get_level(SCAN_PINS[i])) {
            low |= PIN_BIT(SCAN_PINS[i]);
            if (pos < (int)sizeof(buf) - 6) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, " %d", SCAN_PINS[i]);
            }
        }
        gpio_reset_pin(SCAN_PINS[i]);
    }
    buf[pos] = '\0';
    printf("DIAG gpio low with pull-up:%s\n", pos ? buf : " none");
    return low;
}

static bool probe_profile_touch(const board_profile_t *p, touch_info_t *t)
{
    if (p->tp_sda < 0 || p->tp_scl < 0) {
        return false;
    }
    if (p->tp_rst >= 0) {
        drive(p->tp_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(p->tp_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    bbi2c_t b;
    bbi2c_init(&b, p->tp_sda, p->tp_scl, 5);
    bbi2c_recover(&b);
    bool found = false;
    for (size_t i = 0; i < sizeof(TOUCH_ADDRS) && !found; i++) {
        if (bbi2c_probe(&b, TOUCH_ADDRS[i])) {
            t->kind = kind_for_addr(TOUCH_ADDRS[i]);
            t->addr = TOUCH_ADDRS[i];
            t->sda = p->tp_sda;
            t->scl = p->tp_scl;
            t->rst = p->tp_rst;
            found = true;
        }
    }
    printf("DIAG touchprobe profile=%s sda=%d scl=%d found=%s\n", p->name, p->tp_sda, p->tp_scl,
           found ? touch_kind_name(t->kind) : "no");
    if (!found) {
        release_pin(p->tp_sda);
        release_pin(p->tp_scl);
        release_pin(p->tp_rst);
    }
    return found;
}

/* Tries every free pin pair as SDA/SCL and reports each bus with devices on it.
 * Lines are only ever pulled low (open drain), never driven high. Takes seconds,
 * so it runs in the background and yields between pairs. */
void board_scan_i2c(board_t *bd, uint64_t busy)
{
    busy |= board_lcd_mask(bd->profile) | touch_mask(&bd->touch) | PIN_BIT(0);
    for (int i = 0; i < 3; i++) {
        busy |= PIN_BIT(bd->profile->buttons[i]);
    }
    int hi[NSCAN];
    int nhi = 0;
    for (size_t i = 0; i < NSCAN; i++) {
        int pin = SCAN_PINS[i];
        if (!(busy & PIN_BIT(pin)) && !(bd->idle_low & PIN_BIT(pin))) {
            bbi2c_setup_pin(pin);
            hi[nhi++] = pin;
        }
    }

    int buses = 0;
    TickType_t t0 = xTaskGetTickCount();
    for (int i = 0; i < nhi; i++) {
        vTaskDelay(1);
        for (int j = 0; j < nhi; j++) {
            if (i == j) {
                continue;
            }
            bbi2c_t b = {.sda = hi[i], .scl = hi[j], .half_us = 3};
            bool any = false;
            for (size_t k = 0; k < sizeof(QUICK_ADDRS) && !any; k++) {
                any = bbi2c_probe(&b, QUICK_ADDRS[k]);
            }
            if (!any) {
                continue;
            }
            char devs[200];
            int dp = 0, count = 0;
            for (uint8_t a = 0x08; a < 0x78; a++) {
                if (bbi2c_probe(&b, a)) {
                    count++;
                    if (dp < (int)sizeof(devs) - 6) {
                        dp += snprintf(devs + dp, sizeof(devs) - dp, " 0x%02x", a);
                    }
                }
            }
            devs[dp] = '\0';
            if (count == 0 || count > 24) {
                continue; /* stuck or bogus line, not a bus */
            }
            buses++;
            bd->i2c_pins |= PIN_BIT(b.sda) | PIN_BIT(b.scl);
            printf("DIAG i2c bus sda=%d scl=%d devices:%s\n", b.sda, b.scl, devs);
        }
    }
    printf("DIAG i2c scan done: %d bus(es) in %lu ms\n", buses,
           (unsigned long)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
    for (int i = 0; i < nhi; i++) {
        gpio_reset_pin(hi[i]);
    }
}

static void print_probe(const char *name, uint32_t id)
{
    bool ok = id != 0 && id != 0xFFFFFF;
    printf("DIAG lcdprobe profile=%s id=0x%06lx%s\n", name, (unsigned long)id, ok ? " match" : "");
}

static bool id_ok(uint32_t id) { return id != 0 && id != 0xFFFFFF; }

void board_detect(board_t *out)
{
    memset(out, 0, sizeof(*out));
    out->profile = &out->prof;
    out->touch.kind = TOUCH_NONE;
    out->touch.sda = out->touch.scl = out->touch.rst = NO_PIN;

    out->idle_low = survey_pins();

    /* 1. touch controller on a known bus: its pins are then off limits for the display */
    const board_profile_t *touch_profile = NULL;
    for (size_t i = 0; i < NPROFILES && !touch_profile; i++) {
        if (probe_profile_touch(&PROFILES[i], &out->touch)) {
            touch_profile = &PROFILES[i];
        }
    }
    uint64_t tmask = touch_mask(&out->touch);

    /* 2. display: pins saved by an earlier search, then the known maps, then a search */
    board_profile_t p;
    if (load_lcd(&p) && !(board_lcd_mask(&p) & tmask)) {
        uint32_t id = lcdscan_read_id(&p);
        print_probe(p.name, id);
        if (id_ok(id)) {
            out->prof = p;
            out->lcd_id = id;
            out->lcd_detected = true;
            out->lcd_source = "saved";
        }
    }
    for (size_t i = 0; i < NPROFILES && !out->lcd_detected; i++) {
        if (board_lcd_mask(&PROFILES[i]) & tmask) {
            printf("DIAG lcdprobe profile=%s skipped (touch pins)\n", PROFILES[i].name);
            continue;
        }
        uint32_t id = lcdscan_read_id(&PROFILES[i]);
        print_probe(PROFILES[i].name, id);
        if (id_ok(id)) {
            out->prof = PROFILES[i];
            out->lcd_id = id;
            out->lcd_detected = true;
            out->lcd_source = "profile";
        }
    }
    if (!out->lcd_detected) {
        int cand[NSCAN];
        bool idle_high[NSCAN];
        int n = 0;
        for (size_t i = 0; i < NSCAN; i++) {
            int pin = SCAN_PINS[i];
            if (pin == 0 || (tmask & PIN_BIT(pin))) {
                continue; /* BOOT button, touch bus */
            }
            cand[n] = pin;
            idle_high[n] = !(out->idle_low & PIN_BIT(pin));
            n++;
        }
        p = SEARCHED;
        uint32_t id = lcdscan_search(cand, idle_high, n, &p);
        if (id_ok(id)) {
            out->prof = p;
            out->lcd_id = id;
            out->lcd_detected = true;
            out->lcd_source = "search";
            save_lcd(&p);
        }
    }
    if (!out->lcd_detected) {
        out->prof = touch_profile ? *touch_profile : PROFILES[DEFAULT_PROFILE];
        out->lcd_source = "default";
    }
    if (out->touch.kind != TOUCH_NONE) {
        out->prof.tp_sda = out->touch.sda;
        out->prof.tp_scl = out->touch.scl;
        out->prof.tp_rst = out->touch.rst;
    }
}
