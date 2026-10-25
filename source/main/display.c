#include "display.h"
#include <math.h>
#include <stdbool.h>
#include "board.h"
#include <stdio.h>
#include <string.h>
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
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COL_BG RGB(16, 20, 40)
#define COL_GRID RGB(150, 160, 190)
#define COL_X RGB(255, 80, 80)
#define COL_O RGB(60, 200, 255)
#define COL_WIN RGB(255, 220, 40)
#define COL_TEXT RGB(235, 235, 235)
#define COL_DIM RGB(120, 125, 150)

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_done;
static uint16_t *s_fb; /* SCREEN_W x SCREEN_H, byte-swapped RGB565 for the SPI bus */

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

static void fb_flush(void)
{
    for (int y = 0; y < SCREEN_H; y += CHUNK_LINES) {
        push_rows(y, s_fb + y * SCREEN_W, CHUNK_LINES);
    }
}

static inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static void fill_rect(int x, int y, int w, int h, uint16_t c)
{
    uint16_t v = swap16(c);
    for (int j = y < 0 ? 0 : y; j < y + h && j < SCREEN_H; j++) {
        for (int i = x < 0 ? 0 : x; i < x + w && i < SCREEN_W; i++) {
            s_fb[j * SCREEN_W + i] = v;
        }
    }
}

static void thick_line(float x0, float y0, float x1, float y1, float width, uint16_t c)
{
    uint16_t v = swap16(c);
    float r = width / 2;
    int minx = (int)floorf(fminf(x0, x1) - r), maxx = (int)ceilf(fmaxf(x0, x1) + r);
    int miny = (int)floorf(fminf(y0, y1) - r), maxy = (int)ceilf(fmaxf(y0, y1) + r);
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int y = miny < 0 ? 0 : miny; y <= maxy && y < SCREEN_H; y++) {
        for (int x = minx < 0 ? 0 : minx; x <= maxx && x < SCREEN_W; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float ex = px - t * dx, ey = py - t * dy;
            if (ex * ex + ey * ey <= r * r) {
                s_fb[y * SCREEN_W + x] = v;
            }
        }
    }
}

static void ring(float cx, float cy, float r_out, float r_in, uint16_t c)
{
    uint16_t v = swap16(c);
    for (int y = (int)(cy - r_out) - 1; y <= (int)(cy + r_out) + 1; y++) {
        for (int x = (int)(cx - r_out) - 1; x <= (int)(cx + r_out) + 1; x++) {
            if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) {
                continue;
            }
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 <= r_out * r_out && d2 >= r_in * r_in) {
                s_fb[y * SCREEN_W + x] = v;
            }
        }
    }
}

static int text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

static void draw_text(int x, int y, const char *s, int scale, uint16_t c)
{
    for (; *s; s++, x += 6 * scale) {
        char ch = *s;
        if (ch < ' ' || ch > 'Z') {
            ch = ' ';
        }
        const uint8_t *g = FONT5X7[ch - ' '];
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g[col] & (1 << row)) {
                    fill_rect(x + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
    }
}

static void cell_center(int cell, float *cx, float *cy)
{
    *cx = GRID_X0 + (cell % 3) * CELL_PX + CELL_PX / 2.0f;
    *cy = GRID_Y0 + (cell / 3) * CELL_PX + CELL_PX / 2.0f;
}

int display_cell_at(int x, int y)
{
    int col = (x - GRID_X0) / CELL_PX, row = (y - GRID_Y0) / CELL_PX;
    if (x < GRID_X0 || y < GRID_Y0 || col > 2 || row > 2) {
        return -1;
    }
    return row * 3 + col;
}

void display_render(const view_t *v)
{
    if (!s_panel) {
        return;
    }
    const game_t *g = &v->game;
    fill_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);

    /* status bar: scores at the sides, turn or result in the middle */
    char buf[16];
    snprintf(buf, sizeof(buf), "X%d", v->wins_x > 99 ? 99 : v->wins_x);
    draw_text(4, 12, buf, 2, COL_X);
    snprintf(buf, sizeof(buf), "O%d", v->wins_o > 99 ? 99 : v->wins_o);
    draw_text(SCREEN_W - 4 - text_width(buf, 2), 12, buf, 2, COL_O);

    uint16_t tc = COL_TEXT;
    if (g->status == GAME_WIN) {
        snprintf(buf, sizeof(buf), "%c WINS!", game_mark_char(g->winner));
        tc = g->winner == CELL_X ? COL_X : COL_O;
    } else if (g->status == GAME_DRAW) {
        snprintf(buf, sizeof(buf), "DRAW!");
    } else {
        snprintf(buf, sizeof(buf), "%c TURN", game_mark_char(g->turn));
        tc = g->turn == CELL_X ? COL_X : COL_O;
    }
    draw_text((SCREEN_W - text_width(buf, 3)) / 2, 8, buf, 3, tc);

    /* grid */
    int grid = 3 * CELL_PX;
    for (int i = 1; i < 3; i++) {
        fill_rect(GRID_X0 + i * CELL_PX - 2, GRID_Y0 + 4, 4, grid - 8, COL_GRID);
        fill_rect(GRID_X0 + 4, GRID_Y0 + i * CELL_PX - 2, grid - 8, 4, COL_GRID);
    }

    bool over = g->status != GAME_PLAYING;
    for (int c = 0; c < 9; c++) {
        float cx, cy;
        cell_center(c, &cx, &cy);
        bool on_line = g->status == GAME_WIN &&
                       (c == g->win_line[0] || c == g->win_line[1] || c == g->win_line[2]);
        if (g->cells[c] == CELL_X) {
            uint16_t col = over && !on_line ? COL_DIM : COL_X;
            thick_line(cx - 19, cy - 19, cx + 19, cy + 19, 9, col);
            thick_line(cx - 19, cy + 19, cx + 19, cy - 19, 9, col);
        } else if (g->cells[c] == CELL_O) {
            ring(cx, cy, 23, 15, over && !on_line ? COL_DIM : COL_O);
        }
    }

    if (g->status == GAME_WIN) {
        float ax, ay, bx, by;
        cell_center(g->win_line[0], &ax, &ay);
        cell_center(g->win_line[2], &bx, &by);
        float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        float ext = 26 / len;
        thick_line(ax - dx * ext, ay - dy * ext, bx + dx * ext, by + dy * ext, 7, COL_WIN);
    }
    fb_flush();
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

    memset(s_fb, 0, SCREEN_W * CHUNK_LINES * sizeof(uint16_t));
    for (int y = 0; y < SCREEN_H; y += CHUNK_LINES) {
        push_rows(y, s_fb, CHUNK_LINES);
    }
    gpio_set_level(LCD_BL, 1);
    return ESP_OK;
}
