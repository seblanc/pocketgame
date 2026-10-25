#include "board.h"
#include <stdio.h>
#include <string.h>
#include "bbi2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define B3(a, b, c) {a, b, c}

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
        /* Waveshare ESP32-S3-Touch-LCD-1.69 family wiring (CST816 touch, buzzer) */
        .name = "ws169",
        .lcd_mosi = 7, .lcd_sclk = 6, .lcd_cs = 5, .lcd_dc = 4, .lcd_rst = 8, .lcd_bl = 15, .bl_on = 1,
        .lcd_w = 240, .lcd_h = 280, .gap_y = 20, .invert = true,
        .tp_sda = 11, .tp_scl = 10, .tp_rst = 13,
        .i2s_bclk = NO_PIN, .i2s_ws = NO_PIN, .i2s_dout = NO_PIN, .amp_en = NO_PIN,
        .buzzer = 42,
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

/* Reads the ST7789 display ID (RDDID 04h) over the bidirectional SDA line. */
static uint32_t lcd_read_id(const board_profile_t *p)
{
    spi_bus_config_t bus = {
        .mosi_io_num = p->lcd_mosi,
        .miso_io_num = -1,
        .sclk_io_num = p->lcd_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) {
        return 0;
    }
    spi_device_interface_config_t dev = {
        .mode = 0,
        .clock_speed_hz = 1000000,
        .spics_io_num = p->lcd_cs,
        .queue_size = 1,
        .flags = SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX,
    };
    spi_device_handle_t h;
    if (spi_bus_add_device(SPI2_HOST, &dev, &h) != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        return 0;
    }
    /* an unconnected line then reads as all ones and is rejected */
    gpio_set_pull_mode(p->lcd_mosi, GPIO_PULLUP_ONLY);
    drive(p->lcd_dc, 0);

    if (p->lcd_rst >= 0) {
        drive(p->lcd_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(p->lcd_rst, 1);
    } else {
        spi_transaction_t sw = {.flags = SPI_TRANS_USE_TXDATA, .length = 8, .tx_data = {0x01}};
        spi_device_polling_transmit(h, &sw);
    }
    vTaskDelay(pdMS_TO_TICKS(130));

    uint32_t id = 0xFFFFFF;
    for (int attempt = 0; attempt < 2; attempt++) {
        spi_transaction_t t = {
            .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
            .length = 8,
            .rxlength = 32,
            .tx_data = {0x04},
        };
        if (spi_device_polling_transmit(h, &t) != ESP_OK) {
            id = 0;
            break;
        }
        uint32_t raw = ((uint32_t)t.rx_data[0] << 24) | ((uint32_t)t.rx_data[1] << 16) |
                       ((uint32_t)t.rx_data[2] << 8) | t.rx_data[3];
        /* one dummy clock precedes the three ID bytes */
        uint32_t v = (raw >> 7) & 0xFFFFFF;
        if (attempt > 0 && v != id) {
            id = 0; /* unstable: not a real answer */
            break;
        }
        id = v;
    }

    spi_bus_remove_device(h);
    spi_bus_free(SPI2_HOST);
    release_pin(p->lcd_mosi);
    release_pin(p->lcd_sclk);
    release_pin(p->lcd_cs);
    release_pin(p->lcd_dc);
    release_pin(p->lcd_rst);
    return id;
}

static bool pins_overlap_lcd(const board_profile_t *lcd, const int *pins, int n)
{
    if (!lcd) {
        return false;
    }
    const int lp[] = {lcd->lcd_mosi, lcd->lcd_sclk, lcd->lcd_cs, lcd->lcd_dc, lcd->lcd_rst, lcd->lcd_bl};
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < 6; j++) {
            if (pins[i] >= 0 && pins[i] == lp[j]) {
                return true;
            }
        }
    }
    return false;
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
 * Lines are only ever pulled low (open drain), never driven high. */
