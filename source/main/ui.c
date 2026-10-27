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

/* home screen: a list of full-width tiles, one per game, right below the top bar */
#define MENU_Y0 (BAR_H + 1)
#define TILE_X 16
#define TILE_Y0 10
#define TILE_W 208
#define TILE_H 80
#define TILE_GAP 8
#define COL_BANNER RGB(22, 26, 34)

/* the menu list ends at the top of the "hold L + R" banner, the screen bottom once it is gone */
static int menu_y1(int hint_dy) { return SCREEN_H - HINT_SLIDE_PX + hint_dy; }

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
#define ROW_UPDATE 236
#define ROW_ABOUT 312
#define SET_H 366
#define SL_X0 24
#define SL_X1 216
#define SL_Y (ROW_VOL + 48)
/* only the bar itself (with a finger's margin) moves the volume */
static const rect_t SLIDER_AREA = {SL_X0 - 12, SL_Y - 12, SL_X1 - SL_X0 + 24, 24};
static const rect_t BTN_SETUP = {140, ROW_WIFI + 10, 84, 32};
static const rect_t BTN_CHECK = {140, ROW_UPDATE + 10, 84, 32};

/* wifi setup */
static const rect_t BTN_DONE = {70, 192, 100, 34};

/* update screen */
static const rect_t BTN_UPD_BACK = {16, 186, 100, 38};
static const rect_t BTN_UPD_GO = {124, 186, 100, 38};

/* the "?" of a game with a help screen, top right below the score */
static const rect_t BTN_RULES = {200, 38, 40, 40};
#define RULES_CX 220
#define RULES_CY 56

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

/* a version string like "v1.0-2026-10": the font is upper case only, so the leading
 * small "v" is drawn here (5x7 cell, x-height rows 2..6) */
