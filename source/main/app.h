#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "game.h"
#include "net.h"

typedef enum { SCR_MENU, SCR_GAME, SCR_CONFIRM, SCR_SETTINGS, SCR_WIFI, SCR_OFF } screen_t;

/* One entry per game on the home screen. A new game adds an entry here, its
 * tile icon and screen in ui.c, and its actions in main.c. */
typedef struct {
    const char *id; /* UART name: "open ttt" */
    const char *title;
    const char *subtitle;
} game_info_t;

extern const game_info_t GAMES[];
extern const int GAME_COUNT;

/* Everything the screens show; main.c owns it, the display task renders a copy. */
typedef struct {
    screen_t screen;
    int game_idx;
    game_t game;
    int wins_x, wins_o;
    uint8_t next_starter; /* who starts the next round: alternates after each finished one */
    int vol;              /* 0..100, 0 = silent */
    int menu_scroll;      /* list offsets in px (negative or past the end while stretched) */
    int settings_scroll;
    int bat_mv, bat_pct;
    wifi_state_t wifi;
    char ip[16];
    char ap_name[24];
    char version[33];
} app_view_t;

/* one console line, written in one piece so tasks do not interleave */
void app_out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Wi-Fi state changed: redraw */
void app_net_changed(void);
/* the GET /status JSON */
void app_status_json(char *buf, size_t n);