static void scan_all_i2c(const board_profile_t *lcd, touch_info_t *t)
{
    int pins[NSCAN];
    int n = 0;
    char idle[160];
    int pos = 0;
    for (size_t i = 0; i < NSCAN; i++) {
        int pin = SCAN_PINS[i];
        if (pins_overlap_lcd(lcd, &pin, 1)) {
            continue;
        }
        bbi2c_setup_pin(pin);
        pins[n++] = pin;
    }
    esp_rom_delay_us(200);
    int hi[NSCAN];
    int nhi = 0;
    for (int i = 0; i < n; i++) {
        int lvl = gpio_get_level(pins[i]);
        if (lvl) {
            hi[nhi++] = pins[i];
        } else if (pos < (int)sizeof(idle) - 6) {
            pos += snprintf(idle + pos, sizeof(idle) - pos, " %d", pins[i]);
        }
    }
    idle[pos] = '\0';
    printf("DIAG gpio low with pull-up:%s\n", pos ? idle : " none");

    int buses = 0;
    TickType_t t0 = xTaskGetTickCount();
    for (int i = 0; i < nhi; i++) {
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
            uint8_t first_touch = 0;
            for (uint8_t a = 0x08; a < 0x78; a++) {
                if (bbi2c_probe(&b, a)) {
                    count++;
                    if (dp < (int)sizeof(devs) - 6) {
                        dp += snprintf(devs + dp, sizeof(devs) - dp, " 0x%02x", a);
                    }
                    if (!first_touch && kind_for_addr(a) != TOUCH_NONE) {
                        first_touch = a;
                    }
                }
            }
            devs[dp] = '\0';
            if (count == 0 || count > 24) {
                continue; /* stuck or bogus line, not a bus */
            }
            buses++;
            printf("DIAG i2c bus sda=%d scl=%d devices:%s\n", b.sda, b.scl, devs);
            if (first_touch && t->kind == TOUCH_NONE) {
                t->kind = kind_for_addr(first_touch);
                t->addr = first_touch;
                t->sda = b.sda;
                t->scl = b.scl;
                t->rst = NO_PIN;
            }
        }
    }
    printf("DIAG i2c scan done: %d bus(es) in %lu ms\n", buses,
           (unsigned long)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
    for (int i = 0; i < n; i++) {
        gpio_reset_pin(pins[i]);
    }
}

void board_detect(board_t *out)
{
    memset(out, 0, sizeof(*out));
    out->touch.kind = TOUCH_NONE;
    out->touch.sda = out->touch.scl = out->touch.rst = NO_PIN;

    /* 1. which pin map has a display answering? */
    const board_profile_t *lcd = NULL;
    for (size_t i = 0; i < NPROFILES; i++) {
        uint32_t id = lcd_read_id(&PROFILES[i]);
        bool ok = id != 0 && id != 0xFFFFFF;
        printf("DIAG lcdprobe profile=%s id=0x%06lx%s\n", PROFILES[i].name, (unsigned long)id,
               ok ? " match" : "");
        if (ok && !lcd) {
            lcd = &PROFILES[i];
            out->lcd_id = id;
        }
    }
    out->lcd_detected = lcd != NULL;

    /* 2. touch controller on a known bus, else anywhere */
    const board_profile_t *touch_profile = NULL;
    for (size_t i = 0; i < NPROFILES && out->touch.kind == TOUCH_NONE; i++) {
        const board_profile_t *p = &PROFILES[i];
        int tp[] = {p->tp_sda, p->tp_scl, p->tp_rst};
        if (pins_overlap_lcd(lcd, tp, 3)) {
            continue;
        }
        if (probe_profile_touch(p, &out->touch)) {
            touch_profile = p;
        }
    }
    scan_all_i2c(lcd, &out->touch);

    if (!lcd) {
        lcd = touch_profile ? touch_profile : &PROFILES[DEFAULT_PROFILE];
    }
    out->profile = lcd;
}