static void version_text(int x, int y, const char *s, int scale, uint16_t c)
{
    if (*s == 'v') {
        static const uint8_t SMALL_V[5] = {0x1C, 0x20, 0x40, 0x20, 0x1C};
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (SMALL_V[col] & (1 << row)) {
                    gfx_rect(x + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
        x += 6 * scale;
        s++;
    }
    gfx_text(x, y, s, scale, c);
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
    int y1 = menu_y1(v->hint_dy);
    gfx_clip_y(MENU_Y0, y1);
    for (int i = 0; i < GAME_COUNT; i++) {
        rect_t r = at(tile_rect(i), MENU_Y0, v->menu_scroll);
        if (r.y + r.h < MENU_Y0 || r.y >= y1) {
            continue;
        }
        gfx_rrect(r.x, r.y, r.w, r.h, 12, COL_LINE);
        if (GAMES[i]->icon) {
            GAMES[i]->icon(r.x + 12, r.y + (r.h - 48) / 2, 48);
        }
        gfx_text(r.x + 72, r.y + (r.h - 14) / 2, GAMES[i]->title, 2, COL_TEXT);
    }
    gfx_clip_y(0, SCREEN_H);
    if (v->hint_dy < HINT_SLIDE_PX) {
        /* the whole banner, strip and text, slides down off the bottom edge a few
         * seconds after the menu opens; the screen edge clips it */
        gfx_rect(0, y1, SCREEN_W, HINT_SLIDE_PX, COL_BANNER);
        hline(y1);
        gfx_text_mid(SCREEN_W / 2, y1 + 9, "HOLD L + R FOR SETTINGS", 1, COL_DIM);
    }
}

/* scores and turn banner around the board of the game being played */
static void draw_game(const app_view_t *v)
{
    const game_slot_t *s = &v->games[v->game_idx];
    const game_round_t *g = game_round(s->st.bytes);
    if (v->preview >= 0 && g->status == GAME_PLAYING) {
        /* aiming: the top strip is the game's drop lane */
        GAMES[v->game_idx]->draw(s->st.bytes, v->preview);
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "X %d", s->wins_x > 99 ? 99 : s->wins_x);
    gfx_text(8, 13, buf, 2, COL_X);
    snprintf(buf, sizeof(buf), "O %d", s->wins_o > 99 ? 99 : s->wins_o);
    gfx_text_right(232, 13, buf, 2, COL_O);

    /* turn banner, filled in the colour of the player it is about */
    uint16_t tc, fill;
    if (GAMES[v->game_idx]->banner) {
        uint8_t mark = CELL_EMPTY;
        GAMES[v->game_idx]->banner(s->st.bytes, buf, sizeof(buf), &mark);
        tc = mark == CELL_EMPTY ? COL_TEXT : player_col(mark);
        fill = mark == CELL_EMPTY ? COL_LINE : player_dim(mark);
    } else if (g->status == GAME_WIN) {
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

    GAMES[v->game_idx]->draw(s->st.bytes, -1);
    if (GAMES[v->game_idx]->rules) {
        gfx_ring(RULES_CX, RULES_CY, 12, 1.5f, COL_DIM);
        gfx_text_mid(RULES_CX, RULES_CY - 7, "?", 2, COL_TEXT);
    }
}

static void draw_rules(const app_view_t *v)
{
    const game_def_t *g = GAMES[v->game_idx];
    title(g->title);
    int y = 52;
    for (const char *const *l = g->rules; l && *l; l++, y += 12) {
        gfx_text(12, y, *l, 1, **l ? COL_TEXT : COL_DIM);
    }
    gfx_text_mid(SCREEN_W / 2, 222, "TAP OR LEFT = BACK", 1, COL_DIM);
}

static void draw_update(const app_view_t *v)
{
    const ota_view_t *o = &v->ota;
    char buf[48];
    title("UPDATES");
    gfx_text(16, 50, "INSTALLED", 1, COL_DIM);
    version_text(16, 62, FW_VERSION, 2, COL_TEXT);
    gfx_text(16, 88, "LATEST ON GITHUB", 1, COL_DIM);
    if (o->latest[0]) {
        version_text(16, 100, o->latest, 2, o->check == UPD_NEWER ? COL_ACCENT : COL_TEXT);
    } else {
        gfx_text(16, 100, "--", 2, COL_DIM);
    }

    const char *msg = "";
    uint16_t c = COL_DIM;
    const char *go = NULL; /* the right-hand button */
    switch (o->state) {
    case OTA_CHECKING:
        msg = "CHECKING...";
        break;
    case OTA_DOWNLOADING:
    case OTA_REBOOTING: {
        int pct = o->progress < 0 ? 0 : o->progress > 100 ? 100 : o->progress;
        gfx_rrect(16, 140, 208, 16, 4, COL_LINE);
        gfx_rect(18, 142, 204 * pct / 100, 12, COL_ACCENT);
        if (o->state == OTA_REBOOTING) {
            snprintf(buf, sizeof(buf), "RESTARTING...");
        } else {
            snprintf(buf, sizeof(buf), "DOWNLOADING %d%%", pct);
        }
        gfx_text_mid(SCREEN_W / 2, 164, buf, 1, COL_TEXT);
        break;
    }
    case OTA_FAILED:
        snprintf(buf, sizeof(buf), "UPDATE FAILED: %s", o->reason);
        msg = buf;
        c = COL_O;
        go = o->from_github ? "RETRY" : "CHECK";
        break;
    case OTA_IDLE:
        switch (o->check) {
        case UPD_NEWER:
            msg = o->has_asset ? "NEW VERSION! INSTALL IT?" : "NO FIRMWARE FILE IN RELEASE";
            c = o->has_asset ? COL_ACCENT : COL_DIM;
            go = o->has_asset ? "INSTALL" : "CHECK";
            break;
        case UPD_SAME:
        case UPD_OLDER: /* an older release is never offered: no downgrade */
            snprintf(buf, sizeof(buf), "UP TO DATE (%s)", FW_VERSION);
            msg = buf;
            c = COL_ACCENT;
            go = "CHECK";
            break;
        case UPD_ERROR:
            if (strcmp(o->check_err, "repo_private") == 0 || strcmp(o->check_err, "no_release") == 0 ||
                strcmp(o->check_err, "not_found") == 0) {
                /* OTA_REPO_PRIVATE_TEXT / OTA_NO_RELEASE_TEXT, wrapped to the screen */
                static const char *const PRIV[] = {"INTERNET OK, GITHUB REACHED", "GITHUB.COM/" GITHUB_REPO,
                                                   "IS PRIVATE OR MISSING:", "MAKE THE REPO PUBLIC"};
                static const char *const NOREL[] = {"INTERNET OK, GITHUB REACHED", "GITHUB.COM/" GITHUB_REPO,
                                                    "IS PUBLIC BUT HAS", "NO PUBLISHED RELEASE"};
                static const char *const NF[] = {"INTERNET OK, GITHUB REACHED", "GITHUB.COM/" GITHUB_REPO,
                                                 "(REPO PRIVATE OR", "NO RELEASE PUBLISHED)"};
                const char *const *t = strcmp(o->check_err, "repo_private") == 0 ? PRIV
                                       : strcmp(o->check_err, "no_release") == 0 ? NOREL
                                                                                  : NF;
                for (int i = 0; i < 4; i++) {
                    gfx_text_mid(SCREEN_W / 2, 126 + 13 * i, t[i], 1, COL_O);
                }
            } else {
                snprintf(buf, sizeof(buf), "CHECK FAILED: %s", o->check_err);
                msg = buf;
                c = COL_O;
            }
            go = "RETRY";
            break;
        case UPD_NONE:
            go = "CHECK";
            break;
        }
        break;
    }
    if (*msg) {
        gfx_text_mid(SCREEN_W / 2, 146, msg, 1, c);
    }
    if (o->state != OTA_DOWNLOADING && o->state != OTA_REBOOTING) {
        button(BTN_UPD_BACK, "BACK", COL_TEXT);
    }
    if (go) {
        button(BTN_UPD_GO, go, COL_ACCENT);
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
    gfx_rect(0, y0 + ROW_UPDATE - 1, SCREEN_W, 1, COL_LINE);

    gfx_text(16, y0 + ROW_UPDATE + 19, "UPDATES", 2, COL_TEXT);
    button(at(BTN_CHECK, SET_Y0, v->settings_scroll), "CHECK", COL_TEXT);
    gfx_text(16, y0 + ROW_UPDATE + 52, "CHECK FOR UPDATES ON GITHUB", 1, COL_DIM);
    gfx_rect(0, y0 + ROW_ABOUT - 1, SCREEN_W, 1, COL_LINE);

    gfx_text(16, y0 + ROW_ABOUT + 12, "VERSION", 1, COL_DIM);
    version_text(16, y0 + ROW_ABOUT + 26, FW_VERSION, 2, COL_TEXT);

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
        gfx_text_mid(SCREEN_W / 2, 130, v->off_idle ? "NO INPUT FOR 60 S" : "RELEASE THE BUTTON", 1, COL_DIM);
        break;
    case SCR_RULES:
        draw_rules(v);
        break;
    case SCR_UPDATE:
        draw_update(v);
        break;
    }
    display_flush();
}

bool ui_list_size(screen_t s, int hint_dy, int *view_h, int *content_h)
{
    if (s == SCR_MENU) {
        *view_h = menu_y1(hint_dy) - MENU_Y0;
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
        if (y < MENU_Y0 || y >= menu_y1(v->hint_dy)) {
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
        if (GAMES[v->game_idx]->rules && in_rect(BTN_RULES, x, y)) {
            return HIT_RULES;
        }
        if (game_round(v->games[v->game_idx].st.bytes)->status != GAME_PLAYING) {
            return HIT_NEW_GAME;
        }
        const game_def_t *gd = GAMES[v->game_idx];
        int m = gd->hit_state ? gd->hit_state(v->games[v->game_idx].st.bytes, x, y) : gd->hit(x, y);
        if (m == GAME_HIT_PEN) {
            return HIT_PEN;
        }
        if (gd->drag) {
            *arg = m;
            return HIT_AIM;
        }
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
        if (in_rect(at(BTN_CHECK, SET_Y0, v->settings_scroll), x, y)) {
            return HIT_UPDATE_CHECK;
        }
        return HIT_NONE;
    case SCR_WIFI:
        return in_rect(BTN_DONE, x, y) ? HIT_WIFI_DONE : HIT_NONE;
    case SCR_OFF:
        return HIT_NONE;
    case SCR_RULES:
        return HIT_RULES_DONE;
    case SCR_UPDATE: {
        /* the same buttons draw_update() shows */
        ota_state_t st = v->ota.state;
        bool running = st == OTA_CHECKING || st == OTA_DOWNLOADING || st == OTA_REBOOTING;
        if (st != OTA_DOWNLOADING && st != OTA_REBOOTING && in_rect(BTN_UPD_BACK, x, y)) {
            return HIT_UPDATE_BACK;
        }
        if (!running && in_rect(BTN_UPD_GO, x, y)) {
            return HIT_UPDATE_GO;
        }
        return HIT_NONE;
    }
    }
    return HIT_NONE;
}
