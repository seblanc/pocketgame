/* The screens: dark background, 1-2 px lines, one accent colour. Layout constants
 * are shared by drawing and hit-testing so taps always match what is shown. */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "display.h"

#define COL_BG RGB(8, 10, 14)
#define COL_LINE RGB(48, 54, 66)
#define COL_DIM RGB(110, 118, 132)
#define COL_TEXT RGB(226, 230, 236)
#define COL_ACCENT RGB(0, 214, 170)

typedef struct {
    int x, y, w, h;
} rect_t;

static bool in_rect(rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/* top bar (home screen only) */
#define BAR_H 22

/* home screen: one full-width tile per game */
#define TILE_X 16
#define TILE_Y0 58
#define TILE_W 208
#define TILE_H 72
#define TILE_GAP 8
#define TILES_MAX 2

/* tic-tac-toe grid */
#define GRID_X0 21
#define GRID_Y0 39
#define CELL_PX 66

/* confirm */
static const rect_t BTN_NO = {20, 140, 94, 48};
static const rect_t BTN_YES = {126, 140, 94, 48};

/* settings */
#define SL_X0 24
#define SL_X1 216
#define SL_Y 88
static const rect_t SLIDER_AREA = {8, 68, 224, 42};
static const rect_t TOGGLE = {176, 121, 44, 24};
static const rect_t ROW_MUTE = {0, 114, 240, 40};
static const rect_t BTN_SETUP = {140, 162, 84, 32};

/* wifi setup */
static const rect_t BTN_DONE = {70, 192, 100, 34};

static rect_t tile_rect(int i) { return (rect_t){TILE_X, TILE_Y0 + i * (TILE_H + TILE_GAP), TILE_W, TILE_H}; }

static void button(rect_t r, const char *label, uint16_t c)
{
    gfx_rrect(r.x, r.y, r.w, r.h, 8, c);
    gfx_text_mid(r.x + r.w / 2, r.y + (r.h - 14) / 2, label, 2, c);
}

static void hline(int y) { gfx_rect(0, y, SCREEN_W, 1, COL_LINE); }

static void title(const char *s)
{
    gfx_text(16, 14, s, 2, COL_TEXT);
    hline(40);
}

static void wifi_icon(float x, float y, wifi_state_t st)
{
    uint16_t c = st == WIFI_CONNECTED ? COL_ACCENT : COL_DIM;
    for (int i = 1; i <= 3; i++) {
        gfx_arc(x, y, 4.0f * i, 1.6f, 225, 315, c);
    }
    gfx_disc(x, y, 1.6f, st == WIFI_CONNECTING ? COL_ACCENT : c);
    if (st == WIFI_OFF || st == WIFI_FAILED) {
        gfx_line(x - 8, y - 12, x + 7, y + 1, 1.4f, COL_TEXT);
    }
}

static void draw_bar(const app_view_t *v)
{
    char buf[12];
    /* battery outline, charge level and percent */
    gfx_rrect(10, 6, 20, 10, 2, COL_DIM);
    gfx_rect(30, 9, 2, 4, COL_DIM);
    bool known = v->bat_mv >= 2500;
    if (known) {
        gfx_rect(12, 8, (16 * v->bat_pct + 50) / 100, 6, v->bat_pct <= 15 ? COL_TEXT : COL_ACCENT);
        snprintf(buf, sizeof(buf), "%d%%", v->bat_pct);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    gfx_text(38, 4, buf, 2, COL_TEXT);

    wifi_icon(224, 17, v->wifi);
    if (v->wifi == WIFI_SETUP) {
        gfx_text_right(206, 8, "SETUP", 1, COL_DIM);
    }
    hline(BAR_H);
}

static void ttt_icon(int x, int y, int s)
{
    float t = s / 3.0f;
    for (int i = 1; i < 3; i++) {
        gfx_rect(x + (int)(i * t), y, 1, s, COL_LINE);
        gfx_rect(x, y + (int)(i * t), s, 1, COL_LINE);
    }
    float m = t * 0.25f;
    gfx_line(x + m, y + m, x + t - m, y + t - m, 1.6f, COL_TEXT);
    gfx_line(x + m, y + t - m, x + t - m, y + m, 1.6f, COL_TEXT);
    gfx_ring(x + 1.5f * t, y + 1.5f * t, t * 0.3f, 1.6f, COL_ACCENT);
    gfx_line(x + 2 * t + m, y + 2 * t + m, x + 3 * t - m, y + 3 * t - m, 1.6f, COL_TEXT);
    gfx_line(x + 2 * t + m, y + 3 * t - m, x + 3 * t - m, y + 2 * t + m, 1.6f, COL_TEXT);
}

static void draw_menu(const app_view_t *v)
{
    draw_bar(v);
    gfx_text(16, 34, "GAMES", 1, COL_DIM);
    for (int i = 0; i < GAME_COUNT && i < TILES_MAX; i++) {
        rect_t r = tile_rect(i);
        gfx_rrect(r.x, r.y, r.w, r.h, 12, COL_LINE);
        if (strcmp(GAMES[i].id, "ttt") == 0) {
            ttt_icon(r.x + 12, r.y + 12, 48);
        }
        gfx_text(r.x + 72, r.y + 22, GAMES[i].title, 2, COL_TEXT);
        gfx_text(r.x + 72, r.y + 42, GAMES[i].subtitle, 1, COL_DIM);
    }
    gfx_text_mid(SCREEN_W / 2, 226, "HOLD L + R FOR SETTINGS", 1, COL_DIM);
}

static void cell_center(int cell, float *cx, float *cy)
{
    *cx = GRID_X0 + (cell % 3) * CELL_PX + CELL_PX / 2.0f;
    *cy = GRID_Y0 + (cell / 3) * CELL_PX + CELL_PX / 2.0f;
}

static void draw_ttt(const app_view_t *v)
{
    const game_t *g = &v->game;
    char buf[16];
    snprintf(buf, sizeof(buf), "X %d", v->wins_x > 99 ? 99 : v->wins_x);
    gfx_text(8, 12, buf, 2, COL_DIM);
    snprintf(buf, sizeof(buf), "O %d", v->wins_o > 99 ? 99 : v->wins_o);
    gfx_text_right(232, 12, buf, 2, COL_DIM);

    uint16_t tc;
    if (g->status == GAME_WIN) {
        snprintf(buf, sizeof(buf), "%c WINS", game_mark_char(g->winner));
        tc = g->winner == CELL_X ? COL_TEXT : COL_ACCENT;
    } else if (g->status == GAME_DRAW) {
        snprintf(buf, sizeof(buf), "DRAW");
        tc = COL_DIM;
    } else {
        snprintf(buf, sizeof(buf), "%c TURN", game_mark_char(g->turn));
        tc = g->turn == CELL_X ? COL_TEXT : COL_ACCENT;
    }
    gfx_text_mid(SCREEN_W / 2, 12, buf, 2, tc);

    int grid = 3 * CELL_PX;
    for (int i = 1; i < 3; i++) {
        gfx_rect(GRID_X0 + i * CELL_PX - 1, GRID_Y0 + 6, 2, grid - 12, COL_LINE);
        gfx_rect(GRID_X0 + 6, GRID_Y0 + i * CELL_PX - 1, grid - 12, 2, COL_LINE);
    }

    bool over = g->status != GAME_PLAYING;
    for (int c = 0; c < 9; c++) {
        float cx, cy;
        cell_center(c, &cx, &cy);
        bool on_line = g->status == GAME_WIN &&
                       (c == g->win_line[0] || c == g->win_line[1] || c == g->win_line[2]);
        bool dim = over && !on_line;
        if (g->cells[c] == CELL_X) {
            uint16_t col = dim ? COL_LINE : COL_TEXT;
            gfx_line(cx - 17, cy - 17, cx + 17, cy + 17, 4, col);
            gfx_line(cx - 17, cy + 17, cx + 17, cy - 17, 4, col);
        } else if (g->cells[c] == CELL_O) {
            gfx_ring(cx, cy, 19, 4, dim ? COL_LINE : COL_ACCENT);
        }
    }

    if (g->status == GAME_WIN) {
        float ax, ay, bx, by;
        cell_center(g->win_line[0], &ax, &ay);
        cell_center(g->win_line[2], &bx, &by);
        float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        float ext = 26 / len;
        gfx_line(ax - dx * ext, ay - dy * ext, bx + dx * ext, by + dy * ext, 3, COL_TEXT);
    }
}

static void draw_confirm(void)
{
    gfx_text_mid(SCREEN_W / 2, 70, "LEAVE GAME?", 2, COL_TEXT);
    gfx_text_mid(SCREEN_W / 2, 98, "THIS GAME WILL BE LOST", 1, COL_DIM);
    button(BTN_NO, "NO", COL_TEXT);
    button(BTN_YES, "YES", COL_ACCENT);
    gfx_text_mid(SCREEN_W / 2, 226, "LEFT = KEEP PLAYING", 1, COL_DIM);
}

static void draw_settings(const app_view_t *v)
{
    char buf[40];
    title("SETTINGS");

    gfx_text(16, 54, "VOLUME", 2, COL_TEXT);
    snprintf(buf, sizeof(buf), "%d", v->vol);
    gfx_text_right(224, 54, buf, 2, v->mute ? COL_DIM : COL_ACCENT);
    int kx = SL_X0 + (SL_X1 - SL_X0) * v->vol / 100;
    uint16_t sc = v->mute ? COL_DIM : COL_ACCENT;
    gfx_rect(SL_X0, SL_Y - 1, SL_X1 - SL_X0, 2, COL_LINE);
    gfx_rect(SL_X0, SL_Y - 1, kx - SL_X0, 2, sc);
    gfx_disc(kx, SL_Y, 7, sc);
    hline(112);

    gfx_text(16, 126, "MUTE", 2, COL_TEXT);
    gfx_rrect(TOGGLE.x, TOGGLE.y, TOGGLE.w, TOGGLE.h, TOGGLE.h / 2, v->mute ? COL_ACCENT : COL_LINE);
    float ky = TOGGLE.y + TOGGLE.h / 2.0f;
    if (v->mute) {
        gfx_disc(TOGGLE.x + TOGGLE.w - 12, ky, 7, COL_ACCENT);
    } else {
        gfx_disc(TOGGLE.x + 12, ky, 7, COL_DIM);
    }
    hline(156);

    gfx_text(16, 166, "WI-FI", 2, COL_TEXT);
    switch (v->wifi) {
    case WIFI_CONNECTED:
        snprintf(buf, sizeof(buf), "CONNECTED %s", v->ip);
        break;
    case WIFI_CONNECTING:
        snprintf(buf, sizeof(buf), "CONNECTING...");
        break;
    case WIFI_FAILED:
        snprintf(buf, sizeof(buf), "NOT CONNECTED");
        break;
    case WIFI_SETUP:
        snprintf(buf, sizeof(buf), "SETUP RUNNING");
        break;
    default:
        snprintf(buf, sizeof(buf), "NOT SET UP");
        break;
    }
    gfx_text(16, 188, buf, 1, v->wifi == WIFI_CONNECTED ? COL_ACCENT : COL_DIM);
    button(BTN_SETUP, "SET UP", COL_TEXT);

    snprintf(buf, sizeof(buf), "V %.12s", v->version);
    gfx_text(16, 226, buf, 1, COL_LINE);
    gfx_text_right(224, 226, "LEFT = BACK", 1, COL_DIM);
}

static void draw_wifi(const app_view_t *v)
{
    char buf[40];
    title("WI-FI SETUP");
    gfx_text(16, 54, "1. ON YOUR PHONE, JOIN", 1, COL_DIM);
    gfx_text(16, 70, v->ap_name, 2, COL_ACCENT);
    gfx_text(16, 100, "2. THE SETUP PAGE OPENS", 1, COL_DIM);
    gfx_text(16, 112, "   (OR VISIT 192.168.4.1)", 1, COL_DIM);

    const char *st;
    uint16_t c = COL_TEXT;
    buf[0] = '\0';
    switch (v->wifi) {
    case WIFI_SETUP:
        st = "WAITING...";
        c = COL_DIM;
        break;
    case WIFI_CONNECTING:
        st = "CONNECTING...";
        break;
    case WIFI_CONNECTED:
        st = "CONNECTED";
        c = COL_ACCENT;
        snprintf(buf, sizeof(buf), "IP %s", v->ip);
        break;
    case WIFI_FAILED:
        st = "NOT CONNECTED";
        snprintf(buf, sizeof(buf), "CHECK THE PASSWORD, TRY AGAIN");
        break;
    default:
        st = "STOPPED";
        c = COL_DIM;
        break;
    }
    gfx_text_mid(SCREEN_W / 2, 142, st, 2, c);
    gfx_text_mid(SCREEN_W / 2, 164, buf, 1, COL_DIM);
    button(BTN_DONE, v->wifi == WIFI_SETUP ? "CANCEL" : "DONE", COL_TEXT);
}

void ui_render(const app_view_t *v)
{
    gfx_clear(COL_BG);
    switch (v->screen) {
    case SCR_MENU:
        draw_menu(v);
        break;
    case SCR_GAME:
        draw_ttt(v);
        break;
    case SCR_CONFIRM:
        draw_confirm();
        break;
    case SCR_SETTINGS:
        draw_settings(v);
        break;
    case SCR_WIFI:
        draw_wifi(v);
        break;
    }
    display_flush();
}

int ui_slider_value(int x)
{
    int v = (x - SL_X0) * 100 / (SL_X1 - SL_X0);
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

hit_t ui_hit(const app_view_t *v, int x, int y, int *arg)
{
    switch (v->screen) {
    case SCR_MENU:
        for (int i = 0; i < GAME_COUNT && i < TILES_MAX; i++) {
            if (in_rect(tile_rect(i), x, y)) {
                *arg = i;
                return HIT_TILE;
            }
        }
        return HIT_NONE;
    case SCR_GAME: {
        if (v->game.status != GAME_PLAYING) {
            return HIT_NEW_GAME;
        }
        int col = (x - GRID_X0) / CELL_PX, row = (y - GRID_Y0) / CELL_PX;
        if (x < GRID_X0 || y < GRID_Y0 || col > 2 || row > 2) {
            return HIT_NONE;
        }
        *arg = row * 3 + col;
        return HIT_CELL;
    }
    case SCR_CONFIRM:
        if (in_rect(BTN_YES, x, y)) {
            return HIT_YES;
        }
        if (in_rect(BTN_NO, x, y)) {
            return HIT_NO;
        }
        return HIT_NONE;
    case SCR_SETTINGS:
        if (in_rect(SLIDER_AREA, x, y)) {
            *arg = ui_slider_value(x);
            return HIT_SLIDER;
        }
        if (in_rect(ROW_MUTE, x, y)) {
            return HIT_MUTE;
        }
        if (in_rect(BTN_SETUP, x, y)) {
            return HIT_WIFI_SETUP;
        }
        return HIT_NONE;
    case SCR_WIFI:
        return in_rect(BTN_DONE, x, y) ? HIT_WIFI_DONE : HIT_NONE;
    }
    return HIT_NONE;
}
