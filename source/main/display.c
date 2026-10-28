#include "display.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "font5x7.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CHUNK_LINES 40

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_done;
static uint16_t *s_fb; /* SCREEN_W x SCREEN_H, byte-swapped RGB565 for the SPI bus */
static int s_clip_y0 = 0, s_clip_y1 = SCREEN_H; /* drawing only lands on rows y0..y1-1 */

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void push_rows(int panel_y, const uint16_t *src, int lines)
{
    esp_lcd_panel_draw_bitmap(s_panel, 0, panel_y, SCREEN_W, panel_y + lines, src);
    xSemaphoreTake(s_done, pdMS_TO_TICKS(500));
}

void display_flush(void)
{
    if (!s_panel) {
        return;
    }
    for (int y = 0; y < SCREEN_H; y += CHUNK_LINES) {
        push_rows(y, s_fb + y * SCREEN_W, CHUNK_LINES);
    }
}

static inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

/* mixes colour c over the pixel with coverage a (0..1) */
static void blend(int x, int y, uint16_t c, float a)
{
    if (x < 0 || y < s_clip_y0 || x >= SCREEN_W || y >= s_clip_y1 || a <= 0) {
        return;
    }
    uint16_t *p = &s_fb[y * SCREEN_W + x];
    if (a >= 1) {
        *p = swap16(c);
        return;
    }
    uint16_t d = swap16(*p);
    int ia = (int)(a * 256), na = 256 - ia;
    int r = ((c >> 11) * ia + (d >> 11) * na) >> 8;
    int g = (((c >> 5) & 0x3F) * ia + ((d >> 5) & 0x3F) * na) >> 8;
    int b = ((c & 0x1F) * ia + (d & 0x1F) * na) >> 8;
    *p = swap16((uint16_t)((r << 11) | (g << 5) | b));
}

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void gfx_clip_y(int y0, int y1)
{
    s_clip_y0 = y0 < 0 ? 0 : y0;
    s_clip_y1 = y1 > SCREEN_H ? SCREEN_H : y1;
}

void gfx_clear(uint16_t c)
{
    gfx_clip_y(0, SCREEN_H);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, c);
}

void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    if (!s_fb) {
        return;
    }
    uint16_t v = swap16(c);
    for (int j = y < s_clip_y0 ? s_clip_y0 : y; j < y + h && j < s_clip_y1; j++) {
        for (int i = x < 0 ? 0 : x; i < x + w && i < SCREEN_W; i++) {
            s_fb[j * SCREEN_W + i] = v;
        }
    }
}

void gfx_line(float x0, float y0, float x1, float y1, float width, uint16_t c)
{
    if (!s_fb) {
        return;
    }
    float r = width / 2;
    int minx = (int)floorf(fminf(x0, x1) - r - 1), maxx = (int)ceilf(fmaxf(x0, x1) + r + 1);
    int miny = (int)floorf(fminf(y0, y1) - r - 1), maxy = (int)ceilf(fmaxf(y0, y1) + r + 1);
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int y = miny < 0 ? 0 : miny; y <= maxy && y < SCREEN_H; y++) {
        for (int x = minx < 0 ? 0 : minx; x <= maxx && x < SCREEN_W; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float ex = px - t * dx, ey = py - t * dy;
            blend(x, y, c, clamp01(r + 0.5f - sqrtf(ex * ex + ey * ey)));
        }
    }
}

void gfx_arc(float cx, float cy, float r, float width, float a0, float a1, uint16_t c)
{
    if (!s_fb) {
        return;
    }
    float h = width / 2;
    bool full = a1 - a0 >= 360;
    int ext = (int)ceilf(r + h + 1);
    for (int y = (int)cy - ext; y <= (int)cy + ext; y++) {
        for (int x = (int)cx - ext; x <= (int)cx + ext; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float a = clamp01(h + 0.5f - fabsf(sqrtf(dx * dx + dy * dy) - r));
            if (a <= 0) {
                continue;
            }
            if (!full) {
                float ang = atan2f(dy, dx) * (180.0f / (float)M_PI);
                if (ang < 0) {
                    ang += 360;
                }
                if (!((ang >= a0 && ang <= a1) || (ang + 360 >= a0 && ang + 360 <= a1))) {
                    continue;
                }
            }
            blend(x, y, c, a);
        }
    }
}

void gfx_ring(float cx, float cy, float r, float width, uint16_t c) { gfx_arc(cx, cy, r, width, 0, 360, c); }

void gfx_wheel(float cx, float cy, float r, int n, float a0, const uint16_t *cols, int n_cols)
{
    if (!s_fb || n <= 0 || n_cols <= 0) {
        return;
    }
    int ext = (int)ceilf(r + 1);
    float per = 360.0f / n;
    for (int y = (int)cy - ext; y <= (int)cy + ext; y++) {
        for (int x = (int)cx - ext; x <= (int)cx + ext; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float a = clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy));
            if (a <= 0) {
                continue;
            }
            float ang = atan2f(dy, dx) * (180.0f / (float)M_PI) - a0;
            ang -= floorf(ang / 360.0f) * 360.0f;
            int k = (int)(ang / per);
            blend(x, y, cols[(k < n ? k : n - 1) % n_cols], a);
        }
    }
}

