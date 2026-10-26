/* The screens: dark background, 1-2 px lines, one accent colour. Layout constants
 * are shared by drawing and hit-testing so taps always match what is shown. */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "display.h"

static uint16_t player_col(uint8_t mark) { return mark == CELL_X ? COL_X : COL_O; }
static uint16_t player_dim(uint8_t mark) { return mark == CELL_X ? COL_X_DIM : COL_O_DIM; }

typedef struct {
    int x, y, w, h;
} rect_t;

static bool in_rect(rect_t r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/* top bar (home screen only) */
#define BAR_H 22

/* Scrolling lists: content is laid out in list coordinates (0 = top of the list) and
 * drawn at screen y = LIST_Y0 + y - scroll, clipped to LIST_Y0..LIST_Y1. */
#define LIST_Y1 216 /* the footer hint stays below */

/* home screen: a list of full-width tiles, one per game */
#define MENU_Y0 (BAR_H + 1)
#define TILE_X 16
#define TILE_Y0 35
#define TILE_W 208
#define TILE_H 72
#define TILE_GAP 8

/* confirm */
static const rect_t BTN_NO = {20, 140, 94, 48};
static const rect_t BTN_YES = {126, 140, 94, 48};

/* turn banner above the board, the same for every game */
static const rect_t BANNER = {56, 5, 128, 28};

/* settings: a list of rows below the title */
#define SET_Y0 41
#define ROW_VOL 0
#define ROW_WIFI 72
#define ROW_KEYS 156
#define ROW_ABOUT 236
#define SET_H 284
#define SL_X0 24
#define SL_X1 216
#define SL_Y (ROW_VOL + 48)
/* only the bar itself (with a finger's margin) moves the volume */
static const rect_t SLIDER_AREA = {SL_X0 - 12, SL_Y - 12, SL_X1 - SL_X0 + 24, 24};
static const rect_t BTN_SETUP = {140, ROW_WIFI + 10, 84, 32};

/* wifi setup */
static const rect_t BTN_DONE = {70, 192, 100, 34};

static rect_t tile_rect(int i) { return (rect_t){TILE_X, TILE_Y0 + i * (TILE_H + TILE_GAP), TILE_W, TILE_H}; }

static int menu_h(void) { return TILE_Y0 + GAME_COUNT * (TILE_H + TILE_GAP) - TILE_GAP + 12; }

/* rect r of a list scrolled by scroll, in screen coordinates */
static rect_t at(rect_t r, int y0, int scroll) { return (rect_t){r.x, y0 + r.y - scroll, r.w, r.h}; }

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

static void draw_menu(const app_view_t *v)
{
    draw_bar(v);
    int y0 = MENU_Y0 - v->menu_scroll;
    gfx_clip_y(MENU_Y0, LIST_Y1);
    gfx_text(16, y0 + 11, "GAMES", 1, COL_DIM);
    for (int i = 0; i < GAME_COUNT; i++) {
        rect_t r = at(tile_rect(i), MENU_Y0, v->menu_scroll);
        if (r.y + r.h < MENU_Y0 || r.y >= LIST_Y1) {
            continue;
        }
        gfx_rrect(r.x, r.y, r.w, r.h, 12, COL_LINE);
        if (GAMES[i]->icon) {
            GAMES[i]->icon(r.x + 12, r.y + 12, 48);
        }
        gfx_text(r.x + 72, r.y + (r.h - 14) / 2, GAMES[i]->title, 2, COL_TEXT);
    }
    gfx_clip_y(0, SCREEN_H);
    gfx_text_mid(SCREEN_W / 2, 226, "HOLD L + R FOR SETTINGS", 1, COL_DIM);
}

/* scores and turn banner around the board of the game being played */
static void draw_game(const app_view_t *v)
{
    const game_slot_t *s = &v->games[v->game_idx];
    const game_round_t *g = game_round(s->st.bytes);
    char buf[16];
    snprintf(buf, sizeof(buf), "X %d", s->wins_x > 99 ? 99 : s->wins_x);
    gfx_text(8, 13, buf, 2, COL_X);
    snprintf(buf, sizeof(buf), "O %d", s->wins_o > 99 ? 99 : s->wins_o);
    gfx_text_right(232, 13, buf, 2, COL_O);

    /* turn banner, filled in the colour of the player it is about */
    uint16_t tc, fill;
    if (g->status == GAME_WIN) {
        snprintf(buf, sizeof(buf), "%c WINS", game_mark_char(g->winner));
        tc = player_col(g->winner);
        fill = player_dim(g->winner);
    } else if (g->status == GAME_DRAW) {
        snprintf(buf, sizeof(buf), "DRAW");
        tc = COL_TEXT;
        fill = COL_LINE;
    } else {
        snprintf(buf, sizeof(buf), g->moves == 0 ? "%c STARTS" : "%c TO MOVE", game_mark_char(g->turn));
        tc = player_col(g->turn);
        fill = player_dim(g->turn);
    }
    rect_t b = BANNER;
    gfx_rect(b.x + 2, b.y + 1, b.w - 4, b.h - 2, fill);
    gfx_rect(b.x + 1, b.y + 2, b.w - 2, b.h - 4, fill);
    gfx_rrect(b.x, b.y, b.w, b.h, 4, tc);
    gfx_text_mid(b.x + b.w / 2, b.y + (b.h - 14) / 2, buf, 2, tc);

    GAMES[v->game_idx]->draw(s->st.bytes);
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
    int y0 = SET_Y0 - v->settings_scroll; /* screen y of list row 0 */
    gfx_clip_y(SET_Y0, LIST_Y1);

    /* volume 0 is silent: there is no separate mute */
    bool silent = v->vol == 0;
    gfx_text(16, y0 + ROW_VOL + 14, "VOLUME", 2, COL_TEXT);
    if (silent) {
        snprintf(buf, sizeof(buf), "OFF");
    } else {
        snprintf(buf, sizeof(buf), "%d", v->vol);
    }
    gfx_text_right(224, y0 + ROW_VOL + 14, buf, 2, silent ? COL_DIM : COL_ACCENT);
    int kx = SL_X0 + (SL_X1 - SL_X0) * v->vol / 100, ky = y0 + SL_Y;
    uint16_t sc = silent ? COL_DIM : COL_ACCENT;
    gfx_rect(SL_X0, ky - 1, SL_X1 - SL_X0, 2, COL_LINE);
    gfx_rect(SL_X0, ky - 1, kx - SL_X0, 2, sc);
    gfx_disc(kx, ky, 7, sc);
    gfx_rect(0, y0 + ROW_WIFI - 1, SCREEN_W, 1, COL_LINE);

    gfx_text(16, y0 + ROW_WIFI + 19, "WI-FI", 2, COL_TEXT);
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
    gfx_text(16, y0 + ROW_WIFI + 58, buf, 1, v->wifi == WIFI_CONNECTED ? COL_ACCENT : COL_DIM);
    button(at(BTN_SETUP, SET_Y0, v->settings_scroll), "SET UP", COL_TEXT);
    gfx_rect(0, y0 + ROW_KEYS - 1, SCREEN_W, 1, COL_LINE);

    gfx_text(16, y0 + ROW_KEYS + 12, "BUTTONS", 2, COL_TEXT);
    gfx_text(16, y0 + ROW_KEYS + 36, "LEFT: BACK", 1, COL_DIM);
    gfx_text(16, y0 + ROW_KEYS + 50, "HOLD LEFT + RIGHT: SETTINGS", 1, COL_DIM);
    gfx_text(16, y0 + ROW_KEYS + 64, "HOLD MIDDLE: POWER OFF", 1, COL_DIM);
    gfx_rect(0, y0 + ROW_ABOUT - 1, SCREEN_W, 1, COL_LINE);

    gfx_text(16, y0 + ROW_ABOUT + 12, "VERSION", 1, COL_DIM);
    snprintf(buf, sizeof(buf), "%.32s", v->version);
    gfx_text(16, y0 + ROW_ABOUT + 26, buf, 1, COL_TEXT);

    gfx_clip_y(0, SCREEN_H);
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
        draw_game(v);
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
    case SCR_OFF:
        gfx_text_mid(SCREEN_W / 2, 100, "POWERING OFF", 2, COL_TEXT);
        gfx_text_mid(SCREEN_W / 2, 130, "RELEASE THE BUTTON", 1, COL_DIM);
        break;
    }
    display_flush();
}

