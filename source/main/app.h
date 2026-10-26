#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "game/game.h"
#include "net.h"
#include "ota.h"

typedef enum { SCR_MENU, SCR_GAME, SCR_CONFIRM, SCR_SETTINGS, SCR_WIFI, SCR_OFF, SCR_RULES, SCR_UPDATE } screen_t;

/* One entry per game on the home screen, in menu order (main.c). A new game is a
 * module in game/ exporting a game_def_t, added to this list. */
#define GAME_MAX 5
extern const game_def_t *const GAMES[];
extern const int GAME_COUNT;

/* one game's state and score, kept while another game or screen is shown */
typedef struct {
    union {
        uint8_t bytes[GAME_STATE_MAX]; /* the game's own state, starting with a game_round_t */
        uint32_t align;
    } st;
    int wins_x, wins_o;
    uint8_t next_starter; /* who starts the next round: alternates after each finished one */
} game_slot_t;

/* Everything the screens show; main.c owns it, the display task renders a copy. */
typedef struct {
    screen_t screen;
    int game_idx;
    int preview; /* the move a finger is aiming in a drag game, -1 for none */
    game_slot_t games[GAME_MAX];
    int vol;              /* 0..100, 0 = silent */
    int menu_scroll;      /* list offsets in px (negative or past the end while stretched) */
    int settings_scroll;
    int bat_mv, bat_pct;
    wifi_state_t wifi;
    char ip[16];
    char ap_name[24];
    char version[33]; /* build id (commit) from the app description */
    ota_view_t ota;   /* the updater (update screen) */
    int hint_dy;      /* px the menu's "hold L + R" hint has slid down (HINT_SLIDE_PX: gone) */
    bool off_idle;    /* the off screen is for the idle power-off, not the power key */
} app_view_t;

#define HINT_SLIDE_PX 16

/* human-readable firmware version, defined once in source/CMakeLists.txt */
#ifndef FW_VERSION
#error "FW_VERSION must be defined by the build (source/CMakeLists.txt)"
#endif

/* one console line, written in one piece so tasks do not interleave */
void app_out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Wi-Fi state changed: redraw */
void app_net_changed(void);
/* update check / download progressed: redraw */
void app_ota_changed(void);
/* the GET /status JSON */
void app_status_json(char *buf, size_t n);