void gfx_disc(float cx, float cy, float r, uint16_t c)
{
    if (!s_fb) {
        return;
    }
    int ext = (int)ceilf(r + 1);
    for (int y = (int)cy - ext; y <= (int)cy + ext; y++) {
        for (int x = (int)cx - ext; x <= (int)cx + ext; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            blend(x, y, c, clamp01(r + 0.5f - sqrtf(dx * dx + dy * dy)));
        }
    }
}

void gfx_rrect(int x, int y, int w, int h, int r, uint16_t c)
{
    gfx_rect(x + r, y, w - 2 * r, 1, c);
    gfx_rect(x + r, y + h - 1, w - 2 * r, 1, c);
    gfx_rect(x, y + r, 1, h - 2 * r, c);
    gfx_rect(x + w - 1, y + r, 1, h - 2 * r, c);
    float rr = r - 0.5f;
    gfx_arc(x + r, y + r, rr, 1, 180, 270, c);
    gfx_arc(x + w - r, y + r, rr, 1, 270, 360, c);
    gfx_arc(x + w - r, y + h - r, rr, 1, 0, 90, c);
    gfx_arc(x + r, y + h - r, rr, 1, 90, 180, c);
}

int gfx_text_w(const char *s, int scale) { return *s ? (int)strlen(s) * 6 * scale - scale : 0; }

static char *s_cap;  /* the "text" capture (display task only) */
static size_t s_cap_n, s_cap_len;
static bool s_cap_mute;

void gfx_text_capture(char *buf, size_t n)
{
    s_cap = buf;
    s_cap_n = n;
    s_cap_len = 0;
    s_cap_mute = false;
    if (buf && n) {
        buf[0] = '\0';
    }
}

void gfx_text_mute(bool on) { s_cap_mute = on; }

void gfx_text_note(const char *s)
{
    if (!s_cap || s_cap_mute || !*s || s_cap_len + 1 >= s_cap_n) {
        return;
    }
    if (s_cap_len) {
        strlcpy(s_cap + s_cap_len, " | ", s_cap_n - s_cap_len);
        s_cap_len = strlen(s_cap);
    }
    strlcpy(s_cap + s_cap_len, s, s_cap_n - s_cap_len);
    s_cap_len = strlen(s_cap);
}

void gfx_text(int x, int y, const char *s, int scale, uint16_t c)
{
    gfx_text_note(s);
    for (; *s; s++, x += 6 * scale) {
        char ch = *s;
        if (ch >= 'a' && ch <= 'z') {
            ch -= 'a' - 'A';
        }
        if (ch < ' ' || ch > 'Z') {
            ch = ' ';
        }
        const uint8_t *g = FONT5X7[ch - ' '];
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g[col] & (1 << row)) {
                    gfx_rect(x + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
    }
}

void gfx_text_mid(int cx, int y, const char *s, int scale, uint16_t c)
{
    gfx_text(cx - gfx_text_w(s, scale) / 2, y, s, scale, c);
}

void gfx_text_right(int x1, int y, const char *s, int scale, uint16_t c)
{
    gfx_text(x1 - gfx_text_w(s, scale), y, s, scale, c);
}

/* Power, porch and gamma setup for 1.54" 240x240 ST7789V2 IPS modules (values from the
 * datasheet ranges, as used by the module vendors); the driver's own init only wakes the
 * chip and sets the pixel format. */
static void st7789v2_tune(esp_lcd_panel_io_handle_t io)
{
    static const struct {
        uint8_t cmd, len, data[14];
    } SEQ[] = {
        {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}}, /* PORCTRL */
        {0xB7, 1, {0x35}},                         /* GCTRL: VGH 13.26 V, VGL -10.43 V */
        {0xBB, 1, {0x19}},                         /* VCOMS 0.725 V */
        {0xC0, 1, {0x2C}},                         /* LCMCTRL */
        {0xC2, 1, {0x01}},                         /* VDVVRHEN */
        {0xC3, 1, {0x12}},                         /* VRHS 4.45 V */
        {0xC4, 1, {0x20}},                         /* VDVS 0 V */
        {0xC6, 1, {0x0F}},                         /* FRCTRL2: 60 Hz */
        {0xD0, 2, {0xA4, 0xA1}},                   /* PWCTRL1 */
        {0xE0, 14, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}},
        {0xE1, 14, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}},
    };
    for (size_t i = 0; i < sizeof(SEQ) / sizeof(SEQ[0]); i++) {
        esp_lcd_panel_io_tx_param(io, SEQ[i].cmd, SEQ[i].data, SEQ[i].len);
    }
}

esp_err_t display_init(void)
{
    /* backlight off until the first frame is on the glass */
    gpio_reset_pin(LCD_BL);
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 0);

    s_done = xSemaphoreCreateBinary();
    s_fb = heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_fb) {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SCREEN_W * CHUNK_LINES * sizeof(uint16_t) + 8,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 26 * 1000 * 1000, /* SPI through the GPIO matrix and the FPC: stay well inside spec */
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_trans_done,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        return err;
    }
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    st7789v2_tune(io);
    esp_lcd_panel_invert_color(s_panel, true); /* IPS panels need inverted colours */
    esp_lcd_panel_disp_on_off(s_panel, true);
    vTaskDelay(pdMS_TO_TICKS(20));

    memset(s_fb, 0, SCREEN_W * SCREEN_H * sizeof(uint16_t));
    display_flush();
    gpio_set_level(LCD_BL, 1);
    return ESP_OK;
}