bool ui_list_size(screen_t s, int *view_h, int *content_h)
{
    if (s == SCR_MENU) {
        *view_h = LIST_Y1 - MENU_Y0;
        *content_h = menu_h();
        return true;
    }
    if (s == SCR_SETTINGS) {
        *view_h = LIST_Y1 - SET_Y0;
        *content_h = SET_H;
        return true;
    }
    return false;
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
        if (y < MENU_Y0 || y >= LIST_Y1) {
            return HIT_NONE;
        }
        for (int i = 0; i < GAME_COUNT; i++) {
            if (in_rect(at(tile_rect(i), MENU_Y0, v->menu_scroll), x, y)) {
                *arg = i;
                return HIT_TILE;
            }
        }
        return HIT_NONE;
    case SCR_GAME: {
        if (game_round(v->games[v->game_idx].st.bytes)->status != GAME_PLAYING) {
            return HIT_NEW_GAME;
        }
        int m = GAMES[v->game_idx]->hit(x, y);
        if (m < 0) {
            return HIT_NONE;
        }
        *arg = m;
        return HIT_MOVE;
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
        if (y < SET_Y0 || y >= LIST_Y1) {
            return HIT_NONE;
        }
        if (in_rect(at(SLIDER_AREA, SET_Y0, v->settings_scroll), x, y)) {
            *arg = ui_slider_value(x);
            return HIT_SLIDER;
        }
        if (in_rect(at(BTN_SETUP, SET_Y0, v->settings_scroll), x, y)) {
            return HIT_WIFI_SETUP;
        }
        return HIT_NONE;
    case SCR_WIFI:
        return in_rect(BTN_DONE, x, y) ? HIT_WIFI_DONE : HIT_NONE;
    case SCR_OFF:
        return HIT_NONE;
    }
    return HIT_NONE;
}
